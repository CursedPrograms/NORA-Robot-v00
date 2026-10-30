package com.synthwomb.nora

import java.util.Locale

// What NORA takes: the same calls as RITA and her PC controller.
class Robot(private val http: Http) : RobotApi {
    private fun n(v: Float) = String.format(Locale.ROOT, "%.2f", v)

    override fun connect(pin: String) {
        http.get("/motorlockOff?pw=$pin")
        http.get("/modeManual")
    }
    override fun drive(forward: Float, side: Float, turn: Float) {
        http.get("/drive?f=${n(forward)}&s=${n(side)}&t=${n(turn)}")
    }
    override fun stop() { http.get("/stop") }
    override val buttons = listOf(
        "MANUAL" to { http.get("/modeManual"); Unit },
        "AUTO" to { http.get("/modeAuto"); Unit },
        "LINE" to { http.get("/modeLine"); Unit },
        "FOLLOW" to { http.get("/modeFollow"); Unit },
        "HELLO" to { http.get("/act?do=hello"); Unit },
        "DANCE" to { http.get("/act?do=dance"); Unit },
        "LOCK" to { http.get("/motorlockOn"); Unit },
    )
}
