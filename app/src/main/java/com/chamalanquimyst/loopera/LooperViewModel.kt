package com.chamalanquimyst.loopera

import android.app.Application
import android.content.Context
import android.content.Intent
import androidx.lifecycle.AndroidViewModel
import androidx.lifecycle.viewModelScope
import kotlinx.coroutines.*
import kotlinx.coroutines.flow.*
import java.io.File
import java.text.SimpleDateFormat
import java.util.*

// ─────────────────────────────────────────────────────────────────────────────
// LooperViewModel — fuente de verdad de la UI
// ─────────────────────────────────────────────────────────────────────────────
class LooperViewModel(app: Application) : AndroidViewModel(app) {

    // ── Estado de la UI ──────────────────────────────────────────────────
    private val _snapshot = MutableStateFlow(LooperBridge.Snapshot(
        state = LooperBridge.State.IDLE,
        layerCount = 0,
        progress = 0f,
        masterRMS = 0f,
        onset = false,
        layerRMS = FloatArray(0)
    ))
    val snapshot: StateFlow<LooperBridge.Snapshot> = _snapshot.asStateFlow()

    private val _engineReady  = MutableStateFlow(false)
    val engineReady: StateFlow<Boolean> = _engineReady.asStateFlow()

    private val _exportPath   = MutableStateFlow<String?>(null)
    val exportPath: StateFlow<String?> = _exportPath.asStateFlow()

    private val _exporting    = MutableStateFlow(false)
    val exporting: StateFlow<Boolean> = _exporting.asStateFlow()

    private val _rtlMs        = MutableStateFlow(0f)
    val rtlMs: StateFlow<Float> = _rtlMs.asStateFlow()

    private val _errorMsg     = MutableStateFlow<String?>(null)
    val errorMsg: StateFlow<String?> = _errorMsg.asStateFlow()

    private val filesDir: String = app.filesDir.absolutePath
    private var snapshotJob: Job? = null

    // ── Inicio del motor ─────────────────────────────────────────────────
    fun startEngine(context: Context) {
        viewModelScope.launch(Dispatchers.IO) {
            val ok = LooperBridge.nativeStart(filesDir)
            withContext(Dispatchers.Main) {
                if (ok) {
                    _engineReady.value = true
                    startSnapshotLoop()
                    // Iniciar foreground service para proteger el audio en background
                    val intent = Intent(context, LooperForegroundService::class.java)
                    context.startForegroundService(intent)
                } else {
                    _errorMsg.value = "Error al abrir el stream de audio. Verifica permisos."
                }
            }
        }
    }

    // ── Poll del snapshot a ~30fps ────────────────────────────────────────
    private fun startSnapshotLoop() {
        snapshotJob?.cancel()
        snapshotJob = viewModelScope.launch(Dispatchers.IO) {
            while (isActive) {
                val snap = LooperBridge.getSnapshot()
                _snapshot.emit(snap)
                delay(33L) // ~30 fps
            }
        }
    }

    // ── Comandos de la UI ─────────────────────────────────────────────────
    fun onRecToggle() = LooperBridge.recToggle()

    fun onStopGlobal(context: Context) {
        LooperBridge.stopGlobal()
        viewModelScope.launch(Dispatchers.IO) {
            delay(200L) // dar tiempo al motor para cerrar la grabación
            exportSession(context)
        }
    }

    fun onUndoLayer()          = LooperBridge.undoLastLayer()
    fun onClearAll()           = LooperBridge.clearAll()
    fun onMuteLayer(idx: Int)  = LooperBridge.muteLayer(idx)

    // ── Calibración RTL manual ────────────────────────────────────────────
    // msValue viene del slider de la UI
    fun setRTLMs(msValue: Float) {
        _rtlMs.value = msValue
        val sr     = LooperBridge.nativeGetSampleRate()
        val frames = (msValue / 1000f * sr).toLong()
        LooperBridge.nativeSetRTL(frames)
    }

    // ── Exportación a MP3 ─────────────────────────────────────────────────
    private fun exportSession(context: Context) {
        viewModelScope.launch(Dispatchers.IO) {
            _exporting.emit(true)
            val sampleRate = LooperBridge.nativeGetSampleRate()
            val pcmPath    = "$filesDir/session_raw.pcm"
            val timestamp  = SimpleDateFormat("yyyyMMdd_HHmmss", Locale.getDefault()).format(Date())
            val mp3Dir     = context.getExternalFilesDir(null)?.absolutePath ?: filesDir
            val mp3Path    = "$mp3Dir/loopera_session_$timestamp.mp3"

            val ok = LooperBridge.nativeExportMP3(pcmPath, mp3Path, sampleRate)
            withContext(Dispatchers.Main) {
                _exporting.value = false
                if (ok) {
                    _exportPath.value = mp3Path
                } else {
                    _errorMsg.value = "Error al exportar MP3. Verifica espacio en disco."
                }
            }
        }
    }

    override fun onCleared() {
        snapshotJob?.cancel()
        LooperBridge.nativeStop()
        super.onCleared()
    }
}
