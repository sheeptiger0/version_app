#include "pipeline/Pipeline.h"

#include <iostream>

#include "pipeline/MockPipeline.h"

#if defined(FATIGUE_ENABLE_ONNX) || defined(FATIGUE_ENABLE_TENSORRT)
#include "pipeline/ModelPipeline.h"
#endif

namespace fatigue {

std::unique_ptr<IPipeline> makePipeline(const PipelineOptions& opts) {
#if defined(FATIGUE_ENABLE_ONNX) || defined(FATIGUE_ENABLE_TENSORRT)
  if (opts.backend == "onnx" || opts.backend == "tensorrt") {
    auto p = std::make_unique<ModelPipeline>(opts);
    if (p->ready()) return p;
    std::cerr << "[pipeline] " << opts.backend
              << " backend failed to initialize; falling back to mock\n";
    return std::make_unique<MockPipeline>();
  }
#else
  (void)opts;      //为了避免编译器报 unused parameter 的警告，使用 (void)opts; 明确告诉编译器：“我知道它没用，我是故意的”。
#endif
  return std::make_unique<MockPipeline>();
}

} // namespace fatigue
