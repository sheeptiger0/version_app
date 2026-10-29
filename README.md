# FatigueGuard — 实时疲劳/分心检测（MVP）

瘦客户端手机 + 重服务端 PC 的实时疲劳/分心检测系统。

- **Android App**（`android/`）：CameraX 采集 → JPEG 压缩 → UDP 分片发送 → 预览/状态 UI → 震动/声音告警。**不做 AI 推理**。
- **PC 服务端**（`server/`）：C++17，可插拔推理后端（`mock` / `onnx` / `tensorrt`）。UDP 重组 → 解码/预处理 → YOLOv8n-face 人脸检测 → PFLD 关键点 → EAR/MAR/solvePnP 头部姿态 → 滑动窗口决策 → JSON 状态回传。

```
[Android] CameraX YUV -> JPEG -> UDP 分片 ──┐
[Android] 预览 + 状态 + 震动/声音告警 ◄─ UDP JSON ┘
                    ▲
                    ▼
[C++ Server] UDP 重组 -> JPEG 解码 -> 推理管线(可插拔: mock/onnx/tensorrt) -> 状态决策 -> JSON 回传
```

## 目录结构

```
.
├── android/                     Android 原生 App（Kotlin + Jetpack Compose + CameraX）
└── server/                      C++17 服务端（CMake + asio + nlohmann/json，可选 OpenCV/ONNX Runtime/TensorRT）
    ├── src/
    │   ├── main.cpp             程序入口：参数解析 → 组装模块 → 帧回调
    │   ├── net/                 UDP 收发、分片协议、帧重组
    │   ├── pipeline/
    │   │   ├── base/            管线接口（IPipeline）+ 工厂 + Features/StateDecider
    │   │   ├── mock/            MockPipeline（无模型，合成数据跑通全链路）
    │   │   ├── model/           ModelPipeline（真实推理基类：预处理/后处理 + 判定）
    │   │   ├── onnx/            OnnxPipeline（ONNX Runtime 后端）
    │   │   └── trt/             TrTPipeline（原生 TensorRT 后端）
    │   ├── inference/           推理后端抽象 + OrtBackend / TrTBackend
    │   ├── video/               实时预览 + JPEG 解码（Windows GDI / OpenCV）
    │   └── util/                nlohmann/json 别名
    ├── models/                  模型权重（onnx/engine，需自行下载/导出）
    ├── scripts/                 一键编译 + trtexec 转 engine
    ├── tests/                   协议 / 端到端测试客户端
    └── docs/                    架构说明 + 踩坑记录
```

## 推理后端

| 后端 | 依赖 | 适用场景 |
| --- | --- | --- |
| `mock` | 无（仅 cmake + g++） | 无模型/无 GPU 联调，合成数据跑通全链路 |
| `onnx` | ONNX Runtime + OpenCV | 跨平台，CPU/CUDA |
| `tensorrt` | CUDA + TensorRT + OpenCV | NVIDIA GPU 原生推理（加载预编译 `.engine`） |

加载失败时服务端会打印原因并自动回退到 `mock`，不会静默失败。

## 快速开始（mock，无需模型/GPU）

### 1. 启动服务端

mock 后端不需要任何模型/GPU，会生成周期性漂移的合成人脸（清醒→分心→清醒→疲劳循环），用于验证整条链路。

**Linux：**

```bash
cd server
bash scripts/build.sh mock
./build/linux-default/fatigue_server --backend mock --port 9000
```

**Windows（MinGW）：**

```bash
cd server
cmake --preset default
cmake --build --preset default
build\default\fatigue_server.exe --backend mock --port 9000
```

看到 `[main] backend=mock listening on UDP 9000` 即成功。

### 2. 运行 Android App

1. 用 Android Studio 打开 `android/` 目录（首次会自动生成 Gradle Wrapper 并同步依赖）。
2. 手机与 PC 连同一局域网，在 App 顶部填写 PC 的局域网 IP 与端口 `9000`。
3. 点「开始检测」，授予相机权限。
4. 预览下方状态卡片会在 **清醒(绿) / 分心(黄) / 疲劳(红)** 之间变化；进入疲劳时手机会**震动 + 蜂鸣**。

## 使用真实推理（YOLOv8n-face + PFLD）

两个模型都以 `.onnx` 形式获取，放入 `server/models/`：

| 模型 | 输入 | 输出 | 说明 |
| --- | --- | --- | --- |
| `yolov8n-face.onnx` | 1×3×640×640 | 1×5×8400（框+置信度） | 人脸检测 |
| `pfld.onnx` | 1×3×112×112 | 1×136（68 个 x,y） | 关键点，缺失则退化为 5 点姿态检测 |

> 下载/导出方式见 [server/models/README.md](server/models/README.md)。

### 方案 A：ONNX Runtime（跨平台）

```bash
# 依赖（Windows 建议 vcpkg）
vcpkg install opencv4:x64-windows onnxruntime[gpu]:x64-windows
```

```bash
cd server
bash scripts/build.sh onnx          # Linux（Windows 用 cmake --preset onnx）
./build/linux-onnx/fatigue_server --backend onnx --device cpu --port 9000
# 有 NVIDIA 卡用 --device cuda
```

### 方案 B：TensorRT（NVIDIA GPU，实测通过）

环境：Ubuntu 26.04 + RTX 5070（Blackwell）+ CUDA 13.4 + TensorRT 11.4 + OpenCV 4.10。

```bash
# 1. 依赖（NVIDIA 官方源）
wget https://developer.download.nvidia.cn/compute/cuda/repos/ubuntu2604/x86_64/cuda-keyring_1.1-1_all.deb
sudo dpkg -i cuda-keyring_1.1-1_all.deb && sudo apt update
sudo apt install -y cuda-toolkit-13-4 tensorrt-dev libnvinfer-bin libopencv-dev

# 2. 把 onnx 转成 .engine（trtexec，shape 钉死、FP32）
cd server
bash scripts/build_engines.sh

# 3. 编译并运行
bash scripts/build.sh tensorrt
./build/linux-tensorrt/fatigue_server --backend tensorrt --device cuda --port 9000
```

> TensorRT 11 移除了 `trtexec --fp16` 等精度开关，engine 按 FP32 构建（两个模型都很小，5070 上实时绰绰有余）。完整踩坑见 [server/docs/linux-build-and-run.md](server/docs/linux-build-and-run.md)。

## 命令行参数（服务端）

| 参数 | 默认 | 说明 |
| --- | --- | --- |
| `--port` | `9000` | UDP 监听端口 |
| `--backend` | `mock` | `mock` / `onnx` / `tensorrt` |
| `--device` | `cpu` | `cpu` / `cuda` |
| `--model-dir` | 自动 | 模型目录（默认相对可执行文件定位到 `server/models`） |
| `--save-dir` | 空（不保存） | 抽帧保存目录（环形保留最近 200 张） |
| `--save-interval` | `5` | 每 N 帧保存一张 |
| `--no-view` | 关 | 关闭实时预览窗口（无桌面/服务器环境用） |

## 日志

所有日志统一格式：`[时间] [级别] [tag] 消息`，例如：

```
[2026-10-21 10:30:45.123] [INFO ] [main] backend=tensorrt listening on UDP 9000
```

日志级别与落盘路径是**编译期配置**（宏），运行时不再传参：

| 宏 | 默认 | 说明 |
| --- | --- | --- |
| `FATIGUE_LOG_LEVEL` | `1`（info） | `0`=debug / `1`=info / `2`=warn / `3`=error |
| `FATIGUE_LOG_FILE` | 空（只写控制台） | 追加写日志到文件（文件不带颜色） |

编译时通过 CMake 变量传入（值会写入 build 目录的 CMakeCache，后续 `bash scripts/build.sh` 沿用）：

```bash
cmake --preset linux-default -DFATIGUE_LOG_LEVEL=0 -DFATIGUE_LOG_FILE=/tmp/server.log
cmake --build --preset linux-default
```

- **输出**：`info`/`debug` 走 stdout，`warn`/`error` 走 stderr；配了 `FATIGUE_LOG_FILE` 后所有级别同时追加到文件（无颜色）。
- **关键日志**：状态切换打印 `[state] awake -> drowsy (...)`；断流超 5 秒打印 `[main] no frames for Ns`；`FATIGUE_LOG_LEVEL=0` 可看到每帧推理耗时。

## 手动判定 / 实时预览 / 抽帧保存

- **终端按键**（启动服务端的终端里按）：`1`=清醒 `2`=分心 `3`=疲劳 `0`=无 `M`=回到自动。用于人工接管判定结果。
- **实时预览**：Windows 用 GDI 窗口；Linux 装了 OpenCV 才编译 `imshow` 预览（无桌面环境加 `--no-view`）。
- **抽帧保存**：`--save-dir frames --save-interval 5` 每 5 帧存一张，`frame_000000.jpg`…`frame_000199.jpg` 环形覆盖。

## UDP 协议

每个 datagram 头部 11 字节：

```
[magic 2B=0xFACE][type 1B][frame_id 4B BE][frag_idx 2B BE][frag_cnt 2B BE][payload ≤1200B]
```

- `0x01` 视频帧分片（单帧 JPEG 按 1200B 分片，按 `frame_id` 重组）
- `0x02` 客户端 hello
- `0x03` 服务端状态回传（JSON：`state/ear/mar/yaw/pitch/fps/box`）
- `0x04` 预留（回传标注帧）

服务端回传 JSON 示例：

```json
{"state":"drowsy","ear":0.15,"mar":0.12,"yaw":3.2,"pitch":-4.1,"fps":14.8,"box":[0.30,0.25,0.40,0.50]}
```

`box` 为归一化坐标 `[x,y,w,h]`，手机端据此在预览上画框。

## 决策规则（server/src/pipeline/base/StateDecider）

- 2s 滑动窗口（约 30 帧）统计：
  - **疲劳**：窗口内 `EAR < 0.20` 占比 ≥ 60%（PERCLOS），或 `MAR > 0.60`（哈欠）。
  - **分心**：`|yaw| > 25°` 或 `|pitch| > 20°` 持续（帧级滞回）。
  - 否则**清醒**；无人脸为 `none`。
- 状态切换带滞回（`persistFrames=15`，约 1s），避免闪烁。

## 构建排障

- **需要联网**：CMake 通过 `FetchContent` 自动从 GitHub 拉取 asio 与 nlohmann/json。若构建机无法访问 GitHub，可预下载两者源码放到 `server/third_party/`（`asio/asio/include`、`json/single_include`），`CMakePresets.json` 已通过 `FETCHCONTENT_SOURCE_DIR_*` 指向该目录（见 [server/docs/linux-build-and-run.md](server/docs/linux-build-and-run.md)）。
- **一键编译脚本**：Linux 推荐 `bash scripts/build.sh mock|onnx|tensorrt`，自动做依赖检查并选对应 preset。
- **生成器**：Windows 的 `default`/`onnx` preset 用 MinGW Makefiles，`tensorrt` preset 用 Visual Studio 17 2022（TensorRT 需 MSVC）。Linux 三个 preset 均用 Unix Makefiles。
- **ONNX 路径**：`onnxruntime` 建议走 vcpkg；GPU 推理需本机有 CUDA 且安装 `onnxruntime[gpu]`。

## 已延后（后续优化方向）

- iOS 端
- Android 原生 libyuv YUV 压缩桥（当前用系统 `YuvImage`，MVP 足够）
- FaceMesh(468) 关键点变体（当前 PFLD 68 点 / 5 点姿态兜底）
- UDP 回传标注帧（当前回传 JSON box，由手机端画覆盖层）
- DirectML 后端
