package com.example.fatigueguard.alert

import android.content.Context
import android.media.AudioManager
import android.media.ToneGenerator
import android.os.Build
import android.os.Handler
import android.os.Looper
import android.os.VibrationEffect
import android.os.Vibrator
import android.os.VibratorManager
import com.example.fatigueguard.ui.DetectState

// On-device vibration + sound alerts, with cooldown and re-arm hysteresis so
// alerts don't spam while a dangerous state persists.
class AlertManager(context: Context) {
    private var enabled = true
    private var lastAlertAt = 0L
    private var lastDangerAt = 0L

    private val vibrator: Vibrator? by lazy {
        if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.S) {
            val vm = context.getSystemService(VibratorManager::class.java)
            vm?.defaultVibrator
        } else {
            @Suppress("DEPRECATION")
            context.getSystemService(Vibrator::class.java)
        }
    }

    fun setEnabled(value: Boolean) {
        enabled = value
    }

    fun onState(s: DetectState) {
        if (!enabled) return
        val now = System.currentTimeMillis()
        if (s.state == "awake" || s.state == "none") {
            // Re-arm once the driver has stayed safe for a while.
            if (now - lastDangerAt > REARM_MS) lastAlertAt = 0L
            return
        }
        lastDangerAt = now
        if (now - lastAlertAt < COOLDOWN_MS) return
        lastAlertAt = now

        when (s.state) {
            "drowsy" -> {
                vibrate(longArrayOf(0, 400, 200, 400))
                beep()
            }
            "distracted" -> vibrate(longArrayOf(0, 150))
        }
    }

    private fun vibrate(pattern: LongArray) {
        val v = vibrator ?: return
        if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.O) {
            v.vibrate(VibrationEffect.createWaveform(pattern, -1))
        } else {
            @Suppress("DEPRECATION")
            v.vibrate(pattern, -1)
        }
    }

    private fun beep() {
        runCatching {
            val tone = ToneGenerator(AudioManager.STREAM_ALARM, 90)
            tone.startTone(ToneGenerator.TONE_CDMA_ALERT_CALL_GUARD, 500)
            Handler(Looper.getMainLooper()).postDelayed({ tone.release() }, 600)
        }
    }

    companion object {
        private const val COOLDOWN_MS = 3000L
        private const val REARM_MS = 5000L
    }
}
