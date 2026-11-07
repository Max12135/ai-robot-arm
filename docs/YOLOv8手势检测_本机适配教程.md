# YOLOv8 手势检测（石头 / 布 / 剪刀）本机适配教程

本项目与 `face_yolo` 完全独立，照搬其「训练 → 摄像头 → 串口 → Arduino」工作流，
只是把识别目标从「周杰伦的脸」换成了三种手势。

---

## 0. 本机环境清单

| 项目 | 本机情况 |
| --- | --- |
| Python 环境 | conda `robot`（Python 3.12） |
| PyTorch | 2.11.0 + cu128（RTX 5070 Laptop，sm_120） |
| ultralytics | 8.4.150 |
| 串口 | pyserial；USB-TTL 为 CH340，9600 8N1 |
| Arduino | Uno；总线舵机占 D0/D1（115200），视觉模块接 D4/D5（9600） |

在 `gestures_yolo` 目录下运行。用 PyCharm 时选 `robot` 解释器；
命令行把 `python` 换成 `D:/tools/Anaconda/envs/robot/python.exe` 也可以。

---

## 1. 识别什么、怎么识别

- 任务：**目标检测**（不是分类），输出手势框 + 类别 + 置信度。
- 类别：`rock`（握拳）、`paper`（张开手掌）、`scissors`（比耶）。
- 模型：`yolov8n.pt` 预训练权重做迁移学习。数据集只有一千多张，
  从 COCO 学到的通用特征能让模型几十轮就收敛，不必从零训练。
- 一张图里可以同时出现多个手势框，PC 端取置信度最高的那个发串口。

---

## 2. 数据集来源与许可

- 镜像下载地址：`https://hf-mirror.com/datasets/Xuban/rock-paper-scissors-roboflow`
- 原始项目：Roboflow Universe 的 *Rock Paper Scissors*（RPS v3 的 YOLO 导出副本）
- 许可：**CC BY 4.0**。报告/README 里要注明来源和许可。
- 规模：原始 1485 张 train + 100 张 val，共 1585 张，YOLO txt 标注。

下载脚本：`scripts/download_gesture_dataset.py`

```bash
python scripts/download_gesture_dataset.py
```

脚本并发数故意压到 `WORKERS = 4`：HF 镜像对 resolver 请求限流（约 100k 次/5 分钟），
并发太高会出现大批 429。脚本支持断点续传，重复运行会自动跳过已下载文件。

标签缺失时用 `scripts/fix_missing_labels.py` 串行补齐（每文件间隔 1.2 秒，
遇到 429 等 65 秒重试）。

---

## 3. 为什么要重新划分数据（重点，报告里值得写）

### 3.1 现象

原始 split 里，train 和 val 存在**同源图片**：Roboflow 对同一张原图做过裁切、
缩放、压缩等增强，HF 副本又随机重新划分，导致同一张照片的不同变体分别落进
train 和 val。验证集里出现「训练时见过的画面」，指标会虚高。

### 3.2 做法

`scripts/make_clean_split.py`：

1. 每张图缩成 32×32 灰度并标准化；
2. 对**全部图片两两**计算归一化互相关 NCC（原图/水平翻转四种组合取最大）；
3. `NCC >= 0.8` 视为同源，用并查集聚成组；完全相同的文件（MD5）直接合并；
4. 按组划分 train/val（约 9:1），**同一组只进一边**；
5. 输出到 `datasets/gestures_clean`，不破坏原始下载。

### 3.3 第一版为什么失败

第一版用感知哈希（aHash + dHash）先筛候选对，再算 NCC 确认。结果重划分后
独立校验仍有 **45 / 157** 张 val 与 train 的 NCC ≥ 0.8——裁切变体的哈希距离
可能很大，预筛阶段就把它们漏掉了。改为全量两两 NCC 后问题消失。

> 教训：**去重/去泄漏的预筛条件不能太紧**；宁可多算，也不要漏。
> 1585 张图全量两两比较用 numpy 分块矩阵乘，几秒就能算完。

### 3.4 最终结果

| 项目 | 数值 |
| --- | --- |
| 同源分组 | 995 组（172 个多图组，最大组 82 张） |
| 新划分 | train 1427 / val 158 |
| 跨 split 同源组 | 0 |
| val 对 train 最大 NCC | < 0.80 |
| `NCC >= 0.8` 的 val 图片 | **0 / 158** |

运行：

```bash
python scripts/make_clean_split.py
```

---

## 4. 数据集配置 `gesture_dataset.yaml`

```yaml
# 手势检测数据集：石头（握拳）/ 布（张开手掌）/ 剪刀（比耶）
path: C:/Users/inspiration/Desktop/专业课/26秋ai自主性机器人系统建构（跨专业）/gestures_yolo/datasets/gestures_clean
train: images/train
val: images/val
nc: 3
names: ['rock', 'paper', 'scissors']
```

`names` 的顺序就是标签里的类别编号，**不能随意改顺序**，否则标签全错。

---

## 5. 训练

### 5.1 命令

```bash
yolo detect train data=gesture_dataset.yaml model=yolov8n.pt ^
  epochs=100 imgsz=640 batch=16 device=0 workers=0 amp=False name=gesture
```

Git Bash 里把 `^` 换成 `\`，或者写成一行。

- `epochs=100`、`imgsz=640`、`batch=16`、`device=0`：与老师 PPT 一致。
- `workers=0`：Windows 下多进程 dataloader 容易出问题，关掉最稳。
- `amp=False`：本机访问不了 GitHub，AMP 自检要下载 `yolo26n.pt` 会超时；
  关掉 AMP 用 FP32 训练。代价是显存/速度略差，精度不受影响。
- `name=gesture`：结果固定保存到 `runs/detect/gesture`，
  PC 端脚本默认就读这里的 `weights/best.pt`。

### 5.2 本次训练结果

训练 100 epochs，用时约 **0.645 小时**（RTX 5070 Laptop）。

验证集（去泄漏后的 158 张）：

| 类别 | 图片数 | 实例数 | Precision | Recall | mAP50 | mAP50-95 |
| --- | --- | --- | --- | --- | --- | --- |
| all | 158 | 209 | 0.981 | 0.973 | 0.977 | 0.682 |
| rock | 74 | 83 | 0.982 | 0.964 | 0.968 | 0.670 |
| paper | 72 | 80 | 0.975 | 1.000 | 0.982 | 0.676 |
| scissors | 41 | 46 | 0.985 | 0.957 | 0.981 | 0.701 |

说明：val 集只有 158 张，单个类别几十张，指标会有几个点的波动；
但这是**去泄漏后**的结果，比原始 split 上的虚高数字可信。

训练产出在 `runs/detect/gesture/`：

- `weights/best.pt`：最终模型（PC 端默认使用）
- `results.png`、`results.csv`：loss / mAP 曲线
- `confusion_matrix.png`：混淆矩阵
- `val_batch*_pred.jpg`：验证集预测可视化
- `BoxPR_curve.png` 等：PR / F1 曲线

模型本身也只有 6 MB 左右。

---

## 6. 图片 / 摄像头推理

### 6.1 单张或整个文件夹

```bash
yolo detect predict model=runs/detect/gesture/weights/best.pt ^
  source=datasets/gestures_clean/images/val save=True name=gesture_val
```

结果保存在 `runs/detect/gesture_val/`。

### 6.2 摄像头实时窗口

```bash
python scripts/gesture_serial.py --dry-run
```

`--dry-run` 不打开串口，只看识别效果。按 `q` 退出。

- 默认摄像头 0；笔记本外接摄像头可以试 `--camera 1`。
- 默认 `--conf 0.6`，误检多就调到 0.7；手势做不全就降到 0.5。
- 画面默认左右镜像，更符合照镜子的直觉；要关掉加 `--no-mirror`。
- 连续 2 帧（`--stable 2`）识别到同一手势才触发，避免单帧抖动乱发命令。

### 6.3 固定图片模式

```bash
python scripts/gesture_serial.py --image test_images/demo_rf_025.jpg --dry-run
```

适合没摄像头或只想验证流程的时候。

`test_images/demo_*.jpg` 是从验证集挑的 6 张带框结果图，可直接做报告配图。

---

## 7. PC → USB-TTL → Arduino 串口控制

### 7.1 接线

| USB-TTL | Arduino Uno |
| --- | --- |
| TXD | D4 |
| RXD | D5 |
| GND | GND |
| VCC | **不接**（Arduino 自己供电） |

- 视觉软串口：`SoftwareSerial visionSerial(4, 5)`，**9600**。
- 语音软串口：D2/D3，115200，接 ASRPRO。
- 总线舵机：D0/D1 硬件串口，115200（烧录时先拔掉，否则占用串口）。
- 两路软串口不能同时收，Arduino 程序按 150 ms（语音）/ 50 ms（视觉）轮询切换；
  PC 端每条命令重复 8 次、间隔 30 ms，保证至少有一次落在视觉监听窗口里。

### 7.2 烧录 Arduino

打开 `arduino/gesture_vision/gesture_vision.ino` 烧录。
本机用 arduino-cli 编译验证过：7724 字节（23%），全局变量 682 字节（33%）。

三个手势分别启动三个不同的执行器：

| 命令 | 1 号舵机 | 2 号舵机 | 风扇 | 效果 |
| --- | --- | --- | --- | --- |
| `rock` | 800 | 2048（中位） | 关 | 只有 1 号舵机动 |
| `paper` | 2048（中位） | 3100 | 关 | 只有 2 号舵机动 |
| `scissors` | 2048（中位） | 2048（中位） | **开** | 只有风扇转 |
| `reback` | 2048 | 2048 | 关 | 全部复位 |

位置范围 96~4000，中位 2048。收到手势命令时，Arduino 会先调用
`gesture_take_control()` 停掉语音驱动的来回摆动/摇头，避免两边抢舵机；
风扇命令会先 `fan_stop_modes()` 清掉定时/间歇/摇头模式。

PC 端默认识别后保持 3 秒再发 `reback`，所以风扇默认转约 3 秒。
想让它转久一点，运行脚本时加 `--hold 5`（单位秒）。

### 7.3 PC 端运行顺序

```bash
# 1) 查串口（Arduino 和 USB-TTL 都是 CH340，名字一样；
#    拔插 USB-TTL，新出现的那个就是它）
python scripts/gesture_serial.py --list-ports

# 2) 只测串口链路，不跑模型；舵机应每个手势来回动
python scripts/gesture_serial.py --port COM5 --test-serial --target rock

# 3) 固定图片模式
python scripts/gesture_serial.py --port COM5 --image test_images/demo_rf_025.jpg

# 4) 摄像头实时（作业演示用这个）
python scripts/gesture_serial.py --port COM5
```

### 7.4 控制节奏

与老师 PPT 的串口功能流程一致：

```
识别到手势 → 发手势名（重复 8 次，间隔 30 ms）
           → 等 3 秒（舵机转到位、保持）
           → 发 reback（重复 8 次）
           → 等 3 秒（回中位）
           → 继续检测
```

PC 端状态机就是 `detect → hold → cool` 三态，窗口左上角会显示当前状态。

---

## 8. 常见问题

**训练一启动就卡在 `downloading yolo26n.pt`**
本机连不上 GitHub。加 `amp=False` 跳过 AMP 自检即可。

**`yolo` 命令找不到**
用 `D:/tools/Anaconda/envs/robot/Scripts/yolo.exe`，或先激活 `robot` 环境。

**摄像头打不开**
被微信/QQ/腾讯会议占用，或编号不是 0。关掉占用程序，或试 `--camera 1`。

**识别到了但 Arduino 没反应**
先跑 `--test-serial` 排除模型问题；再查 USB-TTL 贴纸上的 TX/RX 是否接反
（TXD 接 Arduino 的 RX 引脚 D4，RXD 接 D5）；最后确认 Arduino 已烧录本项目的
`gesture_vision.ino`，而不是旧程序。

**舵机不动或抖动**
总线舵机要独立供电，不要从 Uno 的 5V 取电；确认调试板波特率 115200，
ID 为 1（恢复出厂后 ID 会变回 1）。

**串口能发但舵机偶尔漏动作**
两路软串口抢 CPU + PC 端只发一次时容易丢字节。PC 端保持默认 `--repeat 8`，
不要为了「干净」把它改成 1。

---

## 9. 作业提交（PPT 第 27 页）

1. Arduino 通过串口与语音模块交互控制舵机/风扇，**或**与 AI 视觉模块交互控制舵机；
2. 实验报告：实现过程、硬件接线、软件代码及主要注释，发 `308638599@qq.com`；
3. 实现效果附图/视频。

建议报告配图直接用：

- `runs/detect/gesture/results.png`（训练曲线）
- `runs/detect/gesture/confusion_matrix.png`（混淆矩阵）
- `runs/detect/gesture/val_batch0_pred.jpg`（验证集预测）
- `test_images/demo_*.jpg`（单张检测结果）
- 摄像头演示录屏（手势切换 + 舵机动作）

数据来源注明：Rock Paper Scissors 数据集（Roboflow Universe），
经 HF 镜像 `Xuban/rock-paper-scissors-roboflow` 获取，CC BY 4.0。
