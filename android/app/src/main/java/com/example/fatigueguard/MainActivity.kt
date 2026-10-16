package com.example.fatigueguard

import android.Manifest
import android.content.pm.PackageManager
import android.os.Bundle
import androidx.activity.ComponentActivity
import androidx.activity.compose.rememberLauncherForActivityResult
import androidx.activity.compose.setContent
import androidx.activity.result.contract.ActivityResultContracts
import androidx.camera.view.PreviewView
import androidx.compose.foundation.Canvas
import androidx.compose.foundation.background
import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Box
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.Spacer
import androidx.compose.foundation.layout.fillMaxSize
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.height
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.shape.RoundedCornerShape
import androidx.compose.material3.Button
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.OutlinedTextField
import androidx.compose.material3.Switch
import androidx.compose.material3.Text
import androidx.compose.runtime.Composable
import androidx.compose.runtime.collectAsState
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.remember
import androidx.compose.runtime.setValue
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.geometry.Offset
import androidx.compose.ui.geometry.Size
import androidx.compose.ui.graphics.Color
import androidx.compose.ui.graphics.drawscope.Stroke
import androidx.compose.ui.platform.LocalContext
import androidx.compose.ui.platform.LocalLifecycleOwner
import androidx.compose.ui.text.font.FontWeight
import androidx.compose.ui.unit.dp
import androidx.compose.ui.unit.sp
import androidx.compose.ui.viewinterop.AndroidView
import androidx.core.content.ContextCompat
import androidx.lifecycle.viewmodel.compose.viewModel

class MainActivity : ComponentActivity() {
    override fun onCreate(savedInstanceState: Bundle?) {
        super.onCreate(savedInstanceState)
        setContent {
            MaterialTheme { FatigueGuardApp() }
        }
    }
}

private fun stateColor(state: String): Color = when (state) {
    "drowsy" -> Color(0xFFF44336)
    "distracted" -> Color(0xFFFFC107)
    "awake" -> Color(0xFF4CAF50)
    else -> Color(0xFF9E9E9E)
}

private fun stateLabel(state: String): String = when (state) {
    "drowsy" -> "疲劳"
    "distracted" -> "分心"
    "awake" -> "清醒"
    else -> "未检测到人脸"
}

@Composable
fun FatigueGuardApp(vm: CameraViewModel = viewModel()) {
    val context = LocalContext.current
    val lifecycleOwner = LocalLifecycleOwner.current
    val state by vm.state.collectAsState()
    val running by vm.running.collectAsState()
    val alertEnabled by vm.alertEnabled.collectAsState()

    var host by remember { mutableStateOf("192.168.1.100") }
    var portText by remember { mutableStateOf("9000") }
    val previewView = remember { PreviewView(context) }

    val requestPermission = rememberLauncherForActivityResult(
        ActivityResultContracts.RequestPermission(),
    ) { granted ->
        if (granted) {
            val port = portText.toIntOrNull() ?: 9000
            vm.start(host, port, lifecycleOwner, previewView)
        }
    }

    fun startStreaming() {
        if (ContextCompat.checkSelfPermission(context, Manifest.permission.CAMERA) ==
            PackageManager.PERMISSION_GRANTED
        ) {
            val port = portText.toIntOrNull() ?: 9000
            vm.start(host, port, lifecycleOwner, previewView)
        } else {
            requestPermission.launch(Manifest.permission.CAMERA)
        }
    }

    Column(modifier = Modifier.fillMaxSize()) {
        Box(
            modifier = Modifier
                .fillMaxWidth()
                .weight(1f)
                .background(Color.Black),
        ) {
            AndroidView(factory = { previewView }, modifier = Modifier.fillMaxSize())

            // Draw the face box over the preview using normalized coords.
            if (running && state.box.size >= 4) {
                Canvas(modifier = Modifier.fillMaxSize()) {
                    val w = size.width * state.box[2]
                    val h = size.height * state.box[3]
                    val x = size.width * state.box[0]
                    val y = size.height * state.box[1]
                    drawRect(
                        color = stateColor(state.state),
                        topLeft = Offset(x, y),
                        size = Size(w, h),
                        style = Stroke(width = 6f),
                    )
                }
            }
        }

        // Status card.
        Row(
            modifier = Modifier
                .fillMaxWidth()
                .padding(12.dp)
                .background(stateColor(state.state).copy(alpha = 0.15f), RoundedCornerShape(12.dp))
                .padding(16.dp),
            verticalAlignment = Alignment.CenterVertically,
        ) {
            Column(modifier = Modifier.weight(1f)) {
                Text(
                    text = stateLabel(state.state),
                    color = stateColor(state.state),
                    fontWeight = FontWeight.Bold,
                    fontSize = 24.sp,
                )
                Text(
                    text = if (state.connected) "已连接 · 服务端 FPS ${"%.1f".format(state.fps)}"
                    else "未连接",
                    color = Color.Gray,
                    fontSize = 13.sp,
                )
                Spacer(modifier = Modifier.height(4.dp))
                Text(
                    text = "EAR ${"%.2f".format(state.ear)}   MAR ${"%.2f".format(state.mar)}",
                    fontSize = 14.sp,
                )
                Text(
                    text = "Yaw ${"%.1f".format(state.yaw)}°   Pitch ${"%.1f".format(state.pitch)}°",
                    fontSize = 14.sp,
                )
            }
            Column(horizontalAlignment = Alignment.End) {
                Text(text = "告警", fontSize = 13.sp, color = Color.Gray)
                Switch(checked = alertEnabled, onCheckedChange = vm::setAlertEnabled)
            }
        }

        // Controls.
        Column(
            modifier = Modifier
                .fillMaxWidth()
                .padding(horizontal = 12.dp),
            verticalArrangement = Arrangement.spacedBy(8.dp),
        ) {
            Row(horizontalArrangement = Arrangement.spacedBy(8.dp)) {
                OutlinedTextField(
                    value = host,
                    onValueChange = { host = it },
                    label = { Text("服务器 IP") },
                    enabled = !running,
                    modifier = Modifier.weight(2f),
                    singleLine = true,
                )
                OutlinedTextField(
                    value = portText,
                    onValueChange = { portText = it },
                    label = { Text("端口") },
                    enabled = !running,
                    modifier = Modifier.weight(1f),
                    singleLine = true,
                )
            }
            Button(
                onClick = { if (running) vm.stop() else startStreaming() },
                modifier = Modifier
                    .fillMaxWidth()
                    .height(52.dp),
            ) {
                Text(text = if (running) "停止" else "开始检测", fontSize = 18.sp)
            }
            Spacer(modifier = Modifier.height(8.dp))
        }
    }
}
