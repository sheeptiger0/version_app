// ============================================================
//  fatigue_server 程序入口（编排层）
//  职责：解析参数 -> 组装各模块 -> 定义「每收到一帧」的回调
//  数据流：手机 UDP 分片 -> 收包重组 -> 解码显示 + 推理 -> 决策
//          -> (可选)手动覆盖 -> JSON 回传手机
// ============================================================

#include <asio.hpp>                                    // 网络库（standalone Asio）

#include <atomic>                                      // std::atomic 无锁标志
#include <condition_variable>                          // 条件变量
#include <csignal>                                     // 信号处理
#include <cstdint>
#include <iostream>
#include <mutex>                                       // 互斥锁
#include <string>
#include <vector>

#include "net/UdpServer.h"                             // UDP 收发包 + 分片重组
#include "pipeline/Pipeline.h"                         // 可插拔推理管线接口 + 工厂
#include "util/Json.h"                                 // nlohmann/json 别名

#ifdef _WIN32                       //条件编译
#include <conio.h>                  //Windows 特有的控制台输入输出头文件
#include <io.h>                     //Windows 上提供底层 I/O 操作的头文件
#include "video/Jpeg.h"             // WIC 解码 JPEG -> BGRA
#include "video/VideoViewer.h"      // Win32 GDI 视频窗口
#endif

namespace {

std::atomic<bool> g_running{true};             //控制程序运行的总开关
std::mutex g_cvMtx;                            //互斥锁，用于保护条件变量的访问
std::condition_variable g_cv;                  //条件变量，用于线程间的同步，等待或通知某个条件的发生

// 收到退出信号后的回调：把运行开关置 false，唤醒主线程退出
void onSignal(int) {
  g_running = false;                           //收到信号后，将运行标志设置为 false，表示程序应停止运行
  g_cv.notify_all();                           //通知所有等待在条件变量上的线程，条件已经发生变化
}

} // namespace

int main(int argc, char** argv) {
  // ---- 1. 默认配置 ----
  uint16_t port = 9000;                         // UDP 监听端口
  bool noView = false;                          // 是否不弹视频窗口（无桌面环境用）
  fatigue::PipelineOptions opts;                // 管线选项（backend / device / modelDir）

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
    } else if (a == "--help" || a == "-h") {
      std::cout << "usage: fatigue_server [--port 9000] [--backend mock|onnx|tensorrt] "
                   "[--device cpu|cuda] [--model-dir models] [--no-view]\n";
      return 0;
    }
  }

  std::signal(SIGINT, onSignal);           //Ctrl + C 触发回调
  std::signal(SIGTERM, onSignal);          //kill <进程PID> 触发回调

  // 关闭 stdout/stderr 缓冲，保证日志实时可见（尤其重定向到文件/管道时）
  std::cout << std::unitbuf;
  std::cerr << std::unitbuf;

  // ---- 3. 初始化网络 + 推理管线 ----
  asio::io_context io;
  fatigue::UdpServer server(io, port);         // 构造时即 open + bind UDP socket
  if (!server.bound()) {
    std::cerr << "[main] failed to bind UDP port " << port << "\n";
    return 1;                                  // 端口被占用等绑定失败，直接退出
  }

  // 工厂按 --backend 选择后端；onnx 加载失败会自动回退到 mock
  auto pipeline = fatigue::makePipeline(opts);
  std::cout << "[main] backend=" << pipeline->backendName()
            << " listening on UDP " << port << "\n";

#ifdef _WIN32
  // ---- 4. Windows 手动控制：视频窗口 + 控制台双通道按键 ----

  fatigue::VideoViewer viewer;

  // 手动状态：manual=true 表示人工接管结果，manualState 是人工指定的状态。
  // 会被「帧回调 / 窗口按键 / 控制台按键」三处并发访问，统一用 manualMtx 保护。
  std::atomic<bool> manual{false};
  std::mutex manualMtx;
  std::string manualState = "awake";

  // 统一的按键处理：两个输入通道都调它
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
      std::cout << "[manual] result -> " << manualState << "\n";
    } else {
      std::cout << "[manual] back to automatic (" << pipeline->backendName()
                << ")\n";
    }
  };

  // 通道 1：视频窗口按键（点一下窗口后按 1/2/3/0/M）
  if (!noView) {
    const bool ok = viewer.start(
        "FatigueGuard live view (keys: 1=awake 2=distracted 3=drowsy 0=none M=auto)",
        640, 480, [&](int vk) { applyManualKey(vk); });
    if (ok) {
      std::cout << "[view] live window opened\n";
    } else {
      std::cerr << "[view] could not open live window (continuing headless)\n";
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
    std::cout << "[manual] console keys: 1=awake 2=distracted 3=drowsy 0=none M=auto\n";
  }
#endif

  // ---- 5. 启动接收，注册「每收到一帧」的回调（核心业务逻辑）----
  // 注意：该回调运行在 UdpServer 的接收线程上，不是主线程。
  server.start([&](const std::vector<uint8_t>& jpeg,
                   const asio::ip::udp::endpoint& from) {
#ifdef _WIN32
    // ① 解码并显示到视频窗口
    if (viewing) {
      int w = 0, h = 0;
      std::vector<uint8_t> bgra;
      if (fatigue::decodeJpegToBgra(jpeg.data(), jpeg.size(), w, h, bgra)) {
        viewer.showFrame(bgra.data(), w, h);
        static bool logged = false;            // 只打印一次首帧尺寸
        if (!logged) {
          std::cout << "[view] first frame decoded: " << w << "x" << h << "\n";
          logged = true;
        }
      }
    }
#endif

    // ② 走推理管线（mock 或 onnx），得到状态 + 特征
    auto r = pipeline->process(jpeg.data(), jpeg.size());

#ifdef _WIN32
    // ③ 手动模式覆盖：若人工接管，用人工指定的状态替换推理结果
    {
      std::lock_guard<std::mutex> lk(manualMtx);
      if (manual.load()) r.state = manualState;
    }
#endif

    // ④ 打包 JSON 回传手机
    fatigue::json j;
    j["state"] = r.state;
    j["ear"] = r.features.ear;
    j["mar"] = r.features.mar;
    j["yaw"] = r.features.yaw;
    j["pitch"] = r.features.pitch;
    j["fps"] = r.fps;
    j["box"] = std::vector<float>{r.features.bx, r.features.by, r.features.bw, r.features.bh};

    server.sendTo(from, fatigue::packMessage(fatigue::MsgType::ServerState,
                                             r.frameId, j.dump()));

    // ⑤ 每 30 帧打一次状态日志，避免刷屏
    static size_t logTick = 0;
    if (++logTick % 30 == 0) {
      std::cout << "[main] state=" << r.state << " ear=" << r.features.ear
                << " yaw=" << r.features.yaw << " fps=" << r.fps << "\n";
    }
  });

  // ---- 6. 主线程睡到收到退出信号 ----
  std::unique_lock<std::mutex> lk(g_cvMtx);
  g_cv.wait(lk, [] { return !g_running.load(); });

  // ---- 7. 清理：停接收线程、关视频窗口 ----
  server.stop();
#ifdef _WIN32
  viewer.stop();
#endif
  std::cout << "[main] shutting down\n";
  return 0;
}
