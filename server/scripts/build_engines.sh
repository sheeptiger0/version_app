#!/usr/bin/env bash
# ============================================================
#  TensorRT engine 预编译脚本（Linux）
#  前提：已安装 TensorRT，且 trtexec 在 PATH 中。
#  用法：先把 yolov8n-face.onnx 和 pfld.onnx 放到 server/models/，
#        然后运行本脚本，生成对应的 .engine 文件。
# ============================================================
set -euo pipefail

cd "$(dirname "$0")/.."

if ! command -v trtexec >/dev/null 2>&1; then
  echo "[错误] 找不到 trtexec，请把 TensorRT 的 bin 目录加入 PATH。" >&2
  exit 1
fi

if [ ! -f "models/yolov8n-face.onnx" ]; then
  echo "[错误] 找不到 models/yolov8n-face.onnx" >&2
  exit 1
fi
if [ ! -f "models/pfld.onnx" ]; then
  echo "[错误] 找不到 models/pfld.onnx" >&2
  exit 1
fi

echo "正在编译 yolov8n-face.engine (FP16)..."
trtexec --onnx=models/yolov8n-face.onnx --saveEngine=models/yolov8n-face.engine --fp16

echo
echo "正在编译 pfld.engine (FP16)..."
trtexec --onnx=models/pfld.onnx --saveEngine=models/pfld.engine --fp16

echo
echo "完成。engine 文件已生成到 models/ 目录。"
