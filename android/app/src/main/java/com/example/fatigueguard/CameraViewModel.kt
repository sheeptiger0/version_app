package com.example.fatigueguard

import android.app.Application
import androidx.camera.view.PreviewView
import androidx.lifecycle.AndroidViewModel
import androidx.lifecycle.LifecycleOwner
import androidx.lifecycle.viewModelScope
import com.example.fatigueguard.alert.AlertManager
import com.example.fatigueguard.camera.CameraManager
import com.example.fatigueguard.net.UdpStreamClient
import com.example.fatigueguard.ui.DetectState
import kotlinx.coroutines.flow.MutableStateFlow
import kotlinx.coroutines.flow.StateFlow
import kotlinx.coroutines.launch

class CameraViewModel(app: Application) : AndroidViewModel(app) {
    private val client = UdpStreamClient(viewModelScope)
    private val alerts = AlertManager(app)

    private val _state = MutableStateFlow(DetectState())
    val state: StateFlow<DetectState> = _state

    private val _running = MutableStateFlow(false)
    val running: StateFlow<Boolean> = _running

    private val _alertEnabled = MutableStateFlow(true)
    val alertEnabled: StateFlow<Boolean> = _alertEnabled

    private var camera: CameraManager? = null

    init {
        viewModelScope.launch {
            client.state.collect { s ->
                _state.value = s
                alerts.onState(s)
            }
        }
    }

    fun start(host: String, port: Int, lifecycleOwner: LifecycleOwner, previewView: PreviewView) {
        if (_running.value) return
        client.start(host, port)
        camera = CameraManager(
            context = getApplication(),
            lifecycleOwner = lifecycleOwner,
            previewView = previewView,
            onFrame = { jpeg -> client.sendFrame(jpeg) },
        )
        camera?.start()
        _running.value = true
    }

    fun stop() {
        camera?.stop()
        camera = null
        client.stop()
        _running.value = false
    }

    fun setAlertEnabled(enabled: Boolean) {
        _alertEnabled.value = enabled
        alerts.setEnabled(enabled)
    }
}
