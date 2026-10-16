#include <asio.hpp>

#include <atomic>
#include <condition_variable>
#include <csignal>
#include <cstdint>
#include <iostream>
#include <mutex>
#include <string>
#include <vector>

#include "net/UdpServer.h"
#include "pipeline/Pipeline.h"
#include "util/Json.h"

namespace {

std::atomic<bool> g_running{true};
std::mutex g_cvMtx;
std::condition_variable g_cv;

void onSignal(int) {
  g_running = false;
  g_cv.notify_all();
}

} // namespace

int main(int argc, char** argv) {
  uint16_t port = 9000;
  fatigue::PipelineOptions opts;

  for (int i = 1; i < argc; ++i) {
    const std::string a = argv[i];
    if (a == "--port" && i + 1 < argc) {
      port = static_cast<uint16_t>(std::stoi(argv[++i]));
    } else if (a == "--backend" && i + 1 < argc) {
      opts.backend = argv[++i];
    } else if (a == "--device" && i + 1 < argc) {
      opts.device = argv[++i];
    } else if (a == "--model-dir" && i + 1 < argc) {
      opts.modelDir = argv[++i];
    } else if (a == "--help" || a == "-h") {
      std::cout << "usage: fatigue_server [--port 9000] [--backend mock|onnx] "
                   "[--device cpu|cuda] [--model-dir models]\n";
      return 0;
    }
  }

  std::signal(SIGINT, onSignal);
  std::signal(SIGTERM, onSignal);

  // Flush log lines even when stdout is redirected to a file/pipe.
  std::cout << std::unitbuf;
  std::cerr << std::unitbuf;

  asio::io_context io;
  fatigue::UdpServer server(io, port);
  if (!server.bound()) {
    std::cerr << "[main] failed to bind UDP port " << port << "\n";
    return 1;
  }

  auto pipeline = fatigue::makePipeline(opts);
  std::cout << "[main] backend=" << pipeline->backendName()
            << " listening on UDP " << port << "\n";

  server.start([&](const std::vector<uint8_t>& jpeg, const asio::ip::udp::endpoint& from) {
    const auto r = pipeline->process(jpeg.data(), jpeg.size());

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

    static size_t logTick = 0;
    if (++logTick % 30 == 0) {
      std::cout << "[main] state=" << r.state << " ear=" << r.features.ear
                << " yaw=" << r.features.yaw << " fps=" << r.fps << "\n";
    }
  });

  std::unique_lock<std::mutex> lk(g_cvMtx);
  g_cv.wait(lk, [] { return !g_running.load(); });

  server.stop();
  std::cout << "[main] shutting down\n";
  return 0;
}
