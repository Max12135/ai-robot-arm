# -*- coding: utf-8 -*-
"""PC 端：YOLOv8 识别结果通过 USB-TTL 串口发给 Arduino。

流程：读测试图 → YOLOv8 推理 → 检测到目标就发 "xingxing\\n"（或 "jay\\n"）
      → 等 3 秒 → 发 "reback\\n" → 等 3 秒 → 循环。

常用命令（在 face_yolo 目录下，robot 环境里运行）：
    python scripts/pc_vision_serial.py --list-ports        # 先查串口号
    python scripts/pc_vision_serial.py --port COM5         # 正式跑（固定图片）
    python scripts/pc_vision_serial.py --port COM5 --camera 0   # 摄像头实时检测
    python scripts/pc_vision_serial.py --dry-run --camera 0     # 不接串口，只看检测
    python scripts/pc_vision_serial.py --port COM5 --test-serial  # 不跑模型，只测串口链路
"""
import argparse
import sys
import time
from pathlib import Path

import cv2
import numpy as np
import serial
from serial.tools import list_ports

ROOT = Path(__file__).resolve().parents[1]
DEFAULT_MODEL = ROOT / "models" / "teacher_best.pt"
DEFAULT_IMAGE = ROOT / "test_images" / "test1.jpg"

# 老师模型认 xingxing，自己训练的杰伦模型认 jay；两个都接受
TARGET_CLASSES = {"xingxing", "jay"}


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
    """先按字节读再解码，兼容中文路径（cv2.imread 读不了中文路径）。"""
    data = np.fromfile(str(path), dtype=np.uint8)
    img = cv2.imdecode(data, cv2.IMREAD_COLOR)
    if img is None:
        raise ValueError(f"图片读取失败：{path}")
    return img


def send_line(ser, text, repeat, dry_run=False):
    """发一行命令；重复几次，降低两个软串口轮流监听时丢包的概率。"""
    for i in range(repeat):
        if dry_run:
            print(f"  [dry-run] 发送: {text}")
        else:
            ser.write((text + "\n").encode("utf-8"))
            ser.flush()
        if i < repeat - 1:
            time.sleep(0.03)


def detect_frame(model, img, conf, iou):
    """推理一帧，返回 (result, 命中目标列表[(类别名, 置信度), ...])。"""
    result = model(img, conf=conf, iou=iou, verbose=False)[0]
    hits = []
    for cls, c in zip(result.boxes.cls, result.boxes.conf):
        name = model.names[int(cls)]
        if name in TARGET_CLASSES:
            hits.append((name, float(c)))
    return result, hits


def detect_targets(model, img, conf, iou):
    """返回本张图里命中的目标列表 [(类别名, 置信度), ...]。"""
    _, hits = detect_frame(model, img, conf, iou)
    return hits


def run_camera(args, model, ser):
    """摄像头实时检测：画面实时显示，命令发送节奏仍按 PPT（识别→3 秒→reback→3 秒）。

    状态机：
        detect  —— 每帧检测，命中就发类别名
        hold    —— 发完类别名后等 hold 秒，再发 reback
        cool    —— 发完 reback 后等 hold 秒，再回到 detect
    """
    source = args.camera
    cap = cv2.VideoCapture(int(source) if str(source).isdigit() else source)
    if not cap.isOpened():
        print(f"无法打开摄像头/视频源：{source}")
        print("摄像头被其他程序占用（微信/QQ 视频等）或编号不对时都会打不开，试试 --camera 1。")
        return

    state = "detect"
    state_start = 0.0
    print("摄像头已打开，按 q 或 Ctrl+C 结束。\n")

    try:
        while True:
            ok, frame = cap.read()
            if not ok:
                print("读取摄像头画面失败，退出。")
                break
            if not args.no_mirror:
                frame = cv2.flip(frame, 1)   # 镜像显示，自己看着更自然

            result, hits = detect_frame(model, frame, args.conf, args.iou)
            now = time.monotonic()

            if state == "detect":
                if hits:
                    name, score = max(hits, key=lambda x: x[1])
                    print(f"检测到 {name}（置信度 {score:.2f}），发送命令")
                    send_line(ser, name, args.repeat, args.dry_run)
                    state, state_start = "hold", now
            elif state == "hold":
                if now - state_start >= args.hold:
                    print("发送回中位命令：reback")
                    send_line(ser, "reback", args.repeat, args.dry_run)
                    state, state_start = "cool", now
            elif state == "cool":
                if now - state_start >= args.hold:
                    state = "detect"

            if not args.no_show:
                display = result.plot()
                best = f"{max(hits, key=lambda x: x[1])[1]:.2f}" if hits else "-"
                cv2.putText(display, f"{state.upper()}  hits={len(hits)}  conf={best}",
                            (10, 30), cv2.FONT_HERSHEY_SIMPLEX, 0.8, (0, 255, 0), 2)
                cv2.imshow("YOLOv8 vision serial (q to quit)", display)
                if cv2.waitKey(1) & 0xFF == ord("q"):
                    break
    except KeyboardInterrupt:
        print("\n已停止。")
    finally:
        cap.release()
        if not args.no_show:
            cv2.destroyAllWindows()


def main():
    ap = argparse.ArgumentParser(description="YOLOv8 识别结果 → USB-TTL → Arduino")
    ap.add_argument("--model", default=str(DEFAULT_MODEL), help="模型路径")
    ap.add_argument("--image", default=str(DEFAULT_IMAGE), help="测试图片路径")
    ap.add_argument("--port", help="USB-TTL 的串口号，例如 COM5")
    ap.add_argument("--baud", type=int, default=9600, help="波特率（与 Arduino 一致）")
    ap.add_argument("--conf", type=float, default=0.8, help="置信度阈值")
    ap.add_argument("--iou", type=float, default=0.45, help="NMS 的 IoU 阈值")
    ap.add_argument("--hold", type=float, default=3.0, help="发送后等待秒数")
    ap.add_argument("--repeat", type=int, default=8, help="每条命令重复发送次数（盖过一个监听切换周期）")
    ap.add_argument("--target", default="xingxing", help="--test-serial 时发送的目标名")
    ap.add_argument("--test-serial", action="store_true", help="不跑模型，只循环发命令")
    ap.add_argument("--dry-run", action="store_true", help="不打开串口，只打印")
    ap.add_argument("--list-ports", action="store_true", help="列出串口后退出")
    ap.add_argument("--camera", nargs="?", const="0", default=None,
                    help="摄像头实时模式：0/1 为摄像头编号，也可给视频文件路径")
    ap.add_argument("--no-show", action="store_true", help="摄像头模式不弹检测窗口")
    ap.add_argument("--no-mirror", action="store_true", help="摄像头模式不镜像画面")
    args = ap.parse_args()

    if args.list_ports:
        print_ports()
        return

    if args.camera is not None and args.test_serial:
        print("--camera 和 --test-serial 不能一起用。")
        sys.exit(1)

    if not args.dry_run and not args.port:
        print_ports()
        print("\n请用 --port COMx 指定 USB-TTL 的串口号。")
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

    if args.camera is not None:
        try:
            run_camera(args, model, ser)
        finally:
            if ser is not None and ser.is_open:
                ser.close()
                print("串口已关闭。")
        return

    print("开始运行，Ctrl+C 结束。\n")
    try:
        while True:
            if args.test_serial:
                print(f"发送目标命令：{args.target}")
                send_line(ser, args.target, args.repeat, args.dry_run)
                time.sleep(args.hold)
            else:
                img = read_image(args.image)
                hits = detect_targets(model, img, args.conf, args.iou)
                if hits:
                    name, score = max(hits, key=lambda x: x[1])
                    print(f"检测到 {name}（置信度 {score:.2f}），发送命令")
                    send_line(ser, name, args.repeat, args.dry_run)
                    time.sleep(args.hold)
                else:
                    print("未检测到目标，继续等待...")
                    time.sleep(1.0)
                    continue

            print("发送回中位命令：reback")
            send_line(ser, "reback", args.repeat, args.dry_run)
            time.sleep(args.hold)
            print()
    except KeyboardInterrupt:
        print("\n已停止。")
    finally:
        if ser is not None and ser.is_open:
            ser.close()
            print("串口已关闭。")


if __name__ == "__main__":
    main()
