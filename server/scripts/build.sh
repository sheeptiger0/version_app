#!/usr/bin/env bash
# ============================================================
#  Linux 一键编译脚本（配置 + 编译）
#
#  用法:
#    bash scripts/build.sh               # 默认编译 tensorrt 后端
#    bash scripts/build.sh mock          # 仅 mock（无需任何模型/GPU 依赖）
#    bash scripts/build.sh onnx          # ONNX Runtime 后端
#    bash scripts/build.sh tensorrt      # 原生 TensorRT 后端
#    bash scripts/build.sh tensorrt -j 8 # 指定并行编译线程数
#
#  依赖检查：
#    mock      -> cmake + g++（仅此两项）
#    onnx      -> 另需 vcpkg（设 VCPKG_ROOT）
#    tensorrt  -> 另需 CUDA Toolkit + TensorRT（建议设 TensorRT_ROOT）+ OpenCV
# ============================================================
set -euo pipefail

cd "$(dirname "$0")/.."

BACKEND="tensorrt"
JOBS="$(nproc 2>/dev/null || echo 4)"

while [ $# -gt 0 ]; do
  case "$1" in
    -j)
      JOBS="$2"
      shift 2
      ;;
    mock|onnx|tensorrt)
      BACKEND="$1"
      shift
      ;;
    *)
      echo "[错误] 未知参数: $1（可选 mock|onnx|tensorrt，或 -j N）" >&2
      exit 1
      ;;
  esac
done

case "$BACKEND" in
  mock)     PRESET="linux-default" ;;
  onnx)     PRESET="linux-onnx" ;;
  tensorrt) PRESET="linux-tensorrt" ;;
esac

# ---- 依赖检查 ----
command -v cmake >/dev/null 2>&1 || { echo "[错误] 找不到 cmake，请先安装。" >&2; exit 1; }
command -v g++  >/dev/null 2>&1 || { echo "[错误] 找不到 g++，请先安装 build-essential。" >&2; exit 1; }

if [ "$BACKEND" = "onnx" ] && [ -z "${VCPKG_ROOT:-}" ]; then
  echo "[警告] 未设置 VCPKG_ROOT，linux-onnx preset 依赖 vcpkg toolchain，配置可能失败。" >&2
fi

if [ "$BACKEND" = "tensorrt" ]; then
  if [ -z "${TensorRT_ROOT:-}" ]; then
    echo "[提示] 未设置 TensorRT_ROOT，将回退到默认搜索路径（/usr/local/TensorRT、/opt/TensorRT、/usr 等）。" >&2
  fi
  command -v nvcc >/dev/null 2>&1 || \
    echo "[提示] 未在 PATH 找到 nvcc，请确认 CUDA Toolkit 已安装并加入 PATH。" >&2
fi

# ---- 配置 + 编译 ----
echo "==> 配置 preset: $PRESET"
cmake --preset "$PRESET"

echo "==> 编译（并行 $JOBS）"
cmake --build --preset "$PRESET" -j "$JOBS"

echo
echo "==> 完成。可执行文件: build/$PRESET/fatigue_server"
if [ "$BACKEND" = "tensorrt" ]; then
  echo "    如尚未生成 engine，请先运行: bash scripts/build_engines.sh"
fi
echo "    运行: ./build/$PRESET/fatigue_server --backend $BACKEND --device cuda --port 9000"
