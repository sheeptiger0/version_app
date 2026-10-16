package com.example.fatigueguard.net

import android.util.Log
import com.example.fatigueguard.proto.FrameProtocol
import com.example.fatigueguard.ui.DetectState
import kotlinx.coroutines.CoroutineScope
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.Job
import kotlinx.coroutines.flow.MutableStateFlow
import kotlinx.coroutines.flow.StateFlow
import kotlinx.coroutines.isActive
import kotlinx.coroutines.launch
import org.json.JSONObject
import java.net.DatagramPacket
import java.net.DatagramSocket
import java.net.InetAddress
import java.util.concurrent.atomic.AtomicLong

// Sends JPEG frames to the server over UDP (fragmented) and receives state
// replies on the same socket.
class UdpStreamClient(private val scope: CoroutineScope) {
    private val _state = MutableStateFlow(DetectState())
    val state: StateFlow<DetectState> = _state

    private var socket: DatagramSocket? = null
    private var recvJob: Job? = null
    private var serverAddress: InetAddress? = null
    private var serverPort = 0
    private val frameId = AtomicLong(0)

    @Volatile
    private var running = false

    fun start(host: String, port: Int) {
        stop()
        try {
            serverAddress = InetAddress.getByName(host)
            serverPort = port
            socket = DatagramSocket()
            running = true
            sendHello()
            recvJob = scope.launch(Dispatchers.IO) { recvLoop() }
            _state.value = _state.value.copy(connected = true)
        } catch (e: Exception) {
            Log.e(TAG, "start failed", e)
            _state.value = _state.value.copy(connected = false)
        }
    }

    fun stop() {
        running = false
        recvJob?.cancel()
        runCatching { socket?.close() }
        socket = null
        _state.value = _state.value.copy(connected = false)
    }

    fun sendFrame(jpeg: ByteArray) {
        if (!running) return
        val s = socket ?: return
        val addr = serverAddress ?: return
        val id = frameId.getAndIncrement()
        try {
            for (dg in FrameProtocol.fragment(jpeg, id)) {
                s.send(DatagramPacket(dg, dg.size, addr, serverPort))
            }
        } catch (e: Exception) {
            Log.w(TAG, "send failed", e)
        }
    }

    private fun sendHello() {
        val s = socket ?: return
        val addr = serverAddress ?: return
        val header = FrameProtocol.packHeader(FrameProtocol.TYPE_HELLO, 0, 0, 0)
        val payload = "hello".toByteArray(Charsets.UTF_8)
        val dg = ByteArray(header.size + payload.size)
        System.arraycopy(header, 0, dg, 0, header.size)
        System.arraycopy(payload, 0, dg, header.size, payload.size)
        runCatching { s.send(DatagramPacket(dg, dg.size, addr, serverPort)) }
    }

    private fun recvLoop() {
        val s = socket ?: return
        val buf = ByteArray(4096)
        while (running && scope.isActive) {
            try {
                val pkt = DatagramPacket(buf, buf.size)
                s.receive(pkt)
                val h = FrameProtocol.unpackHeader(pkt.data, pkt.length) ?: continue
                if (h.type != FrameProtocol.TYPE_STATE) continue
                val json = String(
                    pkt.data,
                    FrameProtocol.HEADER_SIZE,
                    pkt.length - FrameProtocol.HEADER_SIZE,
                    Charsets.UTF_8,
                )
                val o = JSONObject(json)
                val boxArr = o.optJSONArray("box")
                val box = if (boxArr != null && boxArr.length() >= 4) {
                    floatArrayOf(
                        boxArr.getDouble(0).toFloat(),
                        boxArr.getDouble(1).toFloat(),
                        boxArr.getDouble(2).toFloat(),
                        boxArr.getDouble(3).toFloat(),
                    )
                } else {
                    FloatArray(0)
                }
                _state.value = DetectState(
                    state = o.optString("state", "awake"),
                    ear = o.optDouble("ear", 1.0).toFloat(),
                    mar = o.optDouble("mar", 0.0).toFloat(),
                    yaw = o.optDouble("yaw", 0.0).toFloat(),
                    pitch = o.optDouble("pitch", 0.0).toFloat(),
                    box = box,
                    fps = o.optDouble("fps", 0.0).toFloat(),
                    connected = true,
                )
            } catch (e: Exception) {
                if (!running) break
                Log.w(TAG, "recv error", e)
            }
        }
    }

    companion object {
        private const val TAG = "UdpStreamClient"
    }
}
