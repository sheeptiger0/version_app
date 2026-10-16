# FatigueGuard — 实时疲劳/分心检测（MVP）

瘦客户端手机 + 重服务端 PC 的实时疲劳/分心检测系统。

- **Android App**（`android/`）：CameraX 采集 → JPEG 压缩 → UDP 分片发送 → 预览/状态 UI → 震动/声音告警。**不做 AI 推理**。
- **PC 服务端**（`server/`）：C++ + ONNX Runtime。UDP 重组 → 解码/预处理 → YOLOv8n-face 人脸检测 → PFLD 关键点 → EAR/MAR/solvePnP 头部姿态 → 滑动窗口决策 → JSON 状态回传。

```
[Android] CameraX YUV -> JPEG -> UDP 分片 ──┐
[Android] 预览 + 状态 + 震动/声音告警 ◄─ UDP JSON ┘
                    ▲
                    ▼
[C++ Server] UDP 重组 -> JPEG 解码 -> 推理管线(可插拔) -> 状态决策 -> JSON 回传
```

## 目录结构

```
android/   Android 原生 App（Kotlin + Jetpack Compose + CameraX）
server/    C++ 服务端（CMake + asio + nlohmann/json + 可选 OpenCV/ONNX Runtime）
```

## 快速开始（无需模型，跑通全链路）

### 1. 启动服务端（mock 兜底）

mock 后端不需要任何模型/GPU，会生成周期性漂移的合成人脸（清醒→分心→清醒→疲劳循环），用于验证整条链路。

```bash
cd server
cmake --preset default      # 仅依赖 header-only 的 asio + nlohmann/json（自动拉取）
cmake --build --preset default
./build/default/fatigue_server --backend mock --port 9000
```

看到 `[main] backend=mock listening on UDP 9000` 即成功。

### 2. 运行 Android App

1. 用 Android Studio 打开 `android/` 目录（首次会自动生成 Gradle Wrapper 并同步依赖）。
2. 手机与 PC 连同一局域网，在 App 顶部填写 PC 的局域网 IP 与端口 `9000`。
3. 点「开始检测」，授予相机权限。
4. 预览下方状态卡片会在 **清醒(绿) / 分心(黄) / 疲劳(红)** 之间变化；进入疲劳时手机会**震动 + 蜂鸣**。

## 使用真实推理（YOLOv8n-face + PFLD + GPU）

1. 通过 vcpkg 安装依赖并准备模型：

```bash
# 依赖（Windows）
vcpkg install opencv4:x64-windows onnxruntime[gpu]:x64-windows

# 模型（放入 server/models/）
#   yolov8n-face.onnx   输入 1x3x640x640
#   pfld.onnx           输入 1x3x112x112（可选，缺失则退化为 5 点姿态检测）
cd server/models  # 见 models/README.md 的导出建议
```

2. 编译并运行：

```bash
cd server
cmake --preset onnx          # 走 vcpkg 工具链
cmake --build --preset onnx
./build/onnx/fatigue_server --backend onnx --device cpu   # 有 NVIDIA 卡用 --device cuda
```

## 命令行参数（服务端）

| 参数 | 默认 | 说明 |
| --- | --- | --- |
| `--port` | `9000` | UDP 监听端口 |
| `--backend` | `mock` | `mock`（无模型）或 `onnx`（真实推理） |
| `--device` | `cpu` | `cpu` / `cuda` |
| `--model-dir` | `models` | `.onnx` 模型目录 |

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

## 决策规则（server/src/pipeline/StateDecider）

- 2s 滑动窗口（约 30 帧）统计：
  - **疲劳**：窗口内 `EAR < 0.20` 占比 ≥ 60%（PERCLOS），或 `MAR > 0.60`（哈欠）。
  - **分心**：`|yaw| > 25°` 或 `|pitch| > 20°` 持续（帧级滞回）。
  - 否则**清醒**；无人脸为 `none`。
- 状态切换带滞回（`persistFrames=15`，约 1s），避免闪烁。

## 构建排障

- **需要联网**：CMake 通过 `FetchContent` 自动从 GitHub 拉取 asio 与 nlohmann/json。若构建机无法访问 GitHub，可手动下载两者的头文件放入 `server/third_party/`（`asio/asio/include` 与 `nlohmann/json/single_include`），并把 `CMakeLists.txt` 里对应的 `include_directories` 指向本地路径。
- **生成器**：`CMakePresets.json` 默认用 `MinGW Makefiles`（本机已验证）。若你用 Visual Studio，可把 `--preset default` 换成 `-G "Visual Studio 17 2022"` 直接配置。
- **ONNX 路径**：`onnxruntime` + OpenCV 建议走 **Visual Studio + vcpkg(x64-windows)**，因为 vcpkg 的 onnxruntime 对 MinGW triplet 支持有限；GPU 推理需本机有 CUDA 且安装 `onnxruntime[gpu]`。

## 已延后（后续优化方向）

- iOS 端
- Android 原生 libyuv YUV 压缩桥（当前用系统 `YuvImage`，MVP 足够）
- FaceMesh(468) 关键点变体（当前 PFLD 68 点 / 5 点姿态兜底）
- UDP 回传标注帧（当前回传 JSON box，由手机端画覆盖层）
- CUDA/DirectML 实机 GPU 验证
