package com.chamalanquimyst.loopera

// ─────────────────────────────────────────────────────────────────────────────
// LooperBridge — interfaz Kotlin ↔ motor C++ via JNI
// REGLA: todos los métodos nativos son suspending o llamados desde
//        coroutines en Dispatchers.IO para no bloquear la UI.
// ─────────────────────────────────────────────────────────────────────────────
object LooperBridge {

    init {
        System.loadLibrary("loopera_engine")
    }

    // Comandos espejo del enum EngineCmd en C++
    object Cmd {
        const val NONE         = 0
        const val REC_TOGGLE   = 1
        const val STOP_GLOBAL  = 2
        const val UNDO_LAYER   = 3
        const val CLEAR_ALL    = 4
        const val MUTE_LAYER   = 5
        const val RTL_MEASURED = 6
    }

    // Estados espejo del enum EngineState en C++
    object State {
        const val IDLE      = 0
        const val REC_BASE  = 1
        const val PLAYING   = 2
        const val OVERDUB   = 3
    }

    // ── JNI nativa ────────────────────────────────────────────────────────
    external fun nativeStart(filesDir: String): Boolean
    external fun nativeStop()
    external fun nativeSendCommand(cmd: Int, payload: Long = 0L)
    external fun nativeGetSnapshot(): FloatArray
    external fun nativeSetRTL(rtlFrames: Long)
    external fun nativeGetSampleRate(): Int
    external fun nativeExportMP3(sessionPcmPath: String, mp3OutPath: String, sampleRate: Int): Boolean

    // ── Helpers de alto nivel ────────────────────────────────────────────
    fun recToggle()               = nativeSendCommand(Cmd.REC_TOGGLE)
    fun stopGlobal()              = nativeSendCommand(Cmd.STOP_GLOBAL)
    fun undoLastLayer()           = nativeSendCommand(Cmd.UNDO_LAYER)
    fun clearAll()                = nativeSendCommand(Cmd.CLEAR_ALL)
    fun muteLayer(index: Int)     = nativeSendCommand(Cmd.MUTE_LAYER, index.toLong())

    // Parseo del snapshot devuelto por JNI
    data class Snapshot(
        val state:      Int,
        val layerCount: Int,
        val progress:   Float,  // playhead normalizado 0..1
        val masterRMS:  Float,
        val onset:      Boolean,
        val layerRMS:   FloatArray
    )

    fun getSnapshot(): Snapshot {
        val arr = nativeGetSnapshot()
        return Snapshot(
            state      = arr.getOrElse(0) { 0f }.toInt(),
            layerCount = arr.getOrElse(1) { 0f }.toInt(),
            progress   = arr.getOrElse(2) { 0f },
            masterRMS  = arr.getOrElse(3) { 0f },
            onset      = arr.getOrElse(4) { 0f } > 0.5f,
            layerRMS   = if (arr.size > 5) arr.copyOfRange(5, arr.size) else FloatArray(0)
        )
    }
}
