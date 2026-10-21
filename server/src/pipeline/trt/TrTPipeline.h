#pragma once

// 原生 TensorRT 推理管线：加载 .engine 模型。仅在 FATIGUE_ENABLE_TENSORRT 时编译。
// 前/后处理全部在 ModelPipeline 基类里，这里只负责创建 TrTBackend。

#include "pipeline/model/ModelPipeline.h"

namespace DMSAlgo {

class TrTPipeline : public ModelPipeline {
 public:
  explicit TrTPipeline(const PipelineOptions& opts);
  ~TrTPipeline() override = default;

 protected:
  std::unique_ptr<IInferenceBackend> createBackend(
      const std::string& modelPath) override;
};

} // namespace DMSAlgo
