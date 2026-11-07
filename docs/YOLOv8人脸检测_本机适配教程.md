# YOLOv8 人脸检测课程教程（本机适配版）

> 蓝本：老师发的《数据集建构过程文档_示例.pdf》  
> 本文把教程里**不适合本机**的步骤全部替换掉，并标注了原因。  
> 环境：Windows 11 + RTX 5070 Laptop + Anaconda `robot` 环境  
> 更新日期：2026-10-18

---

## 0. 本机环境清单（先看这个）

| 项目          | 本机情况                                                              |
| ----------- | ----------------------------------------------------------------- |
| 项目目录        | `C:\Users\inspiration\Desktop\专业课\26秋ai自主性机器人系统建构（跨专业）\face_yolo` |
| conda 环境    | `robot`（`D:\tools\Anaconda\envs\robot`）                           |
| Python      | 3.12.14                                                           |
| PyTorch     | 2.11.0 + cu128                                                    |
| GPU         | NVIDIA GeForce RTX 5070 Laptop GPU（8 GB）                          |
| ultralytics | 8.4.150                                                           |
| OpenCV      | 5.0                                                               |
| 本地预训练模型     | `face_yolo\yolov8n.pt`                                            |
| 已训练模型       | `face_yolo\runs\detect\jay\weights\best.pt`（类别 `jay`）             |
| 老师模型        | `face_yolo\models\teacher_best.pt`（类别 `xingxing`）                 |
| 数据集         | 100 张，train 80 / val 20，编号 001~100                                |
| 标注工具        | 独立环境 `D:\tools\labelImg_env`（labelImg 1.8.6）                      |

**总原则**：已经装好、跑通的东西不要重装；类别名 `jay` 不要改；路径以本机为准。

---

## 1. 教程与本机的差异总表

| 教程步骤           | 教程原文                                                                                          | 本机做法                                                                     | 为什么                                 |
| -------------- | --------------------------------------------------------------------------------------------- | ------------------------------------------------------------------------ | ----------------------------------- |
| 创建环境           | `conda create -n yolov8 python=3.8`                                                           | **跳过**，直接用 `robot`                                                       | 本机已有可用环境；3.8 无法装适配 5070 的新版 torch   |
| 安装 PyTorch     | `torch==1.11.0+cu113`                                                                         | **跳过**，已装 2.11.0+cu128                                                   | RTX 5070 是 Blackwell 架构，旧 cu113 不支持 |
| 安装 ultralytics | `pip install ultralytics`                                                                     | **跳过**，已装 8.4.150                                                        | 重复安装可能顺带升降级 numpy/opencv，破坏现有环境     |
| 验证环境           | `yolo predict model=yolov8n.pt source='https://ultralytics.com/images/bus.jpg'`               | 用本地图片验证（见 2.2）                                                           | 外网地址在本机网络下可能超时                      |
| data.yaml      | `nc: 1`，`names: ["1"]`                                                                        | `names: {0: jay}`                                                        | 类别名必须与串口程序里认的 `jay` 一致              |
| 训练             | `yolo detect train data=dataset.yaml model=yolov8n.pt epochs=100 imgsz=640 batch=16 device=0` | 同 PPT，仅把 data 换成 `face_dataset.yaml`；Windows 卡 DataLoader 时补 `workers=0` | 保持与课堂一致，便于对照；路径/文件名按本机              |
| 训练（后半段）        | `epochs=100 imgsz=240`                                                                        | `imgsz=640`                                                              | 240 分辨率下小脸细节损失大；640 实测 mAP50 0.94   |
| 检测模型路径         | `runs/detect/train2/weights/best.pt`                                                          | `runs/detect/jay/weights/best.pt`                                        | 每次训练的编号不同，以实际输出路径为准                 |
| 实时检测置信度        | `conf=0.5`                                                                                    | `conf=0.7`                                                               | 0.5 时周星驰测试图会误报（实测 0.65）             |
| 安装 labelImg    | 在 yolov8 环境 `pip install PyQt5 ... labelimg`                                                  | 用独立环境 `D:\tools\labelImg_env` + 两个 bat                                   | 避免 PyQt5 污染 robot 环境；兼容 bug 已修      |

---

## 2. 环境（已完成，只做验证）

### 2.1 验证命令

打开 Anaconda Prompt 或 PyCharm 终端：

```bash
conda activate robot
cd "C:\Users\inspiration\Desktop\专业课\26秋ai自主性机器人系统建构（跨专业）\face_yolo"
python -c "import torch, ultralytics; print('torch', torch.__version__); print('cuda', torch.cuda.is_available()); print('gpu', torch.cuda.get_device_name(0)); print('ultralytics', ultralytics.__version__)"
```

本机应输出：

```text
torch 2.11.0+cu128
cuda True
gpu NVIDIA GeForce RTX 5070 Laptop GPU
ultralytics 8.4.150
```

### 2.2 用本地图片验证推理

```bash
yolo detect predict model=yolov8n.pt source=test_images/test1.jpg
```

能输出检测结果就说明环境正常。

### 2.3 千万不要做

- 不要 `conda create -n yolov8 python=3.8`（机器上已有同名环境，且新版显卡需要新版 Python/torch）
- 不要在 `robot` 环境里装 `PyQt5`、`labelImg`、`pyqt5-tools`
- 不要随意 `pip install --upgrade` torch / torchvision / opencv

---

## 3. 数据集与标注（已完成）

### 3.1 当前结构

```text
face_yolo/
├─ data.yaml
├─ classes.txt                 # 内容：jay
├─ yolov8n.pt
├─ models/teacher_best.pt      # 老师模型（xingxing）
├─ runs/detect/jay/            # 本次训练输出
├─ test_images/                # 周星驰测试图（给老师模型用）
├─ datasets/faces/
│  ├─ images/train/001.jpg ... 080.jpg
│  ├─ images/val/081.jpg ... 100.jpg
│  ├─ labels/train/001.txt ... 080.txt
│  ├─ labels/val/081.txt ... 100.txt
│  ├─ raw/                     # 爬取的原图
│  ├─ 预览图.jpg
│  └─ 预览图对照.txt
└─ scripts/
   ├─ crawl_jay.py             # 百度爬图
   ├─ prepare_dataset.py       # 清洗/去重/划分/编号
   └─ pc_vision_serial.py      # PC -> 串口 -> Arduino
```

- YOLO 标签与图片**同名**：`001.jpg ↔ 001.txt`
- 每行格式：`类别ID cx cy w h`（全部归一化到 0~1）
- 本数据集只有一类，类别 ID 固定为 `0`

### 3.2 如果要从头重做

```bash
conda activate robot
cd "C:\Users\inspiration\Desktop\专业课\26秋ai自主性机器人系统建构（跨专业）\face_yolo"

# 1) 爬图（每个关键词抓多少张由 --max 控制）
python scripts/crawl_jay.py --max 60

# 2) 清洗：去坏图、去太小、筛人脸、去重，按 80/20 划分并编号为 001~100
python scripts/prepare_dataset.py --target 100 --val-ratio 0.2
```

注意：`prepare_dataset.py` 已经处理了两个本机特有的坑——

- OpenCV 5 不再自带 Haar 级联文件，项目 `assets/` 里自带了一份
- `cv2.CascadeClassifier` 读不了中文路径，脚本会先复制到系统临时目录再加载

### 3.3 标注（已完成）

1. 双击 `启动标注_train.bat`，标 001~080
2. 工具栏把 **PascalVOC** 点成 **YOLO**
3. `Ctrl+R` 保存目录设为 `datasets\faces\labels\train`
4. **W** 按住左键拖框 → 选 `jay` → **Ctrl+S** → **D** 下一张
5. 双击 `启动标注_val.bat`，同样操作，保存目录设为 `labels\val`，标 081~100

快捷键：

| 操作        | 键            |
| --------- | ------------ |
| 新建框       | W            |
| 保存        | Ctrl+S       |
| 下一张 / 上一张 | D / A        |
| 删除选中的框    | Delete       |
| 删除整张图（永久） | Ctrl+Shift+D |
| 缩放        | Ctrl+滚轮      |

说明：`labelImg 1.8.6 + PyQt5 5.15.11` 有 4 处 float/int 兼容 bug（滚轮、缩放、画框辅助线、标签文字），**已经在 `D:\tools\labelImg_env` 里修好**，不要重装 labelImg，否则 bug 会回来。

---

## 4. data.yaml（本机版本）

文件：`face_yolo\data.yaml`

```yaml
# 只认周杰伦的人脸：类别 0 = jay
path: C:/Users/inspiration/Desktop/专业课/26秋ai自主性机器人系统建构（跨专业）/face_yolo/datasets/faces
train: images/train
val: images/val

names:
  0: jay
```

要点：

- `path` 必须是**绝对路径**；`train/val` 是相对 `path` 的路径
- 中文路径在 ultralytics 8.4 下正常（它内部用 Unicode 安全的读图方式）
- **不要**照教程改成 `nc: 1`、`names: ["1"]`。类别名一旦变成 `"1"`，串口脚本里判断的 `jay` 就永远不匹配

---

## 5. 训练（已完成）

### 5.1 按老师 PPT 的命令（自己跟着做用这个）

PPT 原文：

```powershell
yolo detect train data=dataset.yaml model=yolov8n.pt epochs=100 imgsz=640 batch=16 device=0
```

本机执行版（只改文件名）：

```powershell
conda activate robot
cd "C:\Users\inspiration\Desktop\专业课\26秋ai自主性机器人系统建构（跨专业）\face_yolo"

yolo detect train data=face_dataset.yaml model=yolov8n.pt epochs=100 imgsz=640 batch=16 device=0
```

与 PPT 的差异只有一处：PPT 命令里写 `data=dataset.yaml`，但上一页要求创建的文件名是 `face_dataset.yaml`，以实际文件名为准。

如果 Windows 下卡在 DataLoader（长时间停在 “Scanning” 或报 worker 错误），再补一个 `workers=0`，这是唯一的兼容性改动。

输出默认在 `runs/detect/train/`（重复运行依次是 `train2`、`train3`），**不会覆盖**之前训好的 `runs/detect/jay/`。

### 5.2 本次已完成的训练命令（存档）

```bash
yolo detect train data=data.yaml model=yolov8n.pt ^
  epochs=100 imgsz=640 batch=8 workers=0 device=0 patience=25 ^
  project=runs/detect name=jay exist_ok=True
```

这次加了 `patience=25` 早停和 `name=jay` 固定输出目录，方便自己管理结果。

### 5.3 参数为什么这么选

| 参数         | 取值               | 原因                                            |
| ---------- | ---------------- | --------------------------------------------- |
| `model`    | `yolov8n.pt`（本地） | 8 GB 显存足够且快；`yolov8s.pt` 需要临时下载，GitHub 在本机不通  |
| `imgsz`    | 640              | 人脸在图中占比不大，低分辨率会丢细节；640 是精度/速度平衡点              |
| `batch`    | PPT 用 16；存档那次用 8 | batch 16 每轮 5 个 iteration，80 张图完全够用；显存不足再降到 8 |
| `workers`  | 0（按需）            | Windows 下多进程 DataLoader 容易卡死/报错，卡住时加 0        |
| `device`   | 0                | 使用 RTX 5070                                   |
| `epochs`   | 100              | 与 PPT 一致；小数据集 100 轮足够                         |
| `patience` | 25（存档那次）         | 25 轮无提升就早停，避免过拟合空跑                            |

如果需要别的预训练模型（例如老师要求 `yolov8s.pt`），走镜像下载：

```bash
cd "C:\Users\inspiration\Desktop\专业课\26秋ai自主性机器人系统建构（跨专业）\face_yolo"
curl -L -o yolov8s.pt https://hf-mirror.com/Ultralytics/YOLOv8/resolve/main/yolov8s.pt
```

### 5.4 本次训练结果（可直接写进报告）

第一次训练（`runs/detect/jay`）：

- 训练 64 轮触发早停（最佳第 39 轮），总耗时约 **2.5 分钟**
- val 集：**P = 0.858，R = 0.950，mAP50 = 0.940，mAP50-95 = 0.558**
- 模型：`runs/detect/jay/weights/best.pt`

第二次训练（按 PPT 命令，`runs/detect/train`）：

- 100 轮跑满（未设早停），**最佳第 73 轮**
- val 集：**P = 0.904，R = 0.945，mAP50 = 0.979，mAP50-95 = 0.578**
- 模型：`runs/detect/train/weights/best.pt`（目前效果最好）

重训时建议换一个名字，避免覆盖本次结果：

```bash
yolo detect train data=data.yaml model=yolov8n.pt epochs=100 imgsz=640 ^
  batch=8 workers=0 device=0 patience=25 project=runs/detect name=jay_rerun
```

---

## 6. 查看训练成果

打开 `runs/detect/jay/`：

| 文件                                  | 内容                             |
| ----------------------------------- | ------------------------------ |
| `results.png`                       | 总览曲线（loss、P、R、mAP），一张图说明训练是否正常 |
| `confusion_matrix.png`              | 混淆矩阵（jay / 背景）                 |
| `confusion_matrix_normalized.png`   | 归一化混淆矩阵                        |
| `BoxPR_curve.png`                   | PR 曲线                          |
| `BoxF1_curve.png`                   | F1-置信度曲线                       |
| `BoxP_curve.png` / `BoxR_curve.png` | 精确率 / 召回率曲线                    |
| `val_batch0_pred.jpg`               | **验证集预测框，最直观**                 |
| `val_batch0_labels.jpg`             | 同一批图的人工标注框，可对照                 |
| `labels.jpg`                        | 全数据集标注分布                       |
| `train_batch0~2.jpg`                | 数据增强后的训练样本                     |
| `results.csv`                       | 每一轮的精确数值                       |
| `weights/best.pt`                   | 最佳模型（实际使用）                     |
| `weights/last.pt`                   | 最后一轮模型（断点续训用）                  |

断点续训：

```bash
yolo detect train resume=True model=runs/detect/jay/weights/last.pt
```

---

## 7. 图片 / 摄像头推理

### 7.1 单张图片（命令行，最省事）

```bash
cd "C:\Users\inspiration\Desktop\专业课\26秋ai自主性机器人系统建构（跨专业）\face_yolo"

# 自己的杰伦模型：用 val 里的图，置信度 0.7
yolo detect predict model=runs/detect/jay/weights/best.pt ^
  source=datasets/faces/images/val/088.jpg conf=0.7 save=True

# 老师的周星驰模型：用 test_images，置信度 0.8
yolo detect predict model=models/teacher_best.pt ^
  source=test_images/test1.jpg conf=0.8 save=True
```

结果默认保存在 `runs/detect/predict*/`。

### 7.2 摄像头实时窗口（对应教程的示例代码）

```python
# -*- coding: utf-8 -*-
"""摄像头实时人脸检测；按 q 退出。"""
from ultralytics import YOLO
import cv2

MODEL = r"runs/detect/jay/weights/best.pt"   # 或 models/teacher_best.pt
CONF = 0.7                                    # 杰伦模型用 0.7；老师模型用 0.8

model = YOLO(MODEL)
cap = cv2.VideoCapture(0)                     # 打不开就改成 1
if not cap.isOpened():
    raise SystemExit("摄像头打开失败：先关掉微信/QQ/腾讯会议，或把 0 改成 1")

while True:
    ret, frame = cap.read()
    if not ret:
        break
    results = model(frame, conf=CONF, iou=0.45, verbose=False)
    cv2.imshow("YOLOv8 Detection", results[0].plot())
    if cv2.waitKey(1) & 0xFF == ord("q"):
        break

cap.release()
cv2.destroyAllWindows()
```

不用写代码的等价做法：

```bash
yolo detect predict model=runs/detect/jay/weights/best.pt source=0 show=True conf=0.7
```

**演示要点**：自己的模型只认周杰伦，拿周杰伦照片演示；`conf=0.7` 可以避免把周星驰误判成 jay。

---

## 8. PC → USB-TTL → Arduino 串口控制（本次课重点）

### 8.1 接线

| USB-TTL | Arduino Uno | 说明                        |
| ------- | ----------- | ------------------------- |
| TXD     | D4          | 视觉数据接收（`visionSerial` RX） |
| RXD     | D5          | 视觉数据发送（`visionSerial` TX） |
| GND     | GND         | 必须共地                      |
| VCC     | **不接**      | Arduino 已有供电，不要接          |
| 电平跳线    | 5V          | 与 Uno 电平一致                |

如果完全没反应，先把 TXD/RXD 对调试一次。

本机现有接法：

| 引脚    | 用途                         | 波特率    |
| ----- | -------------------------- | ------ |
| D0/D1 | 总线舵机调试板（烧录时拔掉 RX/TX）       | 115200 |
| D2/D3 | ASRPRO 语音模块（`softSerial`）  | 115200 |
| D4/D5 | USB-TTL 视觉（`visionSerial`） | 9600   |
| A0    | 继电器（风扇）                    | —      |

Arduino 代码：`C:\Users\inspiration\Desktop\专业课\26秋ai自主性机器人系统建构（跨专业）\sketch_sep16a\sketch_sep16a.ino`

它接受的命令：

| 命令                 | 动作                                                  |
| ------------------ | --------------------------------------------------- |
| `xingxing` 或 `jay` | 1 号舵机转到 900（`bus_servo_control(0x01, 900, 1000)`）   |
| `reback`           | 1 号舵机回中位 3000（`bus_servo_control(0x01, 3000, 500)`） |

Arduino 同一时刻只能监听一路软串口，代码里按时间片切换：语音 150 ms / 视觉 50 ms，所以 PC 端每条命令会**重复发送 8 次**兜底。

### 8.2 PC 端操作顺序

```bash
conda activate robot
cd "C:\Users\inspiration\Desktop\专业课\26秋ai自主性机器人系统建构（跨专业）\face_yolo"

# 1) 查串口
python scripts/pc_vision_serial.py --list-ports

# 2) 先不跑模型，只验证串口链路（Arduino 上舵机应动）
python scripts/pc_vision_serial.py --port COM5 --test-serial

# 3) 固定图片模式：用老师的周星驰模型
python scripts/pc_vision_serial.py --port COM5 ^
  --model models/teacher_best.pt --image test_images/test1.jpg --conf 0.8

# 4) 固定图片模式：用自己训练的杰伦模型
python scripts/pc_vision_serial.py --port COM5 ^
  --model runs/detect/train/weights/best.pt ^
  --image datasets/faces/images/val/088.jpg --conf 0.7

# 5) 摄像头实时模式（老师要求，演示用这个）
python scripts/pc_vision_serial.py --port COM5 --camera 0 ^
  --model runs/detect/train/weights/best.pt --conf 0.5
```


```

摄像头模式的行为：

- 弹出实时窗口，画面镜像显示，检测框和状态（DETECT / HOLD / COOL）直接画在画面上；按 `q` 退出。
- 发送节奏与 PPT 一致：识别到 `jay` → 发 `jay`（舵机 900）→ 3 秒后发 `reback`（舵机 3000）→ 3 秒后重新检测。
- 演示时**给摄像头看周杰伦的照片**（手机屏幕/打印照片均可）。注意：模型只认“这是不是周杰伦”，不会认你自己的脸。
- `--camera 1` 换另一个摄像头；`--no-show` 不弹窗（仅调试用）；`--dry-run --camera 0` 不接串口只看识别效果。
- 实时画面置信度会波动，`--conf 0.5` 比固定图片的 0.7/0.8 更稳。

**COM 口怎么认**：Arduino 和 USB-TTL 都是 CH340，名字一样。拔掉 USB-TTL，看哪个口消失；插回去，看哪个口出现——那个就是 USB-TTL。Python 脚本要连的是 **USB-TTL 的口**，不是 Arduino 的口。

Arduino IDE 的串口监视器开在 Arduino 自己的 COM 上，和 USB-TTL 的口不冲突，可以留着看 `VISION_MATCH` / `VISION_REBACK` 打印。

---

## 9. 常见问题速查

| 现象 | 原因 | 解决 |
|---|---|---|
| 下载 `yolov8n.pt` / `yolov8s.pt` 卡住 | GitHub 被重置 | 用 `https://hf-mirror.com/Ultralytics/YOLOv8/resolve/main/文件名` |
| `torch.cuda.is_available()` 为 False | 装成了 CPU 版或旧 cu113 | `robot` 环境已正确；不要动 torch |
| `cv2.imread` 读中文路径返回 None | OpenCV 不支持中文路径 | 用 `np.fromfile + cv2.imdecode`（项目脚本已封装） |
| 双击 bat 出现“拒绝访问。” | `echo` 里的 `->` 被 cmd 当成重定向 | 已改成 `-^>`，两个 bat 都修好 |
| labelImg 滚轮/缩放/画框时崩溃 | PyQt5 新版要求 int | 已修 4 处；不要重装 labelImg |
| 训练卡在 DataLoader | Windows 多进程问题 | `workers=0` |
| 串口打开失败 | 选错 COM / 被占用 | 用 `--list-ports`；关掉其他占用该口的程序 |
| Arduino 上传失败 / 端口被占用 | 串口监视器占着 Arduino 的 COM | 关闭串口监视器后再上传；烧录时拔掉 D0/D1 |
| 脚本发了命令但舵机不动 | TX/RX 接反 / 未共地 / 选错软串口 | 对调 TX/RX；检查 GND；确认插在 D4/D5 |
| 摄像头画面全黑 | 被其他软件占用 | 关闭微信/QQ/腾讯会议；`VideoCapture(0)` → `1` |
| 周星驰被识别成 jay | 小数据集模型置信度偏低 | 杰伦模型用 `conf=0.7` |
| 模型输出类别是 `1` 而不是 `jay` | 照教程把 `names` 写成 `["1"]` | 改回 `names: {0: jay}` 后重训 |

---

## 10. 作业提交

PPT 要求：

- 实验报告：实现过程、硬件接线、带注释的代码
- 效果图 / 视频
- 发送到老师邮箱（以老师最新通知为准）

可以附上的材料：

- 数据集：`datasets/faces/预览图.jpg`、`预览图对照.txt`
- 训练证据：`runs/detect/jay/results.png`、`confusion_matrix.png`、`results.csv`
- 效果图：`val_batch0_pred.jpg`、`runs/detect/predict*/` 下的检测图
- 模型指标：P 0.858 / R 0.950 / mAP50 0.940 / mAP50-95 0.558
- 代码：`scripts/` 下三个脚本、`data.yaml`、`sketch_sep16a.ino`
```
