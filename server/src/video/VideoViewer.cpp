// Windows 实时视频窗口（GDI 实现）
//
// 职责：把 JPEG 解码出来的 BGRA 画面，用 Win32 窗口实时显示出来；
//       同时把窗口上的按键(WM_KEYDOWN)转成回调，供上层做「手动结果」控制。
//
// 线程模型：start() 会新开一个线程跑 Windows 消息循环；showFrame() 从
//          别的线程(网络接收线程)推画面过来。两者用 impl_->mtx 保护共享的
//          帧缓冲，并通过自定义消息 kRefreshMsg 通知窗口线程重绘。

#include "video/VideoViewer.h"

#include <windows.h>

#include <atomic>
#include <mutex>
#include <thread>
#include <vector>

namespace fatigue {

// 隐藏实现（PIMPL）：把 Win32 的 HWND 等细节藏在这里，头文件不暴露 windows.h
struct VideoViewer::Impl {
  HWND hwnd = nullptr;             // 窗口句柄（由窗口线程创建/销毁）
  std::atomic<bool> ready{false};  // 窗口是否已创建成功（供 start() 返回用）

  int winW = 640;                  // 窗口初始宽度
  int winH = 480;                  // 窗口初始高度
  KeyHandler onKey;                // 按键回调（WM_KEYDOWN -> 上层手动控制）

  // 以下由 showFrame()(接收线程) 写、paint()(窗口线程) 读，用 mtx 保护
  std::mutex mtx;
  std::vector<uint8_t> frame;      // 最近一帧 BGRA 数据
  int frameW = 0;
  int frameH = 0;
  bool hasFrame = false;           // 是否已有可显示的帧

  std::thread thread;              // 窗口线程（跑消息循环）
  std::atomic<bool> running{false};// 窗口线程是否继续跑
};

namespace {

constexpr UINT kRefreshMsg = WM_APP + 1;  // 自定义消息：有新帧，通知窗口重绘

// WM_PAINT 处理：把 impl->frame 里的 BGRA 帧贴到窗口上
void paint(HWND hwnd, VideoViewer::Impl* impl) {
  PAINTSTRUCT ps;
  HDC dc = BeginPaint(hwnd, &ps);
  RECT rc;
  GetClientRect(hwnd, &rc);        // 客户区尺寸
  const int cw = rc.right - rc.left;
  const int ch = rc.bottom - rc.top;

  FillRect(dc, &rc, static_cast<HBRUSH>(GetStockObject(BLACK_BRUSH)));  // 黑底

  std::lock_guard<std::mutex> lk(impl->mtx);
  if (impl->hasFrame && impl->frameW > 0 && impl->frameH > 0 &&
      !impl->frame.empty()) {
    BITMAPINFOHEADER bih = {};
    bih.biSize = sizeof(bih);
    bih.biWidth = impl->frameW;
    bih.biHeight = -impl->frameH;  // 负高度 => 自上而下的 DIB（内存首行=画面首行）
    bih.biPlanes = 1;
    bih.biBitCount = 32;           // 32bpp BGRA
    bih.biCompression = BI_RGB;

    SetStretchBltMode(dc, COLORONCOLOR);

    // 按帧的宽高比「等比」显示：算出一个居中、不拉伸的目标矩形，
    // 多余部分留黑边。否则竖屏帧(如 240x320)会被硬拉成横屏窗口(640x480)，人脸变形。
    int dstW = cw;
    int dstH = ch;
    const float frameAspect = static_cast<float>(impl->frameW) / impl->frameH;
    const float clientAspect = static_cast<float>(cw) / ch;
    if (frameAspect > clientAspect) {
      dstH = static_cast<int>(cw / frameAspect);   // 帧更「宽」：上下留黑边
      if (dstH < 1) dstH = 1;
    } else {
      dstW = static_cast<int>(ch * frameAspect);   // 帧更「高」：左右留黑边
      if (dstW < 1) dstW = 1;
    }
    const int dstX = (cw - dstW) / 2;
    const int dstY = (ch - dstH) / 2;

    StretchDIBits(dc, dstX, dstY, dstW, dstH,
                  0, 0, impl->frameW, impl->frameH,
                  impl->frame.data(),
                  reinterpret_cast<const BITMAPINFO*>(&bih),
                  DIB_RGB_COLORS, SRCCOPY);
  }
  EndPaint(hwnd, &ps);
}

// 窗口过程：处理各类窗口消息
LRESULT CALLBACK wndProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
  VideoViewer::Impl* impl =
      reinterpret_cast<VideoViewer::Impl*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));

  // WM_NCCREATE 是窗口创建时的第一个消息，在这里把 Impl 指针存进窗口的用户数据
  if (msg == WM_NCCREATE) {
    const auto* cs = reinterpret_cast<const CREATESTRUCT*>(lp);
    impl = static_cast<VideoViewer::Impl*>(cs->lpCreateParams);
    SetWindowLongPtrW(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(impl));
  }

  switch (msg) {
    case kRefreshMsg:                 // 有新帧 -> 触发重绘
      InvalidateRect(hwnd, nullptr, FALSE);
      return 0;
    case WM_PAINT:                    // 系统要求重绘 -> 贴帧
      if (impl) paint(hwnd, impl);
      return 0;
    case WM_KEYDOWN:                  // 按键 -> 交给上层（手动控制）
      if (impl && impl->onKey) impl->onKey(static_cast<int>(wp));
      return 0;
    case WM_DESTROY:                  // 窗口关闭 -> 发 WM_QUIT 结束消息循环
      PostQuitMessage(0);
      return 0;
    default:
      return DefWindowProcW(hwnd, msg, wp, lp);
  }
}

} // namespace

VideoViewer::VideoViewer() : impl_(std::make_unique<Impl>()) {}

VideoViewer::~VideoViewer() { stop(); }

// 创建窗口并在新线程里跑消息循环。
// 参数：
//   title        - 窗口标题
//   width/height - 窗口初始大小
//   onKey        - 按键回调（收到 WM_KEYDOWN 时调用，参数是虚拟键码 vk）
// 返回：窗口是否成功创建。
bool VideoViewer::start(const std::string& title, int width, int height,
                        KeyHandler onKey) {
  if (impl_->running) return false;      // 已经在跑，不允许重复 start
  impl_->winW = width > 0 ? width : 640;
  impl_->winH = height > 0 ? height : 480;
  impl_->onKey = std::move(onKey);
  impl_->ready = false;
  impl_->running = true;

  const std::wstring wtitle(title.begin(), title.end());  // 转宽字符给 Win32 API

  // 新开窗口线程：注册窗口类 -> 创建窗口 -> 显示 -> 跑消息循环 -> 收尾
  impl_->thread = std::thread([impl = impl_.get(), wtitle] {
    const HINSTANCE hInst = GetModuleHandleW(nullptr);

    // 1) 注册窗口类（只需一次；重复注册返回 ERROR_CLASS_ALREADY_EXISTS 也 OK）
    WNDCLASSW wc = {};
    wc.lpfnWndProc = wndProc;                          // 指定窗口过程
    wc.hInstance = hInst;
    wc.hCursor = LoadCursor(nullptr, IDC_ARROW);       // 箭头光标
    wc.hbrBackground = static_cast<HBRUSH>(GetStockObject(BLACK_BRUSH)); // 黑背景
    wc.lpszClassName = L"FatigueGuardVideo";
    if (!RegisterClassW(&wc) && GetLastError() != ERROR_CLASS_ALREADY_EXISTS) {
      impl->running = false;
      return;
    }

    // 2) 计算含边框/标题栏的实际窗口尺寸，再创建窗口
    RECT rc = {0, 0, impl->winW, impl->winH};
    AdjustWindowRect(&rc, WS_OVERLAPPEDWINDOW, FALSE);
    impl->hwnd = CreateWindowExW(
        0, L"FatigueGuardVideo", wtitle.c_str(), WS_OVERLAPPEDWINDOW,
        CW_USEDEFAULT, CW_USEDEFAULT, rc.right - rc.left, rc.bottom - rc.top,
        nullptr, nullptr, hInst, impl);   // 最后一个参数把 Impl 传进 WM_NCCREATE
    if (!impl->hwnd) {
      impl->running = false;
      return;
    }

    // 3) 显示窗口并抢焦点（让用户能直接点窗口按键）
    ShowWindow(impl->hwnd, SW_SHOW);
    UpdateWindow(impl->hwnd);
    SetForegroundWindow(impl->hwnd);
    SetFocus(impl->hwnd);
    impl->ready = true;   // 窗口就绪，start() 据此返回 true

    // 4) 消息循环：PeekMessage 轮询 + 短暂 Sleep，保证 running=false 时能退出
    while (impl->running) {
      MSG msg;
      while (PeekMessageW(&msg, nullptr, 0, 0, PM_REMOVE)) {
        if (msg.message == WM_QUIT) {   // WM_DESTROY 发出的退出消息
          impl->running = false;
          break;
        }
        TranslateMessage(&msg);
        DispatchMessageW(&msg);
      }
      if (impl->running) Sleep(10);     // 没消息时让出 CPU，避免空转
    }

    // 5) 收尾：销毁窗口、复位状态
    if (impl->hwnd && IsWindow(impl->hwnd)) DestroyWindow(impl->hwnd);
    impl->hwnd = nullptr;
    impl->ready = false;
    impl->running = false;
  });

  // 最多等 1 秒(100 * 10ms)，直到窗口线程把 ready 置 true
  for (int i = 0; i < 100 && !impl_->ready.load(); ++i) Sleep(10);
  return impl_->ready.load();
}

// 供外部(网络接收线程)推送一帧 BGRA 画面
void VideoViewer::showFrame(const uint8_t* bgra, int width, int height) {
  if (!bgra || width <= 0 || height <= 0) return;
  {
    std::lock_guard<std::mutex> lk(impl_->mtx);
    const size_t bytes = static_cast<size_t>(width) * static_cast<size_t>(height) * 4;
    impl_->frame.assign(bgra, bgra + bytes);  // 拷贝一帧进缓冲区
    impl_->frameW = width;
    impl_->frameH = height;
    impl_->hasFrame = true;
  }
  // 通知窗口线程重绘（PostMessage 是异步投递，线程安全）
  if (impl_->hwnd) PostMessageW(impl_->hwnd, kRefreshMsg, 0, 0);
}

void VideoViewer::stop() {
  impl_->running = false;               // 让消息循环退出
  if (impl_->thread.joinable()) impl_->thread.join();  // 等窗口线程结束
}

bool VideoViewer::started() const { return impl_->ready.load(); }

} // namespace fatigue
