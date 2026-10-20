# 疲劳/分心检测服务端：架构与技术说明

> 更新时间：2026-10-20
> 适用代码：`server/` 目录（C++17）
> 配套踩坑记录见 [linux-build-and-run.md](linux-build-and-run.md)

---

## 目录

1. [概述](#1-概述)
2. [系统架构](#2-系统架构)
3. [业务逻辑（端到端数据流）](#3-业务逻辑端到端数据流)
4. [网络协议](#4-网络协议)
5. [技术栈](#5-技术栈)
6. [检测算法原理](#6-检测算法原理)
7. [构建与运行](#7-构建与运行)
8. [关键配置参数](#8-关键配置参数)

---

## 1. 概述

一个**基于 UDP 的实时疲劳/分心检测服务端**。手机端（Android）采集视频 → 抽帧压缩成 JPEG → 用 UDP 分片发给服务端 → 服务端重组、解码、跑模型、判定状态 → JSON 回传给手机端显示。

核心特点：

- **可插拔推理后端**：mock（无模型合成数据）/ onnx（ONNX Runtime）/ tensorrt（原生 TensorRT），通过工厂切换，加载失败自动回退 mock 并打印原因。
- **依赖分层**：mock 模式只需 `cmake + g++`（asio、json 都是 header-only 且已 vendor 到 `third_party/`）；真实推理才需要 OpenCV + ONNX Runtime / CUDA + TensorRT。
- **两段模型 + 纯几何后处理**：人脸检测（YOLOv8n-face）→ 关键点（PFLD，68 点）→ EAR/MAR/头部姿态 → 时序状态机。

---

## 2. 系统架构

### 2.1 模块划分

| 目录 | 职责 |
|---|---|
| [../src/net/](../src/net/) | UDP 收发、分片协议、帧重组 |
| [../src/pipeline/](../src/pipeline/) | 推理管线（前/后处理）、状态判定、工厂 |
| [../src/inference/](../src/inference/) | 推理后端抽象接口 + ONNX Runtime / TensorRT 实现 |
| [../src/video/](../src/video/) | 视频窗口 / JPEG 解码（Windows GDI 或 OpenCV） |
| [../src/util/](../src/util/) | JSON 别名 |
| [../src/main.cpp](../src/main.cpp) | 编排层：参数解析 → 组装模块 → 定义帧回调 |

### 2.2 分层关系

```
main.cpp（编排层）
   │
   ├─ UdpServer（网络层）── FrameReassembler（重组）── Protocol.h（协议）
   │
   ├─ IPipeline（管线接口）── makePipeline 工厂
   │      ├─ MockPipeline        合成特征，跑通全链路
   │      └─ ModelPipeline       真实推理管线
   │            ├─ IInferenceBackend（后端抽象）
   │            │      ├─ OrtBackend（ONNX Runtime）
   │            │      └─ TrTBackend（原生 TensorRT）
   │            └─ StateDecider（时序状态机）
   │
   └─ video/（可选预览）
```

---

## 3. 业务逻辑（端到端数据流）

```
手机 ──UDP 分片──> UdpServer ──> FrameReassembler(重组) ──> 帧回调 ──> JSON 回传 ──> 手机
                                                              │
                            ┌─────────────────────────────────┼──────────────────────────┐
                            ① 抽帧保存(可选)  ② 解码显示(可选)  ③ 推理+状态判定  ④ 手动覆盖(可选)
```

逐步说明：

1. **注册**：手机先发一个 `ClientHello`（type=0x02），服务端记录其 IP:port 作为回包地址。
2. **分片发送**：手机把一帧 JPEG 切成每片 ≤1200 字节的 UDP 包，每个包带 11 字节头。
3. **重组**：[FrameReassembler.h](../src/net/FrameReassembler.h) 按 `frame_id` 把分片按序拼回完整 JPEG，支持乱序/重复（幂等），并直接丢弃过期帧。
4. **帧回调**（[main.cpp](../src/main.cpp) 的核心 lambda），按序执行：
   - ① 抽帧保存（`--save-dir` 启用，`--save-interval` 控制间隔，环形保留 200 张）
   - ② 视频预览（OpenCV `imshow`，可选）
   - ③ 推理 + 状态判定（`pipeline->process(jpeg)`）
   - ④ 手动覆盖（按键 `1/2/3/0/M` 覆盖/恢复自动判定）
   - 最后打包 JSON（state/ear/mar/yaw/pitch/fps/box）回传手机。
5. **退出**：主线程带超时轮询退出标志，`Ctrl+C` / `kill` 优雅关闭。

---

## 4. 网络协议

协议定义在 [Protocol.h](../src/net/Protocol.h)，与 Android 客户端共享。

### 4.1 报文头（11 字节，大端）

```
[magic 2B = 0xFA 0xCE][type 1B][frame_id 4B BE][frag_idx 2B BE][frag_cnt 2B BE]
```

| 字段 | 大小 | 说明 |
|---|---|---|
| magic | 2B | 固定 `0xFACE`，校验用 |
| type | 1B | 消息类型（见下） |
| frame_id | 4B | 帧序号，大端，单调递增 |
| frag_idx | 2B | 分片序号（0 起） |
| frag_cnt | 2B | 该帧总分片数 |

### 4.2 消息类型

```cpp
enum class MsgType : uint8_t {
  VideoFragment   = 0x01,  // 手机 -> 服务端：一帧 JPEG 的一个分片
  ClientHello     = 0x02,  // 手机 -> 服务端：注册客户端地址
  ServerState     = 0x03,  // 服务端 -> 手机：状态 JSON（不分片，frag_cnt=1）
  AnnotatedFrame  = 0x04,  // 保留
};
```

- 单帧 JPEG 被切成 `frag_cnt` 个分片，每个分片载荷 ≤1200 字节，服务端按 `frag_idx` 顺序重组。
- 服务端回传的 `ServerState` 是单包（不超 MTU），直接带 JSON payload。

---

## 5. 技术栈

| 层 | 技术 | 说明 |
|---|---|---|
| 语言 | **C++17** | `CMAKE_CXX_STANDARD 17` |
| 网络 | **Asio**（standalone，header-only） | UDP socket + 独立接收线程；`ASIO_SEPARATE_COMPILATION` |
| JSON | **nlohmann/json** | 回传手机的状态包 |
| 图像/几何 | **OpenCV 4** | `imdecode`、`resize`、letterbox、`solvePnP`、`Rodrigues`、`imshow` |
| 推理后端 A | **ONNX Runtime**（可选） | `OrtBackend` |
| 推理后端 B | **TensorRT 11.4 + CUDA 13.4** | `TrTBackend`，RTX 5070（Blackwell sm_120） |
| 构建 | **CMake 4.x + CMakePresets** | 6 个 preset；`FetchContent` + `FETCHCONTENT_SOURCE_DIR_*` 本地 vendor 依赖 |
| 模型转换 | **trtexec** | ONNX → `.engine`，shape 钉死 |

依赖开关（见 [CMakeLists.txt](../CMakeLists.txt)）：

- `FATIGUE_ENABLE_ONNX`：编译 ONNX Runtime 后端。
- `FATIGUE_ENABLE_TENSORRT`：编译原生 TensorRT 后端。
- 二者都关闭 → 纯 mock 构建，零外部依赖。

---

## 6. 检测算法原理

检测是**两段模型 + 纯几何后处理**，全在 [ModelPipeline.cpp](../src/pipeline/ModelPipeline.cpp)：

```
JPEG → 人脸检测(YOLOv8n-face) → 人脸框 → 关键点(PFLD, 68点) → 几何特征 → 时序状态机
                                                    ├── EAR（眼睛开合）
                                                    ├── MAR（嘴巴开合）
                                                    └── head-pose（yaw/pitch/roll）
```

### 6.1 人脸检测（YOLOv8n-face）

- 输入 letterbox 到 **640×640**，BGR→RGB，归一化到 [0,1]，打包成 NCHW float。
- 输出 `[1, 5, 8400]`（纯检测版）：`cx, cy, w, h, conf` 5 个字段 × 8400 个候选框。
- 置信度 >0.25 保留，按 IoU>0.5 做 NMS，取置信度最高的人脸框。
- 关键点**不来自检测器**，而是交给下面的 PFLD。

### 6.2 关键点（PFLD）

- 把人脸框 crop 出来，缩放到 **112×112**，跑 PFLD。
- 输出 136 个数 = **68 个 (x,y) 关键点**，映射回原图坐标。

### 6.3 疲劳 —— EAR（眼睛纵横比）

闭眼时上下眼睑距离会显著变小。每只眼取 6 个关键点（左眼 36–41，右眼 42–47），公式在 [Features.h](../src/pipeline/Features.h)：

```
EAR = ( |P1-P5| + |P2-P4| ) / ( 2 × |P0-P3| )
      └─── 上下眼睑距离 ───┘     └── 左右眼角宽度 ─┘
```

- 睁眼 ≈ 0.3~0.4，闭眼 ≈ 0.05~0.1，取**双眼平均**。
- `EAR < 0.20` 记为一次「闭眼」。

### 6.4 疲劳 —— MAR（嘴巴纵横比，打哈欠）

取内嘴 6 点（60/62/63/64/65/66）：

```
MAR = ( |62-66| + |63-65| ) / ( 2 × |60-64| )
        └── 上下唇距离 ──┘      └── 左右嘴角 ──┘
```

- 闭嘴 ≈ 0，打哈欠时 `MAR > 0.60` 直接判疲劳。

### 6.5 分心 —— 头部姿态（solvePnP）

- 从 68 点里挑 **6 个关键点**（鼻尖 30、下巴 8、左眼角 36、右眼角 45、左嘴角 48、右嘴角 54）。
- 配一个 **6 点 3D 人脸模型**，用 OpenCV `solvePnP` 解出旋转向量 `rvec`，再 `Rodrigues` 转成旋转矩阵 R。
- 分解成欧拉角（单位：度）：

```
pitch = atan2(R[2][1], R[2][2])                    // 抬头/低头
yaw   = atan2(-R[2][0], √(R[0][0]²+R[1][0]²))      // 左右转头
roll  = atan2(R[1][0], R[0][0])                    // 歪头
```

> ⚠️ 3D 模型已按 OpenCV 约定标定（Y 向下、Z 朝相机），正脸 `pitch≈0`。`|yaw|>25°` 或 `|pitch|>20°` 判「分心」。此前模型朝向相反导致正脸 `pitch≈±180°` 恒误报分心，已修复（见 [linux-build-and-run.md](linux-build-and-run.md) 问题 9）。

### 6.6 时序状态机（[StateDecider.cpp](../src/pipeline/StateDecider.cpp)）

单帧几何特征有噪声，用**滑动窗口 + 迟滞**的时序滤波：

```
每帧 → 塞入滑动窗口（保留最近 30 帧 ≈ 2 秒@15fps）
     → 统计窗口内「闭眼」比例
     → 按优先级给原始状态 raw：
        ① 闭眼比例 ≥ 60%  或  MAR > 0.60   → drowsy（疲劳）
        ② |yaw| > 25° 或 |pitch| > 20°     → distracted（分心）
        ③ 其余                             → awake
        ④ 无人脸                           → none
     → 迟滞：新状态需连续 15 帧（≈1 秒）不变才生效，防抖
```

设计要点：

- **疲劳优先于分心**：既闭眼又转头时报 `drowsy`。
- **不是单帧触发**：要「约 2 秒里 60% 时间闭眼」才报疲劳，偶尔眨眼不误报、长时间闭眼才报。
- 所有阈值集中在 [StateDecider.h](../src/pipeline/StateDecider.h) 的 `StateConfig`，方便统一调参。

---

## 7. 构建与运行

```bash
cd server
bash scripts/build.sh mock        # 纯 mock，零外部依赖
bash scripts/build.sh onnx        # 需 vcpkg + OpenCV + onnxruntime
bash scripts/build.sh tensorrt    # 需 CUDA + TensorRT + OpenCV
```

构建产物：`build/<preset>/fatigue_server`。

运行（TensorRT 真实推理）：

```bash
cd build/linux-tensorrt
./fatigue_server --backend tensorrt --device cuda --port 9000 \
    --save-dir frames --save-interval 5
```

> 默认 `--model-dir` 会相对可执行文件定位（自动找到 `server/models`），从任何目录启动都行；显式传 `--model-dir` 时按给定值解析。

模型文件（[models/](../models/)）：

| 文件 | 用途 | 输入 | 输出 |
|---|---|---|---|
| `yolov8n-face.onnx/.engine` | 人脸检测 | 1×3×640×640 | 1×5×8400（cx,cy,w,h,conf） |
| `pfld.onnx/.engine` | 68 点关键点 | 1×3×112×112 | 1×136（68 个 x,y） |

---

## 8. 关键配置参数

### 8.1 命令行参数（[main.cpp](../src/main.cpp)）

| 参数 | 默认 | 说明 |
|---|---|---|
| `--port` | 9000 | UDP 监听端口 |
| `--backend` | mock | `mock` / `onnx` / `tensorrt` |
| `--device` | cpu | `cpu` / `cuda` |
| `--model-dir` | 相对可执行文件定位 | 模型目录 |
| `--save-dir` | 空（不保存） | 抽帧保存目录 |
| `--save-interval` | 5 | 每 N 帧存一张 |
| `--no-view` | off | 关闭视频预览窗口 |

### 8.2 状态判定阈值（[StateDecider.h](../src/pipeline/StateDecider.h)）

```cpp
earThreshold   = 0.20f;  // 低于它算「闭眼」
earLowFrac     = 0.60f;  // 窗口内闭眼比例 ≥60% 判疲劳
marThreshold   = 0.60f;  // 打哈欠阈值
yawThreshold   = 25.0f;  // 左右转头判分心
pitchThreshold = 20.0f;  // 抬头/低头判分心
windowFrames   = 30;     // 滑动窗口长度
persistFrames  = 15;     // 状态切换需连续持续的帧数
```
