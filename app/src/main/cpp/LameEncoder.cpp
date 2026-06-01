#include "LameEncoder.h"
#include <lame/lame.h>
#include <cstdio>
#include <cstring>
#include <android/log.h>

#define LOGI(...) __android_log_print(ANDROID_LOG_INFO,  "LameEncoder", __VA_ARGS__)
#define LOGE(...) __android_log_print(ANDROID_LOG_ERROR, "LameEncoder", __VA_ARGS__)

bool LameEncoder::encodePCMtoMP3(
        const std::string& rawPcmPath,
        const std::string& mp3OutPath,
        int sampleRate,
        int bitrate) {

    // ── Abrir archivo PCM de entrada ─────────────────────────────────────
    FILE* pcmFile = fopen(rawPcmPath.c_str(), "rb");
    if (!pcmFile) {
        LOGE("No se pudo abrir PCM: %s", rawPcmPath.c_str());
        return false;
    }

    // ── Abrir archivo MP3 de salida ──────────────────────────────────────
    FILE* mp3File = fopen(mp3OutPath.c_str(), "wb");
    if (!mp3File) {
        LOGE("No se pudo crear MP3: %s", mp3OutPath.c_str());
        fclose(pcmFile);
        return false;
    }

    // ── Configurar LAME ──────────────────────────────────────────────────
    lame_t lame = lame_init();
    if (!lame) {
        LOGE("lame_init falló");
        fclose(pcmFile);
        fclose(mp3File);
        return false;
    }

    lame_set_num_channels(lame, 1);           // mono
    lame_set_in_samplerate(lame, sampleRate);
    lame_set_out_samplerate(lame, sampleRate);
    lame_set_brate(lame, bitrate);            // 192 kbps CBR
    lame_set_mode(lame, MONO);
    lame_set_quality(lame, 2);               // 0=mejor calidad, 9=peor; 2 = alta calidad

    if (lame_init_params(lame) < 0) {
        LOGE("lame_init_params falló");
        lame_close(lame);
        fclose(pcmFile);
        fclose(mp3File);
        return false;
    }

    // ── Codificación por bloques ─────────────────────────────────────────
    const int PCM_BLOCK  = 8192;  // samples por bloque
    const int MP3_BUF    = (int)(1.25 * PCM_BLOCK + 7200); // recomendación LAME

    std::vector<float>         pcmBuf(PCM_BLOCK);
    std::vector<unsigned char> mp3Buf(MP3_BUF);

    int64_t totalPCM  = 0;
    int64_t totalMP3  = 0;
    size_t  samplesRead = 0;

    while ((samplesRead = fread(pcmBuf.data(), sizeof(float), PCM_BLOCK, pcmFile)) > 0) {
        // LAME espera int16 por canal; convertimos float→float directamente
        // usando lame_encode_buffer_ieee_float (disponible en LAME ≥ 3.98)
        int encoded = lame_encode_buffer_ieee_float(
            lame,
            pcmBuf.data(),  // canal izquierdo (mono)
            nullptr,         // canal derecho (null para mono)
            (int)samplesRead,
            mp3Buf.data(),
            mp3Buf.size()
        );

        if (encoded < 0) {
            LOGE("lame_encode_buffer error: %d", encoded);
            break;
        }
        if (encoded > 0) {
            fwrite(mp3Buf.data(), 1, encoded, mp3File);
            totalMP3 += encoded;
        }
        totalPCM += samplesRead;
    }

    // ── Flush final (frames remanentes en el encoder) ────────────────────
    int flushed = lame_encode_flush(lame, mp3Buf.data(), mp3Buf.size());
    if (flushed > 0) {
        fwrite(mp3Buf.data(), 1, flushed, mp3File);
        totalMP3 += flushed;
    }

    // ── Escribir tag ID3 opcional ─────────────────────────────────────────
    lame_mp3_tags_fid(lame, mp3File);

    LOGI("Exportación completa: %.2f s → %lld bytes MP3 (%d kbps)",
         (double)totalPCM / sampleRate,
         (long long)totalMP3,
         bitrate);

    lame_close(lame);
    fclose(pcmFile);
    fclose(mp3File);
    return true;
}
