# Loopera Eje Z — Chamalanquimyst Labs

Loop station Android de baja latencia con compensación activa de RTL.

## Stack
- **Motor de audio**: C++17 + Oboe 1.8.1 → AAudio modo Exclusive/LowLatency
- **Encoder MP3**: LAME (compilado desde fuente vía NDK)
- **UI**: Kotlin + Jetpack Compose + Material 3
- **Target**: Samsung Galaxy A34, minSdk 29

---

## Arquitectura del motor (puntos clave del contrato)

### Stream duplex (Anexo 2.1-BIS)
Un único stream Oboe en modo duplex comparte el mismo reloj de hardware para captura y reproducción. Esto elimina la deriva entre capas que afecta a las loopers comerciales.

**DECISION**: Si el A34 no soporta `Exclusive` duplex, se reintenta en `Shared`. Documentar modo activo aquí tras prueba real en dispositivo.

### Compensación RTL
Al cerrar cada capa, `applyRTLShift()` desplaza el buffer grabado **circularmente hacia atrás** por `RTL_frames`:

```
dst[n] = src[(n + RTL) % loopLength]
```

Esto alinea matemáticamente la nueva capa con la base sin recomputar capas anteriores.

**Medición RTL en Samsung A34**: _pendiente de prueba en dispositivo real — completar aquí_

Valor inicial recomendado para auriculares con cable en A34: ~60–90 ms.

### Soft-limiter (bus máster)
```cpp
if (x > 0.708f) return 0.708f + 0.292f * tanh((x - 0.708f) / 0.292f);
```
Evita clipping al acumular 6+ capas sin reducir el headroom de capas individuales.

---

## Instalación y build

### Prerrequisitos
- Android Studio Hedgehog o superior
- NDK r25c o superior
- CMake 3.22.1+

### LAME (paso manual requerido)
Las fuentes de LAME no se incluyen por licencia. Descarga LAME 3.100:
```bash
wget https://sourceforge.net/projects/lame/files/lame/3.100/lame-3.100.tar.gz
tar -xzf lame-3.100.tar.gz
cp -r lame-3.100 app/src/main/cpp/lame
```
El `CMakeLists.txt` espera las fuentes en `app/src/main/cpp/lame/`.

### Build
```bash
./gradlew assembleDebug
```
APK en: `app/build/outputs/apk/debug/app-debug.apk`

### Instalar en A34
```bash
adb install app/build/outputs/apk/debug/app-debug.apk
```

---

## Flujo de uso

1. **REC BASE** → graba mientras tocas → **CERRAR BASE** fija el loop
2. **OVERDUB** → graba capa encima → **CERRAR CAPA** → repite infinitamente
3. **UNDO** → silencia/reactiva la última capa
4. **STOP** → detiene todo y exporta la sesión completa a MP3
5. MP3 guardado en: `Android/data/com.chamalanquimyst.loopera/files/loopera_session_YYYYMMDD_HHmmss.mp3`

---

## Calibración RTL

Si las capas suenan "atrás del beat", abre el panel de calibración (botón CALIB) y ajusta el slider hasta que las capas queden alineadas. El valor en ms se convierte a frames internamente según el sample rate nativo del dispositivo.

**Rango típico**: 40–120 ms con auriculares con cable. Bluetooth puede requerir 150–200 ms.

---

## Limitaciones conocidas

- Bluetooth tiene latencia variable; la compensación RTL es fija por sesión. Para máxima precisión usar auriculares con cable.
- La auto-medición RTL por chirp no está implementada en v1.0 — solo calibración manual.
- LAME debe compilarse desde fuente (no se distribuye el binario por licencia LGPL).

---

## DECISIONS (log del contrato)

| Decisión | Valor | Razón |
|---|---|---|
| `SharingMode` | Exclusive con fallback a Shared | Spec del contrato |
| `PerformanceMode` | LowLatency | Spec del contrato |
| `InputPreset` | VoicePerformance | Mínimo DSP del SO sobre el mic |
| Encoder MP3 | LAME vía NDK | Calidad > MediaCodec para sesiones largas |
| RTL automático | No implementado v1.0 | Se requiere chirp + correlación cruzada; añadir en v1.1 |
| Buffer size | 2 × framesPerBurst | Balance latencia/estabilidad en Exynos 1280 |

---

*Chamalanquimyst Labs · Andrés Prado · Wild Medicine*
