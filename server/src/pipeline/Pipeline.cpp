#include "pipeline/Pipeline.h"

#include <iostream>

#include "pipeline/MockPipeline.h"

#ifdef FATIGUE_ENABLE_ONNX
#include "pipeline/OnnxPipeline.h"
#endif

namespace fatigue {

std::unique_ptr<IPipeline> makePipeline(const PipelineOptions& opts) {
#ifdef FATIGUE_ENABLE_ONNX
  if (opts.backend == "onnx") {
    auto p = std::make_unique<OnnxPipeline>(opts);
    if (p->ready()) return p;
    std::cerr << "[pipeline] ONNX backend failed to initialize; falling back to mock\n";
    return std::make_unique<MockPipeline>();
  }
#else
  (void)opts;
#endif
  return std::make_unique<MockPipeline>();
}

} // namespace fatigue
