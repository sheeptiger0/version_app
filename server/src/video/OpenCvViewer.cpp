#include "video/OpenCvViewer.h"

#include <opencv2/highgui.hpp>    // imshow / waitKey / destroyAllWindows
#include <opencv2/imgcodecs.hpp>  // imdecode

namespace fatigue {

bool OpenCvViewer::start(const std::string& title) {
  title_ = title;
  started_ = true;
  return true;
}

void OpenCvViewer::showFrame(const std::vector<uint8_t>& jpeg) {
  if (jpeg.empty()) return;
  cv::Mat m = cv::imdecode(jpeg, cv::IMREAD_COLOR);
  if (m.empty()) return;
  std::lock_guard<std::mutex> lk(mtx_);
  latest_ = m;
  hasFrame_ = true;
}

void OpenCvViewer::pump() {
  cv::Mat m;
  {
    std::lock_guard<std::mutex> lk(mtx_);
    if (!hasFrame_) {
      // 尚无帧时也刷一下窗口事件，保证窗口能正常创建/响应关闭。
      cv::waitKey(1);
      return;
    }
    m = latest_.clone();  // 拷贝出来，避免持有锁期间做耗时操作
  }
  cv::imshow(title_, m);
  cv::waitKey(1);
}

void OpenCvViewer::stop() {
  if (started_) cv::destroyAllWindows();
  started_ = false;
  hasFrame_ = false;
}

} // namespace fatigue
