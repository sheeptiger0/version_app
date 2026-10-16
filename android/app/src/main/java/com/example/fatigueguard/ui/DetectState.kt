package com.example.fatigueguard.ui

// Server-reported detection state, mirrored into the UI via StateFlow.
data class DetectState(
    val state: String = "awake", // awake | drowsy | distracted | none
    val ear: Float = 1f,
    val mar: Float = 0f,
    val yaw: Float = 0f,
    val pitch: Float = 0f,
    val box: FloatArray = FloatArray(0), // [x, y, w, h] normalized
    val fps: Float = 0f,
    val connected: Boolean = false,
)
