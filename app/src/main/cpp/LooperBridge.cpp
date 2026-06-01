#include <jni.h>
#include <string>
#include <memory>
#include "LooperEngine.h"
#include "LameEncoder.h"
#include <android/log.h>

#define LOGI(...) __android_log_print(ANDROID_LOG_INFO,  "LooperBridge", __VA_ARGS__)
#define LOGE(...) __android_log_print(ANDROID_LOG_ERROR, "LooperBridge", __VA_ARGS__)

// Instancia global del motor (singleton, vida ligada a la Activity)
static std::unique_ptr<LooperEngine> gEngine;

// ─── Hilo de escritura de sesión ──────────────────────────────────────────
#include <thread>
#include <atomic>
#include <cstdio>

static std::thread       gSessionThread;
static std::atomic<bool> gSessionRunning{false};
static FILE*             gSessionFile = nullptr;

static void sessionWriterLoop(const std::string& path) {
    gSessionFile = fopen(path.c_str(), "wb");
    if (!gSessionFile) {
        LOGE("No se pudo abrir session file: %s", path.c_str());
        return;
    }
    float sample;
    while (gSessionRunning.load()) {
        if (gEngine && gEngine->drainSessionSample(sample)) {
            fwrite(&sample, sizeof(float), 1, gSessionFile);
        } else {
            // Pausa corta para no quemar CPU cuando el ring está vacío
            std::this_thread::sleep_for(std::chrono::microseconds(500));
        }
    }
    // Flush final
    if (gEngine) {
        float s;
        while (gEngine->drainSessionSample(s))
            fwrite(&s, sizeof(float), 1, gSessionFile);
    }
    fclose(gSessionFile);
    gSessionFile = nullptr;
    LOGI("SessionWriter terminado");
}

// ─────────────────────────────────────────────────────────────────────────────
// JNI Functions
// Paquete: com.chamalanquimyst.loopera
// ─────────────────────────────────────────────────────────────────────────────
extern "C" {

// Crear e iniciar el motor
JNIEXPORT jboolean JNICALL
Java_com_chamalanquimyst_loopera_LooperBridge_nativeStart(
        JNIEnv* env, jobject /*thiz*/, jstring filesDir) {

    const char* dir = env->GetStringUTFChars(filesDir, nullptr);
    std::string sessionPath = std::string(dir) + "/session_raw.pcm";
    env->ReleaseStringUTFChars(filesDir, dir);

    if (!gEngine) gEngine = std::make_unique<LooperEngine>();

    bool ok = gEngine->start();
    if (!ok) {
        LOGE("gEngine->start() falló");
        return JNI_FALSE;
    }

    // Iniciar hilo de escritura de sesión
    gSessionRunning.store(true);
    gSessionThread = std::thread(sessionWriterLoop, sessionPath);

    LOGI("Motor + SessionWriter iniciados");
    return JNI_TRUE;
}

// Detener y liberar recursos
JNIEXPORT void JNICALL
Java_com_chamalanquimyst_loopera_LooperBridge_nativeStop(
        JNIEnv* /*env*/, jobject /*thiz*/) {
    gSessionRunning.store(false);
    if (gSessionThread.joinable()) gSessionThread.join();
    if (gEngine) {
        gEngine->stop();
        gEngine.reset();
    }
    LOGI("Motor detenido y liberado");
}

// Enviar comando (desde botones de la UI)
JNIEXPORT void JNICALL
Java_com_chamalanquimyst_loopera_LooperBridge_nativeSendCommand(
        JNIEnv* /*env*/, jobject /*thiz*/, jint cmd, jlong payload) {
    if (gEngine) gEngine->sendCommand(static_cast<EngineCmd>(cmd), payload);
}

// Obtener snapshot como array de floats/ints para Kotlin
// Formato del array retornado (12 + MAX_LAYERS floats):
// [0] state (int cast a float)
// [1] layerCount
// [2] playhead (normalizado 0..1)
// [3] masterRMS
// [4] onset (0 o 1)
// [5..5+layerCount-1] layerRMS por capa
JNIEXPORT jfloatArray JNICALL
Java_com_chamalanquimyst_loopera_LooperBridge_nativeGetSnapshot(
        JNIEnv* env, jobject /*thiz*/) {

    if (!gEngine) return env->NewFloatArray(0);

    EngineSnapshot s = gEngine->getSnapshot();
    const int size = 5 + MAX_LAYERS;
    jfloat buf[size];
    buf[0] = (float)s.state;
    buf[1] = (float)s.layerCount;
    buf[2] = (s.loopLength > 0) ? (float)s.playhead / s.loopLength : 0.f;
    buf[3] = s.masterRMS;
    buf[4] = s.onset ? 1.f : 0.f;
    for (int i = 0; i < MAX_LAYERS; ++i)
        buf[5 + i] = s.layerRMS[i];

    jfloatArray arr = env->NewFloatArray(size);
    env->SetFloatArrayRegion(arr, 0, size, buf);
    return arr;
}

// Setear RTL desde calibración manual (Kotlin)
JNIEXPORT void JNICALL
Java_com_chamalanquimyst_loopera_LooperBridge_nativeSetRTL(
        JNIEnv* /*env*/, jobject /*thiz*/, jlong rtlFrames) {
    if (gEngine) gEngine->setRTLFrames(rtlFrames);
}

// Obtener sample rate nativo
JNIEXPORT jint JNICALL
Java_com_chamalanquimyst_loopera_LooperBridge_nativeGetSampleRate(
        JNIEnv* /*env*/, jobject /*thiz*/) {
    return gEngine ? gEngine->getSampleRate() : 48000;
}

// Exportar sesión a MP3 (llamar DESPUÉS de nativeStop o al Stop global)
// sessionPcmPath y mp3OutPath son rutas completas
JNIEXPORT jboolean JNICALL
Java_com_chamalanquimyst_loopera_LooperBridge_nativeExportMP3(
        JNIEnv* env, jobject /*thiz*/,
        jstring sessionPcmPath, jstring mp3OutPath, jint sampleRate) {

    const char* pcm = env->GetStringUTFChars(sessionPcmPath, nullptr);
    const char* mp3 = env->GetStringUTFChars(mp3OutPath, nullptr);

    bool ok = LameEncoder::encodePCMtoMP3(pcm, mp3, sampleRate, 192);

    env->ReleaseStringUTFChars(sessionPcmPath, pcm);
    env->ReleaseStringUTFChars(mp3OutPath, mp3);
    return ok ? JNI_TRUE : JNI_FALSE;
}

} // extern "C"
