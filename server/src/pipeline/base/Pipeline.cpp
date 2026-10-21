#include "pipeline/base/Pipeline.h"

#include "pipeline/mock/MockPipeline.h"
#include "util/Log.h"

#ifdef FATIGUE_ENABLE_ONNX
#include "pipeline/onnx/OnnxPipeline.h"
#endif
#ifdef FATIGUE_ENABLE_TENSORRT
#include "pipeline/trt/TrTPipeline.h"
#endif

namespace DMSAlgo {

std::unique_ptr<IPipeline> makePipeline(const PipelineOptions& opts) {
  // 只有显式请求真实后端（onnx/tensorrt）才走真实管线，其余都回 mock。
  if (opts.backend == "onnx") {
#ifdef FATIGUE_ENABLE_ONNX
    auto p = std::make_unique<OnnxPipeline>(opts);
    if (p->ready()) return p;
    LOG_WARN("pipeline") << "onnx backend failed to initialize; falling back to mock";
#else
    LOG_WARN("pipeline") << "this binary was built without ONNX Runtime; "
                            "--backend onnx is unavailable, falling back to mock";
#endif
  } else if (opts.backend == "tensorrt") {
#ifdef FATIGUE_ENABLE_TENSORRT
    auto p = std::make_unique<TrTPipeline>(opts);
    if (p->ready()) return p;
    LOG_WARN("pipeline") << "tensorrt backend failed to initialize; falling back to mock";
#else
    LOG_WARN("pipeline") << "this binary was built without TensorRT; "
                            "--backend tensorrt is unavailable, falling back to mock";
#endif
  }

  return std::make_unique<MockPipeline>();
}

} // namespace DMSAlgo
