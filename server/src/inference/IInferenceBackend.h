#pragma once

// 推理后端抽象接口：ONNX Runtime（OrtBackend）和原生 TensorRT（TrTBackend）
// 都实现这个接口，让 ModelPipeline 的前/后处理与具体后端解耦。

#include <cstdint>
#include <vector>

namespace DMSAlgo {

class IInferenceBackend {
 public:
  virtual ~IInferenceBackend() = default;

  // 后端是否加载成功。
  virtual bool ok() const = 0;

  // 跑一个形状为 inputShape（如 {1,3,H,W}）的 float 输入张量，
  // 返回每个输出（平铺成 float 数组）及其形状。
  virtual std::vector<std::vector<float>> run(
      const std::vector<float>& input,
      const std::vector<int64_t>& inputShape,
      std::vector<std::vector<int64_t>>& outputShapes) const = 0;

  virtual std::vector<int64_t> inputShape() const = 0;
  virtual std::vector<std::vector<int64_t>> outputShapes() const = 0;
};

} // namespace DMSAlgo
