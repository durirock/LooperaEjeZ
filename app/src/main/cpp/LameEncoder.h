#pragma once
#include <string>
#include <vector>

// ─────────────────────────────────────────────────────────────────────────────
// LameEncoder — convierte PCM float mono a MP3 usando LAME
// Llamado desde el hilo de exportación en Kotlin (vía JNI) al Stop global.
// REGLA: no llamar desde el callback de audio.
// ─────────────────────────────────────────────────────────────────────────────
class LameEncoder {
public:
    // rawPcmPath  : archivo PCM float32 mono grabado durante la sesión
    // mp3OutPath  : destino del MP3 final
    // sampleRate  : tasa nativa del motor (ej: 48000)
    // bitrate     : kbps (mínimo 192 según spec)
    // Devuelve true si la codificación fue exitosa
    static bool encodePCMtoMP3(
        const std::string& rawPcmPath,
        const std::string& mp3OutPath,
        int sampleRate,
        int bitrate = 192
    );
};
