@echo off
chcp 65001 >nul
rem ============================================================
rem  TensorRT engine 预编译脚本
rem  前提：已安装 TensorRT，且 trtexec 在 PATH 中。
rem  用法：先把 yolov8n-face.onnx 和 pfld.onnx 放到 server\models\，
rem        然后运行本脚本，生成对应的 .engine 文件。
rem ============================================================
cd /d "%~dp0\.."

where trtexec >nul 2>nul
if errorlevel 1 (
  echo [错误] 找不到 trtexec，请确认 TensorRT 的 bin 目录已加入 PATH。
  goto :end
)

if not exist "models\yolov8n-face.onnx" (
  echo [错误] 找不到 models\yolov8n-face.onnx
  goto :end
)
if not exist "models\pfld.onnx" (
  echo [错误] 找不到 models\pfld.onnx
  goto :end
)

echo 正在编译 yolov8n-face.engine ^(FP16^)...
trtexec --onnx=models\yolov8n-face.onnx --saveEngine=models\yolov8n-face.engine --fp16

echo.
echo 正在编译 pfld.engine ^(FP16^)...
trtexec --onnx=models\pfld.onnx --saveEngine=models\pfld.engine --fp16

echo.
echo 完成。engine 文件已生成到 models\ 目录。

:end
pause
