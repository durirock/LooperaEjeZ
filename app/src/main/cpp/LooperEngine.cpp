#include "LooperEngine.h"
#include <android/log.h>
#include <cstring>
#include <algorithm>
#include <cassert>

#define LOGI(...) __android_log_print(ANDROID_LOG_INFO,  "LooperEngine", __VA_ARGS__)
#define LOGE(...) __android_log_print(ANDROID_LOG_ERROR, "LooperEngine", __VA_ARGS__)

// ─────────────────────────────────────────────────────────────────────────────
// Constructor / Destructor
// ─────────────────────────────────────────────────────────────────────────────
LooperEngine::LooperEngine() {
    for (auto& rms : mSnapLayerRMS) rms.store(0.f);
    // El path de sesión raw se define en start() cuando el contexto Android
    // ya ha pasado el filesDir via LooperBridge (JNI).
    mSessionRawPath = "/data/data/com.chamalanquimyst.loopera/files/session_raw.pcm";
}

LooperEngine::~LooperEngine() {
    stop();
}

// ─────────────────────────────────────────────────────────────────────────────
// Apertura de stream Oboe — duplex, LowLatency, Exclusive
// DECISION: se intenta duplex Exclusive primero; si falla, Shared;
//           si falla duplex, el build aborta y muestra error al usuario.
// ─────────────────────────────────────────────────────────────────────────────
bool LooperEngine::openStream() {
    oboe::AudioStreamBuilder builder;
    builder.setDirection(oboe::Direction::Input);  // duplex: Oboe usa el mismo builder

    oboe::AudioStreamBuilder duplexBuilder;
    duplexBuilder
        .setAudioApi(oboe::AudioApi::AAudio)
        .setPerformanceMode(oboe::PerformanceMode::LowLatency)
        .setSharingMode(oboe::SharingMode::Exclusive)
        .setFormat(oboe::AudioFormat::Float)
        .setChannelCount(oboe::ChannelCount::Mono)
        .setDataCallback(this)
        .setErrorCallback(this)
        .setInputPreset(oboe::InputPreset::VoicePerformance) // menor procesamiento DSP del SO
        .setUsage(oboe::Usage::Media);

    // Obtener sample rate nativo del dispositivo
    oboe::DefaultStreamValues::SampleRate = 48000; // fallback razonable para A34
    duplexBuilder.setSampleRate(oboe::kUnspecified); // deja que Oboe detecte el nativo

    oboe::Result result = duplexBuilder.openManagedStream(mStream);

    if (result != oboe::Result::OK) {
        LOGE("Exclusive duplex falló: %s — intentando Shared", oboe::convertToText(result));
        duplexBuilder.setSharingMode(oboe::SharingMode::Shared);
        result = duplexBuilder.openManagedStream(mStream);
        if (result != oboe::Result::OK) {
            LOGE("Shared duplex falló: %s", oboe::convertToText(result));
            return false;
        }
        LOGI("Stream abierto en modo Shared (ver README.md)");
    } else {
        LOGI("Stream abierto en modo Exclusive");
    }

    mSampleRate     = mStream->getSampleRate();
    mFramesPerBurst = mStream->getFramesPerBurst();
    // Doble burst = latencia mínima estable en A34
    mStream->setBufferSizeInFrames(mFramesPerBurst * 2);

    LOGI("SampleRate=%d FramesPerBurst=%d BufferSize=%d",
         mSampleRate, mFramesPerBurst, mFramesPerBurst * 2);

    return true;
}

void LooperEngine::closeStream() {
    if (mStream) {
        mStream->stop();
        mStream->close();
        mStream.reset();
    }
}

// ─────────────────────────────────────────────────────────────────────────────
// start / stop
// ─────────────────────────────────────────────────────────────────────────────
bool LooperEngine::start() {
    if (mRunning.load()) return true;
    if (!openStream()) return false;
    oboe::Result r = mStream->start();
    if (r != oboe::Result::OK) {
        LOGE("mStream->start() falló: %s", oboe::convertToText(r));
        closeStream();
        return false;
    }
    mRunning.store(true);
    LOGI("Motor iniciado — SR=%d RTL=%lld frames", mSampleRate, (long long)mRTLFrames.load());
    return true;
}

void LooperEngine::stop() {
    mRunning.store(false);
    closeStream();
    clearAll(); // libera memoria de capas
}

// ─────────────────────────────────────────────────────────────────────────────
// API thread-safe desde Kotlin
// ─────────────────────────────────────────────────────────────────────────────
void LooperEngine::sendCommand(EngineCmd cmd, int64_t payload) {
    Command c{cmd, payload};
    if (!mCmdRing.push(c)) {
        LOGE("CmdRing lleno — comando descartado: %d", (int)cmd);
    }
}

void LooperEngine::setRTLFrames(int64_t frames) {
    mRTLFrames.store(frames);
    LOGI("RTL seteado: %lld frames (%.2f ms)", (long long)frames,
         frames * 1000.0 / mSampleRate);
}

// ─────────────────────────────────────────────────────────────────────────────
// Snapshot para la UI (30fps aprox)
// ─────────────────────────────────────────────────────────────────────────────
EngineSnapshot LooperEngine::getSnapshot() const {
    EngineSnapshot s;
    s.state      = static_cast<EngineState>(mSnapState.load(std::memory_order_acquire));
    s.layerCount = mSnapLayerCount.load(std::memory_order_acquire);
    s.playhead   = mSnapPlayhead.load(std::memory_order_acquire);
    s.loopLength = mSnapLoopLength.load(std::memory_order_acquire);
    s.masterRMS  = mSnapMasterRMS.load(std::memory_order_acquire);
    s.onset      = mSnapOnset.load(std::memory_order_acquire);
    for (int i = 0; i < s.layerCount && i < MAX_LAYERS; ++i)
        s.layerRMS[i] = mSnapLayerRMS[i].load(std::memory_order_relaxed);
    return s;
}

bool LooperEngine::drainSessionSample(float& sample) {
    return mSessionRing.pop(sample);
}

// ─────────────────────────────────────────────────────────────────────────────
// FSM — solo llamada desde el callback
// ─────────────────────────────────────────────────────────────────────────────
void LooperEngine::drainCommands() {
    Command c;
    while (mCmdRing.pop(c)) {
        handleCommand(c);
    }
}

void LooperEngine::handleCommand(const Command& c) {
    EngineState st = mState.load(std::memory_order_relaxed);
    switch (c.cmd) {
        case EngineCmd::REC_TOGGLE:
            if      (st == EngineState::IDLE)    startRecBase();
            else if (st == EngineState::REC_BASE) finalizeBase();
            else if (st == EngineState::PLAYING) startOverdub();
            else if (st == EngineState::OVERDUB) finalizeLayer();
            break;
        case EngineCmd::STOP_GLOBAL:
            if (st == EngineState::OVERDUB) finalizeLayer();
            if (st == EngineState::REC_BASE) finalizeBase();
            mState.store(EngineState::IDLE, std::memory_order_release);
            break;
        case EngineCmd::UNDO_LAYER:
            undoLastLayer();
            break;
        case EngineCmd::CLEAR_ALL:
            clearAll();
            break;
        case EngineCmd::MUTE_LAYER:
            if (c.payload >= 0 && c.payload < MAX_LAYERS) {
                bool cur = mLayers[c.payload].muted.load(std::memory_order_relaxed);
                mLayers[c.payload].muted.store(!cur, std::memory_order_relaxed);
            }
            break;
        case EngineCmd::RTL_MEASURED:
            mRTLFrames.store(c.payload, std::memory_order_relaxed);
            LOGI("RTL auto-medido: %lld frames", (long long)c.payload);
            break;
        default: break;
    }
}

void LooperEngine::startRecBase() {
    // La longitud de la base se define al cerrarla — aquí reservamos máximo 4 min
    int64_t maxFrames = (int64_t)mSampleRate * 60 * 4;
    mRecBuf.assign(maxFrames, 0.f);
    mRecPos = 0;
    mState.store(EngineState::REC_BASE, std::memory_order_release);
    LOGI("REC_BASE iniciado");
}

void LooperEngine::finalizeBase() {
    if (mRecPos < 512) {
        LOGE("Base demasiado corta (%lld frames) — abortando", (long long)mRecPos);
        mState.store(EngineState::IDLE, std::memory_order_release);
        return;
    }
    mLoopLength = mRecPos;
    mSnapLoopLength.store(mLoopLength, std::memory_order_release);

    // Preasignar todas las capas al mismo loopLength
    for (auto& layer : mLayers) {
        layer.data.assign(mLoopLength, 0.f);
        layer.active.store(false);
        layer.muted.store(false);
    }

    // Capa 0 = base, con compensación RTL
    applyRTLShift(mLayers[0].data, mRecBuf);
    mLayers[0].active.store(true, std::memory_order_release);
    mLayerCount.store(1, std::memory_order_release);
    mPlayhead = 0;
    mRecPos   = 0;
    mUndoLayer = -1;
    mState.store(EngineState::PLAYING, std::memory_order_release);
    LOGI("Base fijada: %lld frames (%.2f s)", (long long)mLoopLength,
         (double)mLoopLength / mSampleRate);
}

void LooperEngine::startOverdub() {
    // RecBuf ya preasignado al tamaño de loopLength
    std::fill(mRecBuf.begin(), mRecBuf.begin() + mLoopLength, 0.f);
    mRecPos = 0;
    mState.store(EngineState::OVERDUB, std::memory_order_release);
    LOGI("OVERDUB iniciado — capa %d", mLayerCount.load());
}

void LooperEngine::finalizeLayer() {
    int idx = mLayerCount.load(std::memory_order_relaxed);
    if (idx >= MAX_LAYERS) {
        LOGE("MAX_LAYERS alcanzado — overdub descartado");
        mState.store(EngineState::PLAYING, std::memory_order_release);
        return;
    }

    // Limitar al loopLength (el usuario puede haber grabado más o menos)
    int64_t recorded = std::min(mRecPos, mLoopLength);

    // Aplicar compensación RTL — shift circular hacia atrás
    // REGLA: una sola vez por capa, nunca re-compensar capas ya fijadas
    applyRTLShift(mLayers[idx].data, mRecBuf);

    mLayers[idx].active.store(true, std::memory_order_release);
    mUndoLayer = idx;
    mLayerCount.store(idx + 1, std::memory_order_release);
    mRecPos = 0;
    mState.store(EngineState::PLAYING, std::memory_order_release);
    LOGI("Capa %d fijada (%lld frames)", idx, (long long)recorded);
}

// ─────────────────────────────────────────────────────────────────────────────
// Compensación RTL — shift circular hacia atrás por mRTLFrames
// src puede tener más frames que loopLength; se wrappea con módulo.
// ─────────────────────────────────────────────────────────────────────────────
void LooperEngine::applyRTLShift(std::vector<float>& dst,
                                  const std::vector<float>& src) {
    int64_t rtl = mRTLFrames.load(std::memory_order_relaxed);
    int64_t len = mLoopLength;
    if (len == 0) return;
    if (rtl <= 0) {
        // Sin compensación — copia directa
        for (int64_t n = 0; n < len; ++n)
            dst[n] = src[n % (int64_t)src.size()];
        return;
    }
    // Shift circular: dst[n] = src[(n + rtl) % len]
    // Significa: la capa grabada se "adelanta" rtl frames en el tiempo → suena alineada
    for (int64_t n = 0; n < len; ++n) {
        int64_t srcIdx = (n + rtl) % len;
        dst[n] = (srcIdx < (int64_t)src.size()) ? src[srcIdx] : 0.f;
    }
}

void LooperEngine::undoLastLayer() {
    if (mUndoLayer >= 0 && mUndoLayer < MAX_LAYERS) {
        bool cur = mLayers[mUndoLayer].muted.load(std::memory_order_relaxed);
        mLayers[mUndoLayer].muted.store(!cur, std::memory_order_relaxed);
        LOGI("Undo toggle capa %d → muted=%d", mUndoLayer, !cur);
    }
}

void LooperEngine::clearAll() {
    mState.store(EngineState::IDLE, std::memory_order_release);
    mLoopLength = 0;
    mPlayhead   = 0;
    mRecPos     = 0;
    mUndoLayer  = -1;
    mLayerCount.store(0, std::memory_order_release);
    for (auto& layer : mLayers) {
        layer.active.store(false, std::memory_order_relaxed);
        layer.muted.store(false, std::memory_order_relaxed);
    }
    mSessionRing.reset();
    mEnvelope = 0.f;
    LOGI("Motor reseteado");
}

// ─────────────────────────────────────────────────────────────────────────────
// ── EL CALLBACK DUPLEX ──────────────────────────────────────────────────────
// Orden de operaciones FIJO (ver Anexo 2.1-BIS del contrato):
//   1) Drenar comandos
//   2) Mezcla de salida (suma de capas activas)
//   3) Soft-limiter en bus máster
//   4) Captura del mic (grabación de capa activa)
//   5) Avance de playhead
//   6) Push al ring de sesión
//   7) Actualizar snapshot atómico (una vez por bloque, no por sample)
// ─────────────────────────────────────────────────────────────────────────────
oboe::DataCallbackResult LooperEngine::onAudioReady(
        oboe::AudioStream* /*stream*/,
        void* inputData,
        void* outputData,
        int32_t numFrames) {

    const float* mic = static_cast<const float*>(inputData);
    float*       spk = static_cast<float*>(outputData);

    // ── 1) Comandos de UI ─────────────────────────────────────────────────
    drainCommands();

    EngineState st         = mState.load(std::memory_order_acquire);
    int         layerCount = mLayerCount.load(std::memory_order_acquire);

    // Variables para snapshot del bloque
    float blockEnergy = 0.f;
    float blockPeak   = 0.f;
    float layerEnergy[MAX_LAYERS]{};

    for (int32_t i = 0; i < numFrames; ++i) {

        // ── 2) MEZCLA DE SALIDA ───────────────────────────────────────────
        float mixed = 0.f;
        if (st == EngineState::PLAYING || st == EngineState::OVERDUB) {
            for (int L = 0; L < layerCount; ++L) {
                if (mLayers[L].active.load(std::memory_order_relaxed) &&
                    !mLayers[L].muted.load(std::memory_order_relaxed)) {
                    float s = mLayers[L].data[mPlayhead];
                    mixed += s;
                    layerEnergy[L] += s * s;
                }
            }
        }

        // ── 3) SOFT-LIMITER (Anexo 2.1-BIS §F) ───────────────────────────
        float out = softLimit(mixed);
        spk[i] = out;

        // Para snapshot
        blockEnergy += out * out;
        if (out > blockPeak) blockPeak = out;

        // ── 4) CAPTURA DEL MIC ────────────────────────────────────────────
        // REGLA: grabamos DESPUÉS de escribir la salida → el timing de captura
        // es coherente con el reloj de reproducción del mismo frame.
        if ((st == EngineState::REC_BASE || st == EngineState::OVERDUB)
            && mRecPos < (int64_t)mRecBuf.size()) {
            mRecBuf[mRecPos++] = mic[i];
            // Auto-cerrar base si llenamos el buffer máximo (4 min)
            if (st == EngineState::REC_BASE && mRecPos >= (int64_t)mRecBuf.size()) {
                handleCommand({EngineCmd::REC_TOGGLE, 0});
                st = mState.load(std::memory_order_acquire);
            }
        }

        // ── 5) AVANCE DEL PLAYHEAD ────────────────────────────────────────
        if (mLoopLength > 0) {
            if (++mPlayhead >= mLoopLength) mPlayhead = 0;
        }

        // ── 6) RING DE SESIÓN (hilo de disco consume esto) ────────────────
        mSessionRing.push(out); // descarta si lleno — ver SpscRing
    }

    // ── 7) SNAPSHOT ATÓMICO (una vez por bloque) ─────────────────────────
    float rms = std::sqrt(blockEnergy / numFrames);
    // Onset: subida brusca por encima del envelope suavizado
    mEnvelope = ENV_RELEASE * mEnvelope + ENV_ATTACK * rms;
    bool onset = (rms > mEnvelope * ONSET_RATIO) && (rms > ONSET_MIN_RMS);

    mSnapMasterRMS.store(rms,         std::memory_order_release);
    mSnapOnset.store(onset,           std::memory_order_release);
    mSnapState.store((int)st,         std::memory_order_release);
    mSnapLayerCount.store(layerCount, std::memory_order_release);
    mSnapPlayhead.store(mPlayhead,    std::memory_order_release);
    mSnapLoopLength.store(mLoopLength,std::memory_order_release);
    for (int L = 0; L < layerCount; ++L) {
        mSnapLayerRMS[L].store(
            std::sqrt(layerEnergy[L] / numFrames),
            std::memory_order_relaxed);
    }

    return oboe::DataCallbackResult::Continue;
}

// ─────────────────────────────────────────────────────────────────────────────
// Error del stream — Oboe llama esto en hilo de audio
// ─────────────────────────────────────────────────────────────────────────────
void LooperEngine::onErrorAfterClose(oboe::AudioStream* /*stream*/, oboe::Result error) {
    LOGE("Stream error: %s — reintentando apertura", oboe::convertToText(error));
    if (mRunning.load()) {
        closeStream();
        if (openStream()) {
            mStream->start();
            LOGI("Stream reabierto tras error");
        }
    }
}
