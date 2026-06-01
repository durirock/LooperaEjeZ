#pragma once
#include <oboe/Oboe.h>
#include <atomic>
#include <vector>
#include <array>
#include <memory>
#include <functional>
#include <cmath>
#include "SpscRing.h"

// ─────────────────────────────────────────────────────────────────────────────
// Constantes del motor
// ─────────────────────────────────────────────────────────────────────────────
static constexpr int    MAX_LAYERS      = 64;
static constexpr int    CMD_RING_SIZE   = 256;
static constexpr int    SESSION_RING_SIZE = 65536; // ~1.3s a 48kHz — hilo de disco drena antes
static constexpr float  ONSET_RATIO     = 1.5f;
static constexpr float  ONSET_MIN_RMS   = 0.02f;
static constexpr float  ENV_ATTACK      = 0.05f;
static constexpr float  ENV_RELEASE     = 0.95f;
static constexpr float  SOFT_LIMIT_TH   = 0.708f; // ~-3 dBFS

// ─────────────────────────────────────────────────────────────────────────────
// FSM de estados
// ─────────────────────────────────────────────────────────────────────────────
enum class EngineState : int {
    IDLE      = 0,
    REC_BASE  = 1,
    PLAYING   = 2,
    OVERDUB   = 3
};

// ─────────────────────────────────────────────────────────────────────────────
// Comandos de la UI → motor (lock-free)
// ─────────────────────────────────────────────────────────────────────────────
enum class EngineCmd : int {
    NONE        = 0,
    REC_TOGGLE  = 1,  // Rec/Overdub según estado actual
    STOP_GLOBAL = 2,  // Detiene todo, exporta sesión
    UNDO_LAYER  = 3,  // Elimina la última capa (toggle muted)
    CLEAR_ALL   = 4,  // Reset completo
    MUTE_LAYER  = 5,  // payload = índice de capa
    RTL_MEASURED= 6,  // payload = RTL en frames (resultado de auto-medición)
};

struct Command {
    EngineCmd cmd     = EngineCmd::NONE;
    int64_t   payload = 0;
};

// ─────────────────────────────────────────────────────────────────────────────
// Capa de audio
// ─────────────────────────────────────────────────────────────────────────────
struct Layer {
    std::vector<float>  data;          // loopLength frames, preasignado
    std::atomic<bool>   muted{false};
    std::atomic<bool>   active{false};
};

// ─────────────────────────────────────────────────────────────────────────────
// Estado observable para la UI (actualizado desde el hilo de audio,
// leído desde la UI — solo floats atómicos o snapshot periódico)
// ─────────────────────────────────────────────────────────────────────────────
struct EngineSnapshot {
    EngineState state       = EngineState::IDLE;
    int         layerCount  = 0;
    int64_t     playhead    = 0;
    int64_t     loopLength  = 0;
    float       masterRMS   = 0.f;
    bool        onset       = false;
    float       layerRMS[MAX_LAYERS]{};
};

// ─────────────────────────────────────────────────────────────────────────────
// LooperEngine — implementa AudioStreamDataCallback (duplex)
// ─────────────────────────────────────────────────────────────────────────────
class LooperEngine : public oboe::AudioStreamDataCallback,
                     public oboe::AudioStreamErrorCallback {
public:
    LooperEngine();
    ~LooperEngine();

    // Ciclo de vida
    bool  start();
    void  stop();

    // API desde la UI (thread-safe — escribe en el ring de comandos)
    void  sendCommand(EngineCmd cmd, int64_t payload = 0);

    // Snapshot periódico para la UI (llamado desde el hilo principal ~30fps)
    EngineSnapshot getSnapshot() const;

    // RTL: la UI llama esto cuando el usuario termina calibración manual
    void  setRTLFrames(int64_t frames);

    // Hilo de sesión: drain del ring de sesión → escribe WAV crudo
    // Lo llama SessionWriter (hilo separado en Kotlin/JNI)
    bool  drainSessionSample(float& sample);

    // Finalización: llamado desde Kotlin al Stop global
    // Devuelve la ruta donde quedó el WAV crudo (luego Kotlin encoda a MP3)
    std::string getSessionRawPath() const { return mSessionRawPath; }
    int32_t     getSampleRate()     const { return mSampleRate; }

    // Callback duplex de Oboe — NO llamar directamente
    oboe::DataCallbackResult onAudioReady(
        oboe::AudioStream* stream,
        void* inputData,
        void* outputData,
        int32_t numFrames) override;

    void onErrorAfterClose(oboe::AudioStream* stream, oboe::Result error) override;

private:
    // ── Apertura de stream ────────────────────────────────────────────────
    bool openStream();
    void closeStream();

    // ── FSM interna (solo desde el callback) ────────────────────────────
    void drainCommands();
    void handleCommand(const Command& cmd);
    void startRecBase();
    void finalizeBase();
    void startOverdub();
    void finalizeLayer();
    void undoLastLayer();
    void clearAll();

    // ── Compensación RTL (aplica shift circular) ─────────────────────────
    void applyRTLShift(std::vector<float>& dst, const std::vector<float>& src);

    // ── Soft-limiter inline ───────────────────────────────────────────────
    static inline float softLimit(float x) noexcept {
        constexpr float TH  = SOFT_LIMIT_TH;
        constexpr float INV = 1.0f / (1.0f - TH);
        if (x >  TH) return  TH + (1.0f - TH) * std::tanh((x -  TH) * INV);
        if (x < -TH) return -TH - (1.0f - TH) * std::tanh((-x - TH) * INV);
        return x;
    }

    // ── Stream Oboe ───────────────────────────────────────────────────────
    std::shared_ptr<oboe::AudioStream> mStream;
    int32_t  mSampleRate      = 48000;
    int32_t  mFramesPerBurst  = 256;

    // ── Estado del motor (solo accedido desde el callback, excepto snapshot) ──
    std::atomic<EngineState> mState{EngineState::IDLE};

    // Capas
    std::array<Layer, MAX_LAYERS> mLayers;
    std::atomic<int>  mLayerCount{0};
    int               mUndoLayer = -1; // índice del last overdub para undo

    // Loop
    int64_t  mLoopLength  = 0;
    int64_t  mPlayhead    = 0;

    // Grabación activa
    std::vector<float> mRecBuf;
    int64_t            mRecPos = 0;

    // RTL
    std::atomic<int64_t> mRTLFrames{0};

    // Envelope para onset
    float mEnvelope = 0.0f;

    // Snapshot atómico (UI lee esto)
    mutable std::atomic<float> mSnapMasterRMS{0.f};
    mutable std::atomic<int>   mSnapLayerCount{0};
    mutable std::atomic<int64_t> mSnapPlayhead{0};
    mutable std::atomic<int64_t> mSnapLoopLength{0};
    mutable std::atomic<int>   mSnapState{0};
    mutable std::atomic<bool>  mSnapOnset{false};
    // layerRMS — array de atomics
    std::atomic<float> mSnapLayerRMS[MAX_LAYERS];

    // ── Rings lock-free ───────────────────────────────────────────────────
    SpscRing<Command> mCmdRing{CMD_RING_SIZE};
    SpscRing<float>   mSessionRing{SESSION_RING_SIZE};

    // ── Sesión raw ────────────────────────────────────────────────────────
    std::string mSessionRawPath;

    // ── Flag de actividad ─────────────────────────────────────────────────
    std::atomic<bool> mRunning{false};
};
