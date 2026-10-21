# Linux 编译与运行记录（含踩坑与修复）

记录时间：2026-10-19
环境：Ubuntu / cmake 4.2.3 / g++ 15.2.0 / 24 核

---

## 一、编译

```bash
cd server
bash scripts/build.sh mock        # 最简单：无需 GPU/模型，仅需 cmake + g++
# bash scripts/build.sh onnx      # 需 vcpkg + OpenCV + onnxruntime + 模型文件
# bash scripts/build.sh tensorrt  # 需 CUDA Toolkit + TensorRT + OpenCV
```

编译产物：`build/<preset>/fatigue_server`（mock 对应 `linux-default`）。

### 问题 1：`sudo ./build.sh` 提示「权限不够」

**现象**：`sudo ./build.sh` 报 `Permission denied`。

**原因**：脚本没有可执行位（`-rw-rw-r--`，无 `x`）。`./build.sh` 这种直接执行方式要求文件有执行位，即使加了 `sudo` 也不行——root 也无法绕过 execve 对 `x` 位的检查。

**解决**（二选一）：
```bash
chmod +x build.sh && sudo ./build.sh     # 加执行位
# 或
sudo bash build.sh                        # 不依赖执行位（脚本注释里推荐的方式）
```

> 本仓库已把 `scripts/build.sh` 改为 755（可执行）。

---

### 问题 2：FetchContent 克隆 nlohmann/json 失败（网络超时）

**现象**：编译到 `json-populate` 阶段反复报错：
```
error: RPC 失败。curl 56 Recv failure: 连接超时
fatal: 无法访问 'https://github.com/nlohmann/json.git/'：GnuTLS recv error (-110)
CMake Error ... Failed to clone repository: 'https://github.com/nlohmann/json.git'
```

**原因**：CMakeLists.txt 用 `FetchContent` 从 GitHub 直接 `git clone` 两个 header-only 依赖（asio、nlohmann/json）。GitHub 直连在本地网络下极不稳定，小仓库（asio）能过、大一点的仓库（json）传输到一半就断连，重试 3 次仍失败。

**解决**：把两个依赖预下载到本地 `third_party/`，再用 CMake 官方提供的 `FETCHCONTENT_SOURCE_DIR_*` 覆盖变量让 CMake 跳过联网克隆、直接用本地源码。

具体做法：
1. 下载依赖源码（断点续传，比 git clone 稳）：
   ```bash
   wget -c https://codeload.github.com/nlohmann/json/tar.gz/refs/tags/v3.11.3 -O json.tar.gz
   wget -c https://codeload.github.com/chriskohlhoff/asio/tar.gz/refs/tags/asio-1-28-2 -O asio.tar.gz
   ```
2. 解压到 `server/third_party/`，保证结构为：
   - `third_party/json/single_include/nlohmann/json.hpp`
   - `third_party/asio/asio/include/asio.hpp`
3. 在 `CMakePresets.json` 的 `linux-default`（及其它 linux preset）里加：
   ```json
   "FETCHCONTENT_SOURCE_DIR_ASIO": "${sourceDir}/third_party/asio",
   "FETCHCONTENT_SOURCE_DIR_JSON": "${sourceDir}/third_party/json"
   ```

> ⚠️ `third_party/`（约 38MB）是构建必需项。要么提交进 git，要么加入 .gitignore 并保留本记录中的下载方式供重新填充。

---

## 二、运行

```bash
cd server
./build/linux-default/fatigue_server --backend mock --device cpu --port 9000
```

- 监听 UDP 9000，等手机端发 JPEG 分片帧 → 重组 → 推理 → 回传 JSON。
- 每 30 帧打印一次状态日志；`Ctrl+C` / `kill <PID>` 优雅退出。
- 可用 `./build/linux-default/fatigue_server --help` 查看全部参数。
- Linux 下是纯 headless：视频窗口与按键人工判定（1/2/3/0/M）仅 `_WIN32` 下编译，Linux 只有自动推理。

### 问题 3：Linux 下 `Ctrl+C` / `kill` 无法退出（信号 lost-wakeup）

**现象**：`Ctrl+C` 或 `kill -TERM <pid>` 后进程不退出，主线程一直卡在 `futex_wait`。

**原因**：`main.cpp` 用「信号处理函数设置标志 + `notify_all()` 唤醒条件变量」的模式。但 Linux 下进程级信号（SIGINT/SIGTERM）会优先投递到**主线程本身**，而主线程此刻正阻塞在 `g_cv.wait()` 里——处理函数里的 `notify_all()` 无法唤醒「自己正在等待」的那把条件变量，形成 lost-wakeup。Windows 上信号由独立线程处理，所以不触发。

**解决**：把无限 `wait()` 改成带超时的 `wait_for()` 轮询退出标志（[main.cpp](src/main.cpp)）：
```cpp
std::unique_lock<std::mutex> lk(g_cvMtx);
while (g_running.load()) {
  g_cv.wait_for(lk, std::chrono::milliseconds(100));
}
```
最多 100ms 内响应退出信号；`onSignal` 里的 `notify_all` 仍保留，用于信号投递到其它线程时的即时唤醒。

### 问题 4：`server.stop()` 卡死在 `thread_.join()`

**现象**：修复问题 3 后，退出信号能走到 `server.stop()`，但进程仍不退——主线程卡在 `pthread_join`（futex），接收线程卡在 `__skb_wait_for_more_packets`（阻塞的 recvfrom）。

**原因**：[UdpServer.cpp](src/net/UdpServer.cpp) 的 `stop()` 直接 `socket_.close()`。Linux 上 `close(fd)` 只减少文件描述符引用计数，**不会**唤醒另一个线程里已在途的阻塞 `recvfrom()`，于是 `thread_.join()` 永远等不到接收线程退出。

**解决**：close 之前先 `shutdown()`，让阻塞的 recv 立即返回：
```cpp
socket_.shutdown(asio::ip::udp::socket::shutdown_both, ec);  // 唤醒阻塞的 receive_from
socket_.close(ec);
if (thread_.joinable()) thread_.join();
```

---

## 三、端到端验证

`tests/send_jpeg_test.cpp` 是现成的测试客户端：发 ClientHello + 分片帧，打印服务端回传的 JSON。

```bash
# 编译测试客户端（用本地 asio 头文件）
g++ -std=c++17 -DASIO_STANDALONE -I src -I third_party/asio/asio/include \
    tests/send_jpeg_test.cpp -o /tmp/send_jpeg_test -lpthread

# 随便造一个「假 JPEG」（mock 后端不解码，任意字节即可）
head -c 5000 /dev/urandom > /tmp/fake.jpg

# 发送并打印回包
/tmp/send_jpeg_test /tmp/fake.jpg 9000
```

预期输出类似：
```json
{"box":[0.3,0.25,0.4,0.5],"ear":0.32,"fps":0.66,"mar":0.12,"pitch":0.0,"state":"awake","yaw":0.0}
```

退出日志应为：
```
[main] backend=mock listening on UDP 9000
[main] shutting down
```

---

## 四、待办建议

- `server/build/`（构建产物）建议加入 `.gitignore`。
- `server/third_party/`（本地依赖，约 38MB）需决定「提交进 git」还是「gitignore + 按上文重新下载」。
- `CMakeLists.txt` 中 `FetchContent_Populate` 已废弃（CMake 4.x 会告警），可迁移到 `FetchContent_MakeAvailable`；但用 `FETCHCONTENT_SOURCE_DIR_*` 覆盖本地源码时两种写法都兼容。

---

## 五、Linux 手动判定与视频预览（2026-10-20 新增）

### 问题 5：Linux 下没有「看视频」和「手动判定」功能

**现象**：Windows 版服务端有视频窗口和按键人工判定，Linux 版却没有。

**原因**：这两个功能在源码里是**仅 Windows** 的（`#ifdef _WIN32`）：视频窗口用 Win32 GDI（`VideoViewer`）、JPEG 解码用 WIC（`Jpeg.cpp`）、按键用 `_getch()`/conio。Linux 编译时这些全被跳过，服务端是 headless。

**解决**：补了 Linux 实现，且**不影响「mock 零依赖」**：

1. **终端按键**（[main.cpp](src/main.cpp)）：纯 POSIX termios，把 stdin 设成 raw + 非阻塞后轮询读键，无额外依赖。服务端启动后，在它的终端里按 `1/2/3/0/M` 即可覆盖/恢复判定结果。
2. **视频预览**（[OpenCvViewer.cpp](src/video/OpenCvViewer.cpp)）：OpenCV `imdecode` + `imshow`。CMake 里**自动探测** OpenCV——装了才编译 viewer，没装就退回 headless。

### 依赖安装（OpenCV）

最简方式（只装预览需要的三个组件）：

```bash
sudo apt install -y libopencv-core-dev libopencv-imgcodecs-dev libopencv-highgui-dev
```

> ⚠️ 注意：Ubuntu 上 `OpenCVConfig.cmake` 和 `opencv4.pc` 在 **`libopencv-dev` 元包**里，单独装上面三个 `-dev` 包是没有的，会导致 CMake `find_package(OpenCV)` 失败。已在 [CMakeLists.txt](CMakeLists.txt) 加了手动兜底（找不到 config 就按 `/usr/include/opencv4` + 标准库路径手写探测），所以只装最小集也能用。

### 使用

```bash
./build/linux-default/fatigue_server --backend mock --device cpu --port 9000
# 启动后：
#   - 弹出一个 "FatigueGuard live view" 窗口，实时显示手机发来的画面
#   - 在启动它的终端里按键：
#       1 = 清醒  2 = 分心  3 = 疲劳  0 = 无  M = 回到自动
#   - 无桌面/服务器环境加 --no-view 关闭预览
```

### 验证结果（已实测通过）

- 窗口启用：`[view] live window enabled (OpenCV)`，无 GTK/display 报错。
- 按键覆盖：按 `2` → `[manual] result -> distracted`，回传 JSON 的 `state` 变为 `distracted`；按 `m` → 回到 `awake`。
- 优雅退出：`Ctrl+C` / `kill` 正常打印 `[main] shutting down` 退出。

---

## 六、TensorRT 真实推理（GPU 自动检测，2026-10-20）

环境：Ubuntu 26.04 + RTX 5070（Blackwell）+ CUDA 13.4 + TensorRT 11.4 + OpenCV 4.10。

### 安装（NVIDIA 官方源 ubuntu2604）

```bash
wget https://developer.download.nvidia.cn/compute/cuda/repos/ubuntu2604/x86_64/cuda-keyring_1.1-1_all.deb
sudo dpkg -i cuda-keyring_1.1-1_all.deb && sudo apt update
sudo apt install -y cuda-toolkit-13-4 tensorrt-dev libnvinfer-bin \
  libnvonnxparsers11 libnvonnxparsers-dev libopencv-dev
```

- RTX 5070（Blackwell sm_120）需 CUDA ≥ 12.8；Ubuntu 26.04 源里只有 TensorRT 11（`libnvinfer11`），所以是 **CUDA 13 + TensorRT 11** 组合。
- `libnvinfer-bin` 提供 `trtexec`；`libopencv-dev` 元包补上 ModelPipeline 用的 `imgproc`/`calib3d` 与 `OpenCVConfig.cmake`。

### 模型获取（国内网络）

- `yolov8n-face.onnx`：deepghs/yolo-face 的**纯检测版**（输出 `[batch,5,8400]` = 框+置信度，无关键点），走 HuggingFace 国内镜像 `hf-mirror.com`。
- `pfld.onnx`：elliottzheng/fast-alignment 的 `PFLD.onnx`（输入 112×112，输出 136 = 68 点）。
- 68 点关键点交给 PFLD，检测器只需给人脸框，故把 [ModelPipeline.cpp](src/pipeline/model/ModelPipeline.cpp) 的 `feat < 15` 放宽为 `feat < 5`（关键点解析加 `feat >= 15` 保护）。

### 转 engine（shape 必须钉死）

[build_engines.sh](scripts/build_engines.sh)：两个模型都是动态 shape，TrTBackend 按固定 shape 加载，须 `min=opt=max` 钉死 `1x3x640x640` / `1x3x112x112`，否则 engine 带 -1 维度，运行期 cudaMalloc 失败。

### 问题 6：TensorRT 11 移除了 trtexec 的 --fp16

**现象**：`trtexec --fp16` 报 `Unknown option: --fp16`。

**原因**：TensorRT 11 移除了所有精度开关（`--fp16`/`--int8`/`--fp8` 及 `BuilderFlag::kFP16` 等），混合精度改用 ModelOpt AutoCast 离线处理。

**解决**：两个模型都很小，直接 **FP32** 构建（5070 上 yolov8n-face 约 7.5k qps，实时绰绰有余）。

### 问题 7：TrTBackend 首次编译失败（两个潜伏 bug）

**现象**：`no match for call to (std::vector<...>)()` 与 `invalid application of 'sizeof' to incomplete type`。

**原因**：TensorRT 后端此前从未真正编译过，暴露两个 bug——
1. [TrTBackend.cpp](src/inference/TrTBackend.cpp) `run()` 形参 `outputShapes` 与成员函数 `outputShapes()` 同名遮蔽，`outputShapes = outputShapes();` 非法；形参改名 `outShapes` 解决。
2. pimpl/不完整类型：[TrTBackend.h](src/inference/TrTBackend.h) 的 `unique_ptr<Impl>` 与 [ModelPipeline.h](src/pipeline/model/ModelPipeline.h) 的 `unique_ptr<IInferenceBackend>` 须把析构器声明在头文件、定义在 .cpp，否则隐式内联析构器让引用方编译失败。

**解决**：两处析构器移出到 .cpp（`= default`）。

### 验证结果（已实测通过）

- `--backend tensorrt --device cuda` 启动：`[main] backend=tensorrt`，engine 加载成功，未回退 mock。
- 对 lena.jpg 回传：`box=[0.41,0.37,0.28,0.39] ear=0.48 mar=0.017 state=awake` —— 人脸框、EAR/MAR、状态均正常。
- ✅ **head-pose 已标定**：此前正脸测得 `yaw=-35° pitch=-176°`（应为 0）的根因是 3D 模型朝向与 OpenCV 相反，导致正脸绕 X 翻 180°、把「分心」判定整成误报。已修复，见问题 9。

### 问题 8：「--backend tensorrt 却回退成 mock」——多数是跑错了二进制

**现象**：明明传了 `--backend tensorrt`，日志却是 `[main] backend=mock`。

**原因**：TensorRT 后端只编译进 `linux-tensorrt` preset。若误跑 **mock 构建**（`build/linux-default/fatigue_server`，未开 `FATIGUE_ENABLE_TENSORRT`），`makePipeline` 会**静默**回退到 mock，之前没有任何提示。

**解决**：
1. 用正确的二进制：`./build/linux-tensorrt/fatigue_server --backend tensorrt --device cuda ...`。
2. [Pipeline.cpp](src/pipeline/base/Pipeline.cpp) 已加明确诊断：跑错二进制 / 请求未编译的后端 / engine 真正加载失败，三种情况都会打印原因（`[pipeline] this binary was built without ...` / `... failed to initialize`），不再静默回退。

> 默认 `--model-dir` 已改为「相对可执行文件位置」解析（[main.cpp](src/main.cpp) 的 `defaultModelDir()`），从 `server/` 或 `build/<preset>/` 启动都能找到 `server/models`；显式传 `--model-dir` 时仍按原样（相对 CWD）解析。`--save-dir` 落盘前也会转成绝对路径，日志里打印的是真实位置。

### 问题 9：正脸被误判「分心」（pitch 恒为 ±180°）

**现象**：模型跑通后，只要画面里有脸就一直报 `distracted`；实测正脸 `pitch≈-177°`（应为 0）。

**原因**：[ModelPipeline.cpp](src/pipeline/model/ModelPipeline.cpp) 里 `pose3D68`/`pose3D5` 沿用 dlib 的 3D 模型朝向——**Y 向上、Z 背离相机**，而 OpenCV 图像/solvePnP 约定是 **Y 向下、Z 朝相机**。两者相反，等价于模型绕 X 轴翻 180°，于是正脸的旋转矩阵 `R[1][1]≈-1、R[2][2]≈-1`，`pitch=atan2(R[2][1],R[2][2])≈±180°`；[StateDecider](src/pipeline/base/StateDecider.cpp) 里 `|pitch|>20°` 一票判「分心」。

**解决**：把两个模型的 **Y、Z 同时取反**（X 不变），对齐 OpenCV 约定。修正后正脸 `pitch≈0`、`roll≈0`，`yaw` 反映真实转头角度。

**验证**：`yaw≈-23°/pitch≈-9°` 的帧 → `awake`；`yaw≈36°` 的真实转头帧 → `distracted`。判定不再被 pitch 的 180° 翻转污染。
