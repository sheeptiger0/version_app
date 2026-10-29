#pragma once

// 原生 TensorRT 推理后端：加载预编译的 .engine 文件，跑 GPU 推理。
// 仅在开启 FATIGUE_ENABLE_TENSORRT 时编译。
//
// .engine 由 trtexec 离线生成（见 scripts/build_engines.bat），
// 运行期只依赖 nvinfer + cudart，不需要 nvonnxparser / nvinfer_plugin。

#include <memory>
#include <string>
#include <vector>

#include "../inference/IInferenceBackend.h"

namespace DMSAlgo {

class TrTBackend : public IInferenceBackend {
 public:
  explicit TrTBackend(const std::string& enginePath);
  ~TrTBackend();  // 定义在 .cpp：Impl 是不完整类型，析构器不能在头文件内联

  bool ok() const override;
  std::vector<std::vector<float>> run(
      const std::vector<float>& input,
      const std::vector<int64_t>& inputShape,
      std::vector<std::vector<int64_t>>& outputShapes) const override;
  std::vector<int64_t> inputShape() const override;
  std::vector<std::vector<int64_t>> outputShapes() const override;

 private:
  struct Impl;
  std::unique_ptr<Impl> impl_;
};

} // namespace DMSAlgo
