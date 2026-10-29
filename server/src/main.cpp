// ============================================================
//  fatigue_server 程序入口（编排层）
//  职责：解析参数 -> 组装各模块 -> 定义「每收到一帧」的回调
//  数据流：手机 UDP 分片 -> 收包重组 -> 解码显示 + 推理 -> 决策
//          -> (可选)手动覆盖 -> JSON 回传手机
// ============================================================

#include <asio.hpp>                                    // 网络库（standalone Asio）

#include <atomic>                                      // std::atomic 无锁标志
#include <chrono>                                      // std::chrono::milliseconds
#include <condition_variable>                          // 条件变量
#include <csignal>                                     // 信号处理
#include <cstdint>
#include <iostream>
#include <mutex>                                       // 互斥锁
#include <string>
#include <thread>                                      // std::thread / sleep_for
#include <vector>

#include <filesystem>                                  // 抽帧保存：创建目录
#include <fstream>                                     // 抽帧保存：写 JPEG
#include <iomanip>                                     // 抽帧保存：文件名补零
#include <sstream>                                     // 抽帧保存：拼文件名

#include "net/UdpServer.h"                             // UDP 收发包 + 分片重组
#include "pipeline/base/Pipeline.h"                         // 可插拔推理管线接口 + 工厂
#include "util/Json.h"                                 // nlohmann/json 别名
#include "util/Log.h"                                  // 统一日志

#ifdef _WIN32                       //条件编译
#include <conio.h>                  //Windows 特有的控制台输入输出头文件
#include <io.h>                     //Windows 上提供底层 I/O 操作的头文件
#include "video/Jpeg.h"             // WIC 解码 JPEG -> BGRA
#include "video/VideoViewer.h"      // Win32 GDI 视频窗口
#else                               // Linux
#include <termios.h>                // 终端 raw 模式
#include <unistd.h>                 // read / isatty / STDIN_FILENO
#ifdef FATIGUE_ENABLE_VIEWER
#include "video/OpenCvViewer.h"     // OpenCV 解码 + imshow 实时预览
#endif
#endif

namespace {

std::atomic<bool> g_running{true};             //控制程序运行的总开关
std::mutex g_cvMtx;                            //互斥锁，用于保护条件变量的访问
std::condition_variable g_cv;                  //条件变量，用于线程间的同步，等待或通知某个条件的发生

// 抽帧保存：环形缓冲区容量——保留最近这么多张，满了从 0 号覆盖。
constexpr int kMaxSavedFrames = 200;

// steady_clock 毫秒时间戳，用于「无帧心跳」判断。
int64_t steadyNowMs() {
  return std::chrono::duration_cast<std::chrono::milliseconds>(
             std::chrono::steady_clock::now().time_since_epoch()).count();
}

// 默认模型目录：相对可执行文件所在的项目根（server/）解析，而不是相对 CWD，
// 这样无论从 server/ 还是 build/<preset>/ 启动，都能找到 server/models。
std::string defaultModelDir() {
#if defined(_WIN32)
  return "models";   // Windows 保持原行为（CWD 相对）
#else
  char buf[4096];
  const ssize_t n = ::readlink("/proc/self/exe", buf, sizeof(buf) - 1);
  if (n <= 0) return "models";   // 读不到就退回 CWD 相对
  buf[n] = '\0';
  std::filesystem::path exe(buf);                     // <server>/build/<preset>/fatigue_server
  const std::filesystem::path root =
      exe.parent_path().parent_path().parent_path();  // 上跳三级：preset -> build -> server
  return (root / "models").string();
#endif
}

// 收到退出信号后的回调：把运行开关置 false，唤醒主线程退出
void onSignal(int) {
  g_running = false;                           //收到信号后，将运行标志设置为 false，表示程序应停止运行
  g_cv.notify_all();                           //通知所有等待在条件变量上的线程，条件已经发生变化
}

#if !defined(_WIN32)
// ---- Linux 终端按键：stdin 设成 raw + 非阻塞，轮询读单字节 ----
static struct termios g_origTermios;
static bool g_stdinRaw = false;

static bool enableRawStdin() {
  if (tcgetattr(STDIN_FILENO, &g_origTermios) != 0) return false;
  struct termios t = g_origTermios;
  t.c_lflag &= ~(ICANON | ECHO);             // 关行缓冲、关回显
  t.c_cc[VMIN] = 0;                          // 无输入时不阻塞
  t.c_cc[VTIME] = 0;
  if (tcsetattr(STDIN_FILENO, TCSANOW, &t) != 0) return false;
  g_stdinRaw = true;
  return true;
}

static int linuxGetchNonBlocking() {
  unsigned char c = 0;
  const ssize_t n = read(STDIN_FILENO, &c, 1);
  return (n == 1) ? static_cast<int>(c) : -1;
}

static void restoreStdin() {
  if (g_stdinRaw) tcsetattr(STDIN_FILENO, TCSANOW, &g_origTermios);
}
#endif

} // namespace

int main(int argc, char** argv) {
  // ---- 1. 默认配置 ----
  uint16_t port = 9000;                         // UDP 监听端口
  bool noView = false;                          // 是否不弹视频窗口（无桌面环境用）
  std::string saveDir;                          // 抽帧保存目录（空 = 不保存）
  int saveInterval = 5;                         // 抽帧保存间隔：每 N 帧存一张
  DMSAlgo::PipelineOptions opts;                // 管线选项（backend / device / modelDir）
  opts.modelDir = defaultModelDir();            // 默认模型目录相对可执行文件定位（而非 CWD）

  // ---- 2. 解析命令行参数 ----
  for (int i = 1; i < argc; ++i) {
    const std::string a = argv[i];
    if (a == "--port" && i + 1 < argc) {
      port = static_cast<uint16_t>(std::stoi(argv[++i]));
    } else if (a == "--backend" && i + 1 < argc) {
      opts.backend = argv[++i];                 // mock | onnx | tensorrt
    } else if (a == "--device" && i + 1 < argc) {
      opts.device = argv[++i];                  // cpu | cuda
    } else if (a == "--model-dir" && i + 1 < argc) {
      opts.modelDir = argv[++i];                // onnx 模型目录
    } else if (a == "--no-view") {
      noView = true;                            // 无桌面/服务器环境，不弹窗口
    } else if (a == "--save-dir" && i + 1 < argc) {
      saveDir = argv[++i];                      // 抽帧保存目录
    } else if (a == "--save-interval" && i + 1 < argc) {
      saveInterval = std::stoi(argv[++i]);      // 每 N 帧存一张
      if (saveInterval < 1) saveInterval = 1;   // 至少每帧
    } else if (a == "--help" || a == "-h") {
      std::cout << "usage: fatigue_server [--port 9000] [--backend mock|onnx|tensorrt] "
                   "[--device cpu|cuda] [--model-dir models] [--no-view] "
                   "[--save-dir frames] [--save-interval 5]\n";
      return 0;
    }
  }

  std::signal(SIGINT, onSignal);           //Ctrl + C 触发回调
  std::signal(SIGTERM, onSignal);          //kill <进程PID> 触发回调

  // ---- 3. 初始化网络 + 推理管线 ----
  asio::io_context io;
  DMSAlgo::UdpServer server(io, port);         // 构造时即 open + bind UDP socket
  if (!server.bound()) {
    LOG_ERROR("main") << "failed to bind UDP port " << port;
    return 1;                                  // 端口被占用等绑定失败，直接退出
  }

  // 工厂按 --backend 选择后端；onnx 加载失败会自动回退到 mock
  auto pipeline = DMSAlgo::makePipeline(opts);
  LOG_INFO("main") << "backend=" << pipeline->backendName()
                   << " listening on UDP " << port;

  // ---- 3.5 抽帧保存：--save-dir 指定目录时启用 ----
  // 每 saveInterval 帧存一张（保证画面多样）；文件名是 0..199 环形槽位，
  // 存满 kMaxSavedFrames 张后回到 0 号覆盖，始终保留最近 kMaxSavedFrames 张。
  std::atomic<int> receivedCount{0};   // 收到的总帧数（0 起）
  std::atomic<int> savedCount{0};      // 已保存帧数（用于环形槽位计算）
  std::atomic<int64_t> lastFrameMs{steadyNowMs()};  // 最近一帧到达时间（无帧心跳用）
  if (!saveDir.empty()) {
    std::error_code ec;
    saveDir = std::filesystem::absolute(saveDir, ec).string();   // 转绝对路径，日志与落盘位置一致清晰
    std::filesystem::create_directories(saveDir, ec);
    LOG_INFO("save") << "saving every " << saveInterval << " frames to '" << saveDir
                     << "' (ring of " << kMaxSavedFrames << ")";
  }

  // ---- 4. 手动控制：视频窗口（按平台）+ 控制台按键 ----
  // 手动状态：manual=true 表示人工接管结果，manualState 是人工指定的状态。
  // 会被「帧回调 / 按键线程」并发访问，统一用 manualMtx 保护。
  std::atomic<bool> manual{false};
  std::mutex manualMtx;
  std::string manualState = "awake";

  // 统一的按键处理：各平台的按键通道都调它
  const auto applyManualKey = [&](int vk) {
    std::lock_guard<std::mutex> lk(manualMtx);
    switch (vk) {
      case '1': manualState = "awake"; manual = true; break;      // 清醒
      case '2': manualState = "distracted"; manual = true; break; // 分心
      case '3': manualState = "drowsy"; manual = true; break;     // 疲劳
      case '0': manualState = "none"; manual = true; break;       // 无
      case 'M': case 'm': manual = false; break;                  // 回到自动
      default: return;                                            // 其它键忽略
    }
    if (manual.load()) {
      LOG_INFO("manual") << "result -> " << manualState;
    } else {
      LOG_INFO("manual") << "back to automatic (" << pipeline->backendName() << ")";
    }
  };

#ifdef _WIN32
  // ---- Windows：Win32 GDI 视频窗口 + 控制台按键 ----
  DMSAlgo::VideoViewer viewer;

  // 通道 1：视频窗口按键（点一下窗口后按 1/2/3/0/M）
  if (!noView) {
    const bool ok = viewer.start(
        "FatigueGuard live view (keys: 1=awake 2=distracted 3=drowsy 0=none M=auto)",
        640, 480, [&](int vk) { applyManualKey(vk); });
    if (ok) {
      LOG_INFO("view") << "live window opened";
    } else {
      LOG_WARN("view") << "could not open live window (continuing headless)";
    }
  }
  const bool viewing = !noView && viewer.started();

  // 通道 2：控制台按键。只有 stdin 是终端时才开这个线程，
  // 避免在重定向 stdin 的场景下 _getch() 卡死或读到管道数据。
  if (_isatty(_fileno(stdin))) {
    std::thread([&] {
      while (g_running.load()) {
        const int ch = _getch();               // 阻塞读一个键，无回显
        if (ch == EOF) break;
        applyManualKey(ch);
      }
    }).detach();                               // 分离线程：随进程结束回收
    LOG_INFO("manual") << "console keys: 1=awake 2=distracted 3=drowsy 0=none M=auto";
  }
#else
  // ---- Linux：OpenCV 实时预览（若编译时检测到 OpenCV）+ 终端按键 ----
#ifdef FATIGUE_ENABLE_VIEWER
  DMSAlgo::OpenCvViewer viewer;
  const bool viewing = !noView && viewer.start(
      "FatigueGuard live view (keys: 1=awake 2=distracted 3=drowsy 0=none M=auto)");
  if (viewing) {
    LOG_INFO("view") << "live window enabled (OpenCV)";
  } else if (!noView) {
    LOG_WARN("view") << "could not enable live window (continuing headless)";
  }
#else
  const bool viewing = false;
#endif

  // 终端按键线程（raw 模式）。只有 stdin 是 TTY 时才开，避免读到重定向的数据。
  if (isatty(STDIN_FILENO) && enableRawStdin()) {
    std::thread([&] {
      while (g_running.load()) {
        const int ch = linuxGetchNonBlocking();
        if (ch >= 0) applyManualKey(ch);
        std::this_thread::sleep_for(std::chrono::milliseconds(30));
      }
    }).detach();
    LOG_INFO("manual") << "console keys: 1=awake 2=distracted 3=drowsy 0=none M=auto";
  }
#endif

  // ---- 5. 启动接收，注册「每收到一帧」的回调（核心业务逻辑）----
  // 注意：该回调运行在 UdpServer 的接收线程上，不是主线程。
  server.start([&](const std::vector<uint8_t>& jpeg,
                   const asio::ip::udp::endpoint& from) {
    lastFrameMs.store(steadyNowMs());   // 记录收帧时间，供无帧心跳判断

    // 抽帧保存（可选）：每 saveInterval 帧存一张；文件名按 0..199 环形覆盖。
    if (!saveDir.empty()) {
      const int seq = receivedCount.fetch_add(1);
      if (seq % saveInterval == 0) {
        const int slot = savedCount.fetch_add(1) % kMaxSavedFrames;
        std::ostringstream name;
        name << saveDir << "/frame_" << std::setw(6) << std::setfill('0') << slot << ".jpg";
        std::ofstream ofs(name.str(), std::ios::binary);
        if (ofs) {
          ofs.write(reinterpret_cast<const char*>(jpeg.data()),
                    static_cast<std::streamsize>(jpeg.size()));
        }
      }
    }

#ifdef _WIN32
    // ① 解码并显示到视频窗口
    if (viewing) {
      int w = 0, h = 0;
      std::vector<uint8_t> bgra;
      if (DMSAlgo::decodeJpegToBgra(jpeg.data(), jpeg.size(), w, h, bgra)) {
        viewer.showFrame(bgra.data(), w, h);
        static bool logged = false;            // 只打印一次首帧尺寸
        if (!logged) {
          LOG_INFO("view") << "first frame decoded: " << w << "x" << h;
          logged = true;
        }
      }
    }
#elif defined(FATIGUE_ENABLE_VIEWER)
    // ① OpenCV 解码并缓存到预览窗口（主线程统一刷新）
    if (viewing) viewer.showFrame(jpeg);
#endif

    // ② 走推理管线（mock 或 onnx），得到状态 + 特征
    auto r = pipeline->process(jpeg.data(), jpeg.size());

    // ③ 手动模式覆盖：若人工接管，用人工指定的状态替换推理结果
    {
      std::lock_guard<std::mutex> lk(manualMtx);
      if (manual.load()) r.state = manualState;
    }

    // ④ 打包 JSON 回传手机
    DMSAlgo::json j;
    j["state"] = r.state;
    j["ear"] = r.features.ear;
    j["mar"] = r.features.mar;
    j["yaw"] = r.features.yaw;
    j["pitch"] = r.features.pitch;
    j["fps"] = r.fps;
    j["box"] = std::vector<float>{r.features.bx, r.features.by, r.features.bw, r.features.bh};

    server.sendTo(from, DMSAlgo::packMessage(DMSAlgo::MsgType::ServerState,
                                             r.frameId, j.dump()));

    // ⑤ 每 30 帧打一次状态日志，避免刷屏
    static size_t logTick = 0;
    if (++logTick % 30 == 0) {
      LOG_INFO("main") << "state=" << r.state << " ear=" << r.features.ear
                       << " yaw=" << r.features.yaw << " fps=" << r.fps;
    }
  });

  // ---- 6. 主线程睡到收到退出信号 ----
  // 注意：不能只用 g_cv.wait() 无限阻塞。Linux 下进程级信号（SIGINT/SIGTERM）
  // 会优先投递到主线程本身，而主线程此时正阻塞在条件变量上——信号处理函数里的
  // notify_all() 无法唤醒「自己正在等待」的那把条件变量（lost-wakeup），
  // 导致 Ctrl+C / kill 无法退出。这里改用带超时的 wait_for 轮询退出标志，
  // 保证最迟 100ms 内响应退出信号（onSignal 里的 notify 仍保留，用于 Windows
  // 及其它信号投递到别的线程时的即时唤醒）。
  std::unique_lock<std::mutex> lk(g_cvMtx);
  int64_t lastHeartbeatMs = steadyNowMs();
  while (g_running.load()) {
    g_cv.wait_for(lk, std::chrono::milliseconds(100));
#if !defined(_WIN32) && defined(FATIGUE_ENABLE_VIEWER)
    // 顺便刷新预览窗口（imshow + waitKey 必须在同一 GUI 线程上）
    if (viewing) viewer.pump();
#endif
    // 无帧心跳：超过 5 秒没收到帧就告警，便于区分「手机断流」还是「服务端卡死」。
    const int64_t nowMs = steadyNowMs();
    const int64_t gap = nowMs - lastFrameMs.load();
    if (gap > 5000 && nowMs - lastHeartbeatMs > 5000) {
      LOG_WARN("main") << "no frames for " << (gap / 1000) << "s";
      lastHeartbeatMs = nowMs;
    }
  }

  // ---- 7. 清理：停接收线程、关视频窗口、恢复终端 ----
  server.stop();
#ifdef _WIN32
  viewer.stop();
#elif defined(FATIGUE_ENABLE_VIEWER)
  if (viewing) viewer.stop();
#endif
#if !defined(_WIN32)
  restoreStdin();
#endif
  LOG_INFO("main") << "shutting down";
  return 0;
}
