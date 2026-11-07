# -*- coding: utf-8 -*-
"""PC 端：YOLOv8 手势识别 → USB-TTL → 四轴机械臂（三种手势三套动作）。

流程与老师 face_detect_serial.py 一致，只是触发条件换成了手势，并且不同手势
执行不同动作：
    读画面/图片 → YOLOv8 推理 → 连续 N 帧识别到触发手势 → 后台线程依次发送：
        rock     握拳 → 抓取：到点→夹紧→抬高→同高度放置→松开→回中
        paper    张开 → 风扇：开 5 秒→关
        scissors 比耶 → 展示夹爪：回中→到前方→张开/夹紧五次→回中
      每条间隔 --hold 秒，动作结束后冷却，再继续检测。

下位机负责运动学解算和指令解析，上位机只做视觉和发角度/动作指令。
rock 只用 gesture+角度 和 grab/release/reback；paper/scissors 还用到
下位机新增的 fan/fanoff/show（老师原版下位机没有这三条）。

常用命令（在 gestures_yolo 目录下，robot 环境里运行）：
    python scripts/gesture_serial.py --list-ports        # 先查串口号
    python scripts/gesture_serial.py --dry-run           # 不接串口，摄像头只看识别
    python scripts/gesture_serial.py --port COM5         # 摄像头实时正式跑
    python scripts/gesture_serial.py --image 图片路径 --port COM5   # 固定图片模式
    python scripts/gesture_serial.py --dry-run --test-serial --test-action rock   # 虚拟串口排动作
    python scripts/gesture_serial.py --port COM5 --test-serial --test-action paper # 只测某一套动作
    python scripts/gesture_serial.py --port COM5 --trigger rock    # 只用握拳触发
"""
import argparse
import sys
import threading
import time
from pathlib import Path

import cv2
import numpy as np
import serial
from serial.tools import list_ports

ROOT = Path(__file__).resolve().parents[1]
DEFAULT_MODEL = ROOT / "runs" / "detect" / "gesture" / "weights" / "best.pt"

# 三种手势的类别名，与 gesture_dataset.yaml 里的 names 一致
TARGET_CLASSES = {"rock", "paper", "scissors"}

# 老师示例里的抓取角度（度），可用命令行参数改
DEFAULT_APPROACH = (45, 45, 90)   # 抓取点：末端高度 z≈60.3 mm
DEFAULT_LIFT = (45, 30, 60)       # 抬高
DEFAULT_PLACE = (0, 45, 90)       # 放置点：与抓取点同高（z≈60.3 mm），只转底座
# 说明：θ1=45°、θ2=45°、θ3=90° 时 H=108+82.5cos45+150cos135≈60.3 mm；
#       把 θ1 改成 0°，θ2/θ3 不变，H 仍≈60.3 mm，末端沿同一高度的圆弧移动。
#       原来的 (0,30,60) 高度 H≈179.4 mm，所以放置点比抓取点高约 119 mm。


def print_ports():
    ports = list(list_ports.comports())
    if not ports:
        print("没有找到任何串口。请检查 USB-TTL 是否插好、CH340 驱动是否装好。")
        return
    print("当前串口：")
    for p in ports:
        print(f"  {p.device:8s} {p.description}")
    print("\nArduino 和 USB-TTL 都是 CH340，名字一样。")
    print("拔掉 USB-TTL 再插上，新出现（或消失）的那个就是它。")


def read_image(path):
    """先按字节读再解码，兼容中文路径。"""
    data = np.fromfile(str(path), dtype=np.uint8)
    img = cv2.imdecode(data, cv2.IMREAD_COLOR)
    if img is None:
        raise ValueError(f"图片读取失败：{path}")
    return img


def angle_triple(text):
    """解析命令行里的 'a1,a2,a3'。"""
    parts = text.split(",")
    if len(parts) != 3:
        raise argparse.ArgumentTypeError(f"角度格式应为 a1,a2,a3：{text}")
    try:
        return tuple(float(p) for p in parts)
    except ValueError:
        raise argparse.ArgumentTypeError(f"角度必须是数字：{text}")


def angle_command(prefix, angles):
    """拼老师协议的一行角度命令：gesture+a1+a2+a3。"""
    return f"{prefix}+{angles[0]:g}+{angles[1]:g}+{angles[2]:g}"


def send_line(ser, text, repeat, dry_run=False):
    """发一行命令；repeat 默认 1，机械臂下位机一次就能收到。"""
    if dry_run:
        return
    for i in range(repeat):
        ser.write((text + "\n").encode("utf-8"))
        ser.flush()
        if i < repeat - 1:
            time.sleep(0.03)


class SerialReader(threading.Thread):
    """后台打印下位机回传的 OK / DONE / ERR，方便看链路状态。"""

    def __init__(self, ser, stop_event):
        super().__init__(daemon=True)
        self.ser = ser
        self.stop_event = stop_event

    def run(self):
        while not self.stop_event.is_set():
            try:
                raw = self.ser.readline()
            except Exception:
                return
            if not raw:
                continue
            text = raw.decode("ascii", errors="replace").strip()
            if text:
                print(f"  [Arduino] {text}")


# 比耶手势的“展示夹爪”姿势：上臂竖直、肘关节 90°，前臂水平伸向正前方
# (θ1,θ2,θ3) = (0,0,90)：末端 R≈150 mm、z≈190.5 mm，不再俯身
SCISSORS_SHOW = (0, 0, 90)
# 张开手势让风扇转多久（秒）
FAN_ON_SECONDS = 5
ACTION_NAMES = {"rock": "GRASP", "paper": "FAN", "scissors": "SHOW"}


def build_sequence(args, gesture):
    """不同手势对应不同的动作序列。

    rock     握拳 → 抓取：到点→夹紧→抬高→同高度放置→松开→回中
    paper    张开 → 风扇：开→关
    scissors 比耶 → 展示夹爪：回中→到前方→张开/夹紧五次→回中
    """
    if gesture == "paper":
        return ["fan", f"wait:{FAN_ON_SECONDS}", "fanoff"]
    if gesture == "scissors":
        return [
            "reback",
            angle_command(args.prefix, SCISSORS_SHOW),
            "show",
            "reback",
        ]
    return [
        angle_command(args.prefix, args.approach),
        "grab",
        angle_command(args.prefix, args.lift),
        angle_command(args.prefix, args.place),
        "release",
        "reback",
    ]


def run_sequence(ser, args, gesture):
    """按老师节奏执行一套动作，每条命令后停 --hold 秒。"""
    steps = build_sequence(args, gesture)
    total = len(steps)
    for i, text in enumerate(steps, 1):
        if text.startswith("wait:"):
            seconds = float(text.split(":", 1)[1])
            print(f"[{gesture} {i}/{total}] 等待 {seconds:g} 秒")
            time.sleep(seconds)
            continue
        print(f"[{gesture} {i}/{total}] 发送: {text}")
        send_line(ser, text, args.repeat, args.dry_run)
        time.sleep(args.hold)


def detect_frame(model, img, conf, iou):
    """推理一帧，返回 (result, 命中列表[(类别名, 置信度), ...])。"""
    result = model(img, conf=conf, iou=iou, verbose=False)[0]
    hits = []
    for cls, c in zip(result.boxes.cls, result.boxes.conf):
        name = model.names[int(cls)]
        if name in TARGET_CLASSES:
            hits.append((name, float(c)))
    return result, hits


def run_source(args, model, ser):
    """摄像头实时检测；给了 --image 就按固定图片循环。"""
    cap = None
    if args.image:
        fixed = read_image(args.image)
        print(f"固定图片模式：{args.image}")
    else:
        cap = cv2.VideoCapture(int(args.camera) if str(args.camera).isdigit() else args.camera)
        if not cap.isOpened():
            print(f"无法打开摄像头：{args.camera}")
            print("摄像头被其他程序占用（微信/QQ 视频等）或编号不对时都会打不开，试试 --camera 1。")
            return

    state = "detect"          # detect -> busy（抓取中）-> cool
    state_start = 0.0
    candidate = None          # 当前连续命中的手势
    candidate_count = 0
    worker = None
    active_gesture = None      # 正在执行动作的手势
    triggers = set(args.trigger)

    print(f"触发手势：{', '.join(sorted(triggers))}；按 q 或 Ctrl+C 结束。\n")
    try:
        while True:
            if cap is not None:
                ok, frame = cap.read()
                if not ok:
                    print("读取摄像头画面失败，退出。")
                    break
                if not args.no_mirror:
                    frame = cv2.flip(frame, 1)
            else:
                frame = fixed.copy()

            result, hits = detect_frame(model, frame, args.conf, args.iou)
            now = time.monotonic()

            # 连续 N 帧确认，避免摄像头抖动导致命令乱跳
            if hits:
                name, score = max(hits, key=lambda x: x[1])
                if name == candidate:
                    candidate_count += 1
                else:
                    candidate, candidate_count = name, 1
            else:
                candidate, candidate_count = None, 0

            if state == "detect":
                if candidate in triggers and candidate_count >= args.stable:
                    active_gesture = candidate
                    print(f"识别到手势 {candidate}（置信度 {score:.2f}），执行 {ACTION_NAMES[candidate]} 动作")
                    worker = threading.Thread(target=run_sequence, args=(ser, args, candidate), daemon=True)
                    worker.start()
                    state = "busy"
            elif state == "busy":
                if not worker.is_alive():
                    print("动作结束，冷却后继续检测。\n")
                    active_gesture = None
                    state, state_start = "cool", now
            elif state == "cool":
                if now - state_start >= args.cooldown:
                    state = "detect"
                    candidate_count = 0

            if not args.no_show:
                display = result.plot()
                best = f"{score:.2f}" if hits else "-"
                cv2.putText(display, f"{state.upper()}  {candidate or '-'}  conf={best}",
                            (10, 30), cv2.FONT_HERSHEY_SIMPLEX, 0.8, (0, 255, 0), 2)
                if active_gesture:
                    cv2.putText(display, f"ACTION: {ACTION_NAMES[active_gesture]}",
                                (10, 65), cv2.FONT_HERSHEY_SIMPLEX, 0.8, (0, 200, 255), 2)
                cv2.imshow("YOLOv8 gesture -> arm (q to quit)", display)
                if cv2.waitKey(1) & 0xFF == ord("q"):
                    break
    except KeyboardInterrupt:
        print("\n已停止。")
    finally:
        if cap is not None:
            cap.release()
        if not args.no_show:
            cv2.destroyAllWindows()


def main():
    ap = argparse.ArgumentParser(description="YOLOv8 手势识别 → USB-TTL → 四轴机械臂抓取")
    ap.add_argument("--model", default=str(DEFAULT_MODEL), help="手势模型路径")
    ap.add_argument("--image", help="固定图片路径；不填则用摄像头")
    ap.add_argument("--camera", default="0", help="摄像头编号，默认 0")
    ap.add_argument("--port", help="USB-TTL 的串口号，例如 COM5")
    ap.add_argument("--baud", type=int, default=9600, help="波特率（与 Arduino 一致）")
    ap.add_argument("--conf", type=float, default=0.6, help="置信度阈值")
    ap.add_argument("--iou", type=float, default=0.45, help="NMS 的 IoU 阈值")
    ap.add_argument("--stable", type=int, default=2, help="连续多少帧同一手势才触发")
    ap.add_argument("--hold", type=float, default=3.0, help="每条命令后的等待秒数（老师用 3 秒）")
    ap.add_argument("--cooldown", type=float, default=3.0, help="抓取结束后冷却秒数")
    ap.add_argument("--repeat", type=int, default=1, help="每条命令重复发送次数")
    ap.add_argument("--trigger", nargs="+", default=sorted(TARGET_CLASSES),
                    choices=sorted(TARGET_CLASSES), help="哪些手势触发抓取，默认三个都触发")
    ap.add_argument("--prefix", default="gesture", help="角度命令前缀，默认 gesture")
    ap.add_argument("--approach", type=angle_triple, default=DEFAULT_APPROACH,
                    help="抓取点角度 a1,a2,a3，默认 45,45,90")
    ap.add_argument("--lift", type=angle_triple, default=DEFAULT_LIFT,
                    help="抬高角度 a1,a2,a3，默认 45,30,60")
    ap.add_argument("--place", type=angle_triple, default=DEFAULT_PLACE,
                    help="目标点2角度 a1,a2,a3，默认 0,45,90（与抓取点同高）")
    ap.add_argument("--test-serial", action="store_true", help="不跑模型，循环执行动作")
    ap.add_argument("--test-action", default="rock", choices=sorted(TARGET_CLASSES),
                    help="--test-serial 时执行哪套动作，默认 rock")
    ap.add_argument("--dry-run", action="store_true", help="不打开串口，只打印")
    ap.add_argument("--list-ports", action="store_true", help="列出串口后退出")
    ap.add_argument("--no-show", action="store_true", help="不弹检测窗口")
    ap.add_argument("--no-mirror", action="store_true", help="摄像头画面不镜像")
    args = ap.parse_args()

    if args.list_ports:
        print_ports()
        return

    if not args.dry_run and not args.port:
        print_ports()
        print("\n请用 --port COMx 指定 USB-TTL 的串口号（或加 --dry-run 只看识别）。")
        sys.exit(1)

    model = None
    if not args.test_serial:
        model_path = Path(args.model)
        if not model_path.exists():
            print(f"找不到模型：{model_path}")
            sys.exit(1)
        print(f"加载模型：{model_path}")
        from ultralytics import YOLO
        model = YOLO(str(model_path))

    ser = None
    if not args.dry_run:
        try:
            ser = serial.Serial(args.port, args.baud, timeout=1, write_timeout=1)
        except serial.SerialException as e:
            print(f"串口打开失败：{e}")
            print("常见原因：串口号不对 / 被别的程序占用 / USB-TTL 没插好。")
            sys.exit(1)
        time.sleep(1.0)
        print(f"串口已打开：{args.port} @ {args.baud}")

    stop_event = threading.Event()
    if ser is not None:
        SerialReader(ser, stop_event).start()

    try:
        if args.test_serial:
            print(f"串口测试模式：循环执行 {args.test_action} 动作，Ctrl+C 结束。\n")
            while True:
                run_sequence(ser, args, args.test_action)
        else:
            run_source(args, model, ser)
    finally:
        stop_event.set()
        if ser is not None and ser.is_open:
            ser.close()
            print("串口已关闭。")


if __name__ == "__main__":
    main()
