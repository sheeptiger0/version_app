#pragma once

// ONNX Runtime 推理管线：加载 .onnx 模型。仅在 FATIGUE_ENABLE_ONNX 时编译。
// 前/后处理全部在 ModelPipeline 基类里，这里只负责创建 OrtBackend。

#include "pipeline/model/ModelPipeline.h"

namespace DMSAlgo {

class OnnxPipeline : public ModelPipeline {
 public:
  explicit OnnxPipeline(const PipelineOptions& opts);
  ~OnnxPipeline() override = default;

 protected:
  std::unique_ptr<IInferenceBackend> createBackend(
      const std::string& modelPath) override;
};

} // namespace DMSAlgo
