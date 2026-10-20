#include "pipeline/Pipeline.h"

#include <iostream>

#include "pipeline/MockPipeline.h"

#if defined(FATIGUE_ENABLE_ONNX) || defined(FATIGUE_ENABLE_TENSORRT)
#include "pipeline/ModelPipeline.h"
#endif

namespace fatigue {

std::unique_ptr<IPipeline> makePipeline(const PipelineOptions& opts) {
  // 只有显式请求真实后端（onnx/tensorrt）才走 ModelPipeline，其余都回 mock。
  const bool wantsOnnx = (opts.backend == "onnx");
  const bool wantsTrT  = (opts.backend == "tensorrt");
  if (!wantsOnnx && !wantsTrT) return std::make_unique<MockPipeline>();

#if defined(FATIGUE_ENABLE_ONNX) || defined(FATIGUE_ENABLE_TENSORRT)
  // 请求的后端是否真编进了当前二进制（例如 onnx-only 构建请求 tensorrt）。
#  if !defined(FATIGUE_ENABLE_ONNX)
  if (wantsOnnx) {
    std::cerr << "[pipeline] this binary was built without ONNX Runtime; "
                 "--backend onnx is unavailable, falling back to mock\n";
    return std::make_unique<MockPipeline>();
  }
#  endif
#  if !defined(FATIGUE_ENABLE_TENSORRT)
  if (wantsTrT) {
    std::cerr << "[pipeline] this binary was built without TensorRT; "
                 "--backend tensorrt is unavailable, falling back to mock\n";
    return std::make_unique<MockPipeline>();
  }
#  endif

  auto p = std::make_unique<ModelPipeline>(opts);
  if (p->ready()) return p;
  std::cerr << "[pipeline] " << opts.backend
            << " backend failed to initialize; falling back to mock\n";
#else
  // mock-only 构建：根本没编译任何真实后端，给出明确提示而不是静默回退。
  (void)opts;
  std::cerr << "[pipeline] this binary was built without onnx/tensorrt support; "
               "--backend " << opts.backend
            << " is unavailable, falling back to mock\n";
#endif
  return std::make_unique<MockPipeline>();
}

} // namespace fatigue
