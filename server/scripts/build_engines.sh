#!/usr/bin/env bash
# ============================================================
#  TensorRT engine 预编译脚本（Linux）
#  前提：已安装 TensorRT，且 trtexec 在 PATH 中。
#  用法：先把 yolov8n-face.onnx 和 pfld.onnx 放到 server/models/，
#        然后运行本脚本，生成对应的 .engine 文件。
#
#  注意 1：两个模型都是「动态 shape」导出，而 TrTBackend 按固定 shape 加载
#  （getTensorShape 直接算体积），所以必须用 min=opt=max 把 batch/高宽钉死，
#  否则生成的 engine 带 -1 维度，运行时会 cudaMalloc 错误。
#
#  注意 2：TensorRT 11 移除了 trtexec 的 --fp16（及所有精度开关），
#  这里按 FP32 构建。两个模型都很小，FP32 在 RTX 5070 上足以实时。
#  如需混合精度，改用 ModelOpt AutoCast 离线转出 FP16 ONNX 再进来。
# ============================================================
set -euo pipefail

cd "$(dirname "$0")/.."

if ! command -v trtexec >/dev/null 2>&1; then
  echo "[错误] 找不到 trtexec，请把 TensorRT 的 bin 目录加入 PATH。" >&2
  exit 1
fi

for f in models/yolov8n-face.onnx models/pfld.onnx; do
  if [ ! -f "$f" ]; then
    echo "[错误] 找不到 $f" >&2
    exit 1
  fi
done

echo "正在编译 yolov8n-face.engine (FP32, 1x3x640x640)..."
trtexec --onnx=models/yolov8n-face.onnx --saveEngine=models/yolov8n-face.engine \
  --minShapes=images:1x3x640x640 --optShapes=images:1x3x640x640 --maxShapes=images:1x3x640x640

echo
echo "正在编译 pfld.engine (FP32, 1x3x112x112)..."
trtexec --onnx=models/pfld.onnx --saveEngine=models/pfld.engine \
  --minShapes=input:1x3x112x112 --optShapes=input:1x3x112x112 --maxShapes=input:1x3x112x112

echo
echo "完成。engine 文件已生成到 models/ 目录。"
