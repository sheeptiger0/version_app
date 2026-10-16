package com.example.fatigueguard.camera

import android.content.Context
import android.graphics.ImageFormat
import android.graphics.Rect
import android.graphics.YuvImage
import android.util.Log
import android.util.Size
import androidx.camera.core.CameraSelector
import androidx.camera.core.ImageAnalysis
import androidx.camera.core.ImageProxy
import androidx.camera.core.Preview
import androidx.camera.lifecycle.ProcessCameraProvider
import androidx.camera.view.PreviewView
import androidx.core.content.ContextCompat
import androidx.lifecycle.LifecycleOwner
import java.io.ByteArrayOutputStream
import java.util.concurrent.ExecutorService
import java.util.concurrent.Executors

// CameraX: preview + YUV_420_888 analysis -> JPEG, handed to onFrame.
class CameraManager(
    private val context: Context,
    private val lifecycleOwner: LifecycleOwner,
    private val previewView: PreviewView,
    private val onFrame: (ByteArray) -> Unit,
) {
    private var analysisExecutor: ExecutorService? = null

    fun start() {
        val providerFuture = ProcessCameraProvider.getInstance(context)
        providerFuture.addListener({
            runCatching {
                val provider = providerFuture.get()
                val preview = Preview.Builder().build().also {
                    it.setSurfaceProvider(previewView.surfaceProvider)
                }
                val analysis = ImageAnalysis.Builder()
                    .setTargetResolution(Size(640, 480))
                    .setBackpressureStrategy(ImageAnalysis.STRATEGY_KEEP_ONLY_LATEST)
                    .setOutputImageFormat(ImageAnalysis.OUTPUT_IMAGE_FORMAT_YUV_420_888)
                    .build()
                val executor = Executors.newSingleThreadExecutor()
                analysisExecutor = executor
                analysis.setAnalyzer(executor) { image -> analyze(image, onFrame) }

                provider.unbindAll()
                provider.bindToLifecycle(
                    lifecycleOwner,
                    CameraSelector.DEFAULT_FRONT_CAMERA,
                    preview,
                    analysis,
                )
            }.onFailure { Log.e(TAG, "camera start failed", it) }
        }, ContextCompat.getMainExecutor(context))
    }

    fun stop() {
        analysisExecutor?.shutdown()
        analysisExecutor = null
    }

    private fun analyze(image: ImageProxy, onFrame: (ByteArray) -> Unit) {
        try {
            val nv21 = yuv420ToNv21(image)
            val yuv = YuvImage(nv21, ImageFormat.NV21, image.width, image.height, null)
            val out = ByteArrayOutputStream()
            yuv.compressToJpeg(Rect(0, 0, image.width, image.height), 70, out)
            onFrame(out.toByteArray())
        } catch (e: Exception) {
            Log.w(TAG, "frame compress failed", e)
        } finally {
            image.close()
        }
    }

    // YUV_420_888 -> NV21 (Y plane + interleaved VU), handling row/pixel strides.
    private fun yuv420ToNv21(image: ImageProxy): ByteArray {
        val w = image.width
        val h = image.height
        val y = image.planes[0]
        val u = image.planes[1]
        val v = image.planes[2]
        val ySize = w * h
        val nv21 = ByteArray(ySize + ySize / 2)

        if (y.pixelStride == 1) {
            y.buffer.rewind()
            y.buffer.get(nv21, 0, ySize)
        } else {
            var i = 0
            while (i < ySize) {
                nv21[i] = y.buffer.get(y.rowStride * (i / w) + y.pixelStride * (i % w))
                i++
            }
        }

        val uvWidth = w / 2
        val uvHeight = h / 2
        var row = 0
        while (row < uvHeight) {
            var col = 0
            while (col < uvWidth) {
                val vByte = v.buffer.get(row * v.rowStride + col * v.pixelStride)
                val uByte = u.buffer.get(row * u.rowStride + col * u.pixelStride)
                val idx = ySize + row * w + col * 2
                nv21[idx] = vByte
                nv21[idx + 1] = uByte
                col++
            }
            row++
        }
        return nv21
    }

    companion object {
        private const val TAG = "CameraManager"
    }
}
