#pragma once

#include <cstdint>
#include <functional>
#include <memory>
#include <string>

namespace fatigue {

// Minimal Windows-only live video window (GDI). Frames are supplied as
// tightly-packed, top-down 32bpp BGRA (see video/Jpeg.h). WM_KEYDOWN virtual-key
// codes are forwarded to the key handler so a human operator can manually choose
// what result gets sent back to the phone. Compiled only on Windows.
class VideoViewer {
 public:
  using KeyHandler = std::function<void(int vk)>;

  VideoViewer();
  ~VideoViewer();
  VideoViewer(const VideoViewer&) = delete;
  VideoViewer& operator=(const VideoViewer&) = delete;

  bool start(const std::string& title, int width, int height, KeyHandler onKey);
  void showFrame(const uint8_t* bgra, int width, int height);
  void stop();
  bool started() const;

  // Opaque implementation (defined in VideoViewer.cpp). Public only so the
  // .cpp's window-proc helpers can name the type; its members stay hidden.
  struct Impl;

 private:
  std::unique_ptr<Impl> impl_;
};

} // namespace fatigue
