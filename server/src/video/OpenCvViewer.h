#pragma once

// Linux 版实时预览：OpenCV 解码 JPEG + imshow 显示。
// 与 Windows 的 VideoViewer（Win32 GDI + WIC）对应，仅在有 FATIGUE_ENABLE_VIEWER
// 宏时参与编译（见 CMakeLists.txt 的 OpenCV 自动探测）。

#include <cstdint>
#include <mutex>
#include <string>
#include <vector>

#include <opencv2/core.hpp>   // cv::Mat

namespace DMSAlgo {

class OpenCvViewer {
 public:
  // 初始化窗口标题。返回 true 表示可用（OpenCV highgui 已就绪）。
  bool start(const std::string& title);

  // 由接收线程调用：解码 JPEG 并缓存最新一帧（线程安全）。
  void showFrame(const std::vector<uint8_t>& jpeg);

  // 由主线程（GUI 线程）周期性调用：把缓存的最新帧显示出来并刷新窗口事件。
  void pump();

  // 关闭窗口。
  void stop();

 private:
  std::mutex mtx_;
  std::string title_;
  bool started_ = false;
  bool hasFrame_ = false;
  cv::Mat latest_;
};

} // namespace DMSAlgo
