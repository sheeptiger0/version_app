# models/

把 ONNX 模型放到本目录，然后以 `--backend onnx` 启动即可切换真实推理：

- `yolov8n-face.onnx` — 人脸检测（输入 1x3x640x640）
- `pfld.onnx` — 68 点关键点（输入 1x3x112x112，可选）

下载/导出建议：

- YOLOv8n-face：可从 Ultralytics 导出或使用社区导出的 face-detection ONNX。
  `yolo export model=yolov8n-face.pt format=onnx imgsz=640`
- PFLD：社区预训练 ONNX（如 InsightFace / 1adrianb/face-alignment 的导出）。

不放入任何模型时，服务端以 `--backend mock` 运行即可跑通整条链路（合成数据）。
