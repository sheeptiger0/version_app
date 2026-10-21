#include "video/Jpeg.h"

#include <windows.h>
#include <objbase.h>
#include <wincodec.h>

namespace DMSAlgo {

namespace {

// Tiny RAII COM pointer for the handful of WIC interfaces we touch.
template <typename T>
struct ComPtr {
  T* p = nullptr;

  ComPtr() = default;
  ComPtr(const ComPtr&) = delete;
  ComPtr& operator=(const ComPtr&) = delete;
  ~ComPtr() {
    if (p) p->Release();
  }
  T** put() { return &p; }
  T* get() const { return p; }
  T* operator->() const { return p; }
  explicit operator bool() const { return p != nullptr; }
};

} // namespace

bool decodeJpegToBgra(const uint8_t* jpeg, size_t len, int& outW, int& outH,
                      std::vector<uint8_t>& outBgra) {
  if (!jpeg || len == 0 || len > 0xFFFFFFFFull) return false;

  // WIC is COM-based; initialize on the calling thread (no-op if already done).
  const HRESULT co = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
  if (FAILED(co) && co != RPC_E_CHANGED_MODE) return false;

  ComPtr<IWICImagingFactory> factory;
  if (FAILED(CoCreateInstance(CLSID_WICImagingFactory, nullptr, CLSCTX_INPROC_SERVER,
                              IID_IWICImagingFactory,
                              reinterpret_cast<void**>(factory.put()))))
    return false;

  ComPtr<IWICStream> stream;
  if (FAILED(factory->CreateStream(stream.put()))) return false;
  if (FAILED(stream->InitializeFromMemory(const_cast<BYTE*>(jpeg),
                                          static_cast<DWORD>(len))))
    return false;

  ComPtr<IWICBitmapDecoder> decoder;
  if (FAILED(factory->CreateDecoderFromStream(stream.get(), nullptr,
                                              WICDecodeMetadataCacheOnDemand,
                                              decoder.put())))
    return false;

  ComPtr<IWICBitmapFrameDecode> frame;
  if (FAILED(decoder->GetFrame(0, frame.put()))) return false;

  ComPtr<IWICFormatConverter> conv;
  if (FAILED(factory->CreateFormatConverter(conv.put()))) return false;
  if (FAILED(conv->Initialize(frame.get(), GUID_WICPixelFormat32bppBGRA,
                              WICBitmapDitherTypeNone, nullptr, 0.0,
                              WICBitmapPaletteTypeCustom)))
    return false;

  UINT w = 0, h = 0;
  if (FAILED(conv->GetSize(&w, &h)) || w == 0 || h == 0) return false;

  const UINT stride = w * 4;
  outBgra.resize(static_cast<size_t>(stride) * h);
  if (FAILED(conv->CopyPixels(nullptr, stride, stride * h, outBgra.data())))
    return false;

  outW = static_cast<int>(w);
  outH = static_cast<int>(h);
  return true;
}

} // namespace DMSAlgo
