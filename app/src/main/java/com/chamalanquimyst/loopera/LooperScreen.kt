package com.chamalanquimyst.loopera

import androidx.compose.animation.*
import androidx.compose.animation.core.*
import androidx.compose.foundation.*
import androidx.compose.foundation.layout.*
import androidx.compose.foundation.lazy.LazyColumn
import androidx.compose.foundation.lazy.itemsIndexed
import androidx.compose.foundation.shape.CircleShape
import androidx.compose.foundation.shape.RoundedCornerShape
import androidx.compose.material.icons.Icons
import androidx.compose.material.icons.filled.*
import androidx.compose.material3.*
import androidx.compose.runtime.*
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.draw.clip
import androidx.compose.ui.draw.scale
import androidx.compose.ui.graphics.Brush
import androidx.compose.ui.graphics.Color
import androidx.compose.ui.text.font.FontWeight
import androidx.compose.ui.unit.*

// ─── Paleta ──────────────────────────────────────────────────────────────────
private val BgDeep    = Color(0xFF0A0A0F)
private val BgCard    = Color(0xFF12121A)
private val AccentRed = Color(0xFFE84040)
private val AccentGrn = Color(0xFF40E870)
private val AccentBlu = Color(0xFF4090E8)
private val AccentYel = Color(0xFFE8C040)
private val TextPri   = Color(0xFFEEEEEE)
private val TextSec   = Color(0xFF888899)

// ─────────────────────────────────────────────────────────────────────────────
@Composable
fun LooperScreen(
    vm: LooperViewModel,
    onRequestPermission: () -> Unit
) {
    val snap      by vm.snapshot.collectAsState()
    val ready     by vm.engineReady.collectAsState()
    val exporting by vm.exporting.collectAsState()
    val exportPath by vm.exportPath.collectAsState()
    val errorMsg  by vm.errorMsg.collectAsState()
    val rtlMs     by vm.rtlMs.collectAsState()

    var showCalibration by remember { mutableStateOf(false) }

    LaunchedEffect(Unit) {
        onRequestPermission()
    }

    Surface(color = BgDeep, modifier = Modifier.fillMaxSize()) {
        Column(
            modifier = Modifier
                .fillMaxSize()
                .padding(16.dp),
            horizontalAlignment = Alignment.CenterHorizontally
        ) {

            // ── Header ────────────────────────────────────────────────────
            LooperHeader(state = snap.state, layerCount = snap.layerCount)

            Spacer(Modifier.height(12.dp))

            // ── VU Meter / Progress del loop ──────────────────────────────
            LoopProgress(
                progress  = snap.progress,
                masterRMS = snap.masterRMS,
                onset     = snap.onset,
                state     = snap.state
            )

            Spacer(Modifier.height(20.dp))

            // ── Botón principal REC / OVERDUB ─────────────────────────────
            MainRecButton(
                state   = snap.state,
                enabled = ready && !exporting,
                onClick = { vm.onRecToggle() }
            )

            Spacer(Modifier.height(16.dp))

            // ── Controles secundarios ─────────────────────────────────────
            SecondaryControls(
                state    = snap.state,
                enabled  = ready && !exporting,
                onStop   = { /* caller needs context — passed via vm */ },
                onUndo   = { vm.onUndoLayer() },
                onClear  = { vm.onClearAll() },
                onCalib  = { showCalibration = !showCalibration }
            )

            Spacer(Modifier.height(16.dp))

            // ── Calibración RTL ───────────────────────────────────────────
            AnimatedVisibility(visible = showCalibration) {
                RTLCalibrationCard(
                    currentMs = rtlMs,
                    onValueChange = { vm.setRTLMs(it) }
                )
            }

            Spacer(Modifier.height(12.dp))

            // ── Lista de capas ────────────────────────────────────────────
            if (snap.layerCount > 0) {
                LayerList(
                    layerCount = snap.layerCount,
                    layerRMS   = snap.layerRMS,
                    onMute     = { idx -> vm.onMuteLayer(idx) },
                    modifier   = Modifier.weight(1f)
                )
            } else {
                Spacer(Modifier.weight(1f))
            }

            // ── Exportación / mensajes ────────────────────────────────────
            StatusBar(
                exporting  = exporting,
                exportPath = exportPath,
                errorMsg   = errorMsg
            )
        }
    }
}

// ─────────────────────────────────────────────────────────────────────────────
@Composable
fun LooperHeader(state: Int, layerCount: Int) {
    val stateLabel = when (state) {
        LooperBridge.State.IDLE     -> "IDLE"
        LooperBridge.State.REC_BASE -> "● REC BASE"
        LooperBridge.State.PLAYING  -> "▶ PLAYING"
        LooperBridge.State.OVERDUB  -> "● OVERDUB"
        else -> "—"
    }
    val stateColor = when (state) {
        LooperBridge.State.REC_BASE,
        LooperBridge.State.OVERDUB -> AccentRed
        LooperBridge.State.PLAYING -> AccentGrn
        else -> TextSec
    }

    Row(
        modifier = Modifier.fillMaxWidth(),
        horizontalArrangement = Arrangement.SpaceBetween,
        verticalAlignment = Alignment.CenterVertically
    ) {
        Text(
            text  = "LOOPERA  EJE·Z",
            color = TextPri,
            fontSize = 20.sp,
            fontWeight = FontWeight.Bold,
            letterSpacing = 2.sp
        )
        Column(horizontalAlignment = Alignment.End) {
            Text(text = stateLabel, color = stateColor, fontSize = 12.sp, fontWeight = FontWeight.Bold)
            Text(text = "$layerCount capas", color = TextSec, fontSize = 11.sp)
        }
    }
}

// ─────────────────────────────────────────────────────────────────────────────
@Composable
fun LoopProgress(progress: Float, masterRMS: Float, onset: Boolean, state: Int) {
    val vuScale by animateFloatAsState(
        targetValue = if (onset) 1.08f else 1f,
        animationSpec = spring(stiffness = Spring.StiffnessHigh),
        label = "vu"
    )
    val rmsHeight = (masterRMS * 120f).coerceIn(2f, 120f)

    Box(
        modifier = Modifier
            .fillMaxWidth()
            .height(80.dp)
            .clip(RoundedCornerShape(12.dp))
            .background(BgCard),
        contentAlignment = Alignment.Center
    ) {
        // Barra de progreso del loop
        if (state != LooperBridge.State.IDLE && progress > 0f) {
            Box(
                modifier = Modifier
                    .fillMaxWidth(progress)
                    .fillMaxHeight()
                    .background(
                        Brush.horizontalGradient(listOf(AccentBlu.copy(alpha = 0.25f), AccentBlu.copy(alpha = 0.08f)))
                    )
                    .align(Alignment.CenterStart)
            )
        }
        // VU meter vertical (master RMS)
        Box(
            modifier = Modifier
                .scale(vuScale)
                .width(6.dp)
                .height(rmsHeight.dp)
                .clip(RoundedCornerShape(3.dp))
                .background(
                    if (onset) AccentYel
                    else if (state == LooperBridge.State.OVERDUB || state == LooperBridge.State.REC_BASE) AccentRed
                    else AccentGrn
                )
        )
        // Label de progreso
        if (state == LooperBridge.State.PLAYING || state == LooperBridge.State.OVERDUB) {
            Text(
                text = "${(progress * 100).toInt()}%",
                color = TextSec,
                fontSize = 10.sp,
                modifier = Modifier.align(Alignment.BottomEnd).padding(6.dp)
            )
        }
    }
}

// ─────────────────────────────────────────────────────────────────────────────
@Composable
fun MainRecButton(state: Int, enabled: Boolean, onClick: () -> Unit) {
    val isRecording = state == LooperBridge.State.REC_BASE || state == LooperBridge.State.OVERDUB
    val label = when (state) {
        LooperBridge.State.IDLE     -> "REC BASE"
        LooperBridge.State.REC_BASE -> "CERRAR BASE"
        LooperBridge.State.PLAYING  -> "OVERDUB"
        LooperBridge.State.OVERDUB  -> "CERRAR CAPA"
        else -> "REC"
    }
    val bgColor = if (isRecording) AccentRed else AccentGrn

    // Pulso animado mientras graba
    val pulse by rememberInfiniteTransition(label = "pulse").animateFloat(
        initialValue = 1f,
        targetValue  = if (isRecording) 1.06f else 1f,
        animationSpec = infiniteRepeatable(
            animation = tween(600, easing = FastOutSlowInEasing),
            repeatMode = RepeatMode.Reverse
        ),
        label = "pulseFloat"
    )

    Button(
        onClick  = onClick,
        enabled  = enabled,
        shape    = CircleShape,
        colors   = ButtonDefaults.buttonColors(containerColor = bgColor),
        modifier = Modifier
            .size(140.dp)
            .scale(pulse)
    ) {
        Column(horizontalAlignment = Alignment.CenterHorizontally) {
            Icon(
                imageVector = if (isRecording) Icons.Default.Stop else Icons.Default.FiberManualRecord,
                contentDescription = label,
                tint = Color.White,
                modifier = Modifier.size(36.dp)
            )
            Spacer(Modifier.height(4.dp))
            Text(label, color = Color.White, fontSize = 11.sp, fontWeight = FontWeight.Bold)
        }
    }
}

// ─────────────────────────────────────────────────────────────────────────────
@Composable
fun SecondaryControls(
    state:   Int,
    enabled: Boolean,
    onStop:  () -> Unit,
    onUndo:  () -> Unit,
    onClear: () -> Unit,
    onCalib: () -> Unit
) {
    Row(
        modifier = Modifier.fillMaxWidth(),
        horizontalArrangement = Arrangement.SpaceEvenly
    ) {
        ControlBtn("STOP",  Icons.Default.StopCircle,   AccentYel, enabled && state != LooperBridge.State.IDLE, onStop)
        ControlBtn("UNDO",  Icons.Default.Undo,          AccentBlu, enabled && state != LooperBridge.State.IDLE && state != LooperBridge.State.REC_BASE, onUndo)
        ControlBtn("CLEAR", Icons.Default.DeleteForever, AccentRed.copy(alpha = 0.7f), enabled, onClear)
        ControlBtn("CALIB", Icons.Default.Tune,          TextSec,   true, onCalib)
    }
}

@Composable
fun ControlBtn(label: String, icon: androidx.compose.ui.graphics.vector.ImageVector,
               color: Color, enabled: Boolean, onClick: () -> Unit) {
    Column(horizontalAlignment = Alignment.CenterHorizontally) {
        IconButton(onClick = onClick, enabled = enabled, modifier = Modifier.size(48.dp)) {
            Icon(icon, contentDescription = label,
                tint = if (enabled) color else color.copy(alpha = 0.3f),
                modifier = Modifier.size(28.dp))
        }
        Text(label, color = if (enabled) color else color.copy(alpha = 0.3f),
            fontSize = 9.sp, fontWeight = FontWeight.Bold, letterSpacing = 1.sp)
    }
}

// ─────────────────────────────────────────────────────────────────────────────
@Composable
fun RTLCalibrationCard(currentMs: Float, onValueChange: (Float) -> Unit) {
    Card(
        modifier = Modifier.fillMaxWidth(),
        colors   = CardDefaults.cardColors(containerColor = BgCard),
        shape    = RoundedCornerShape(12.dp)
    ) {
        Column(modifier = Modifier.padding(16.dp)) {
            Text("CALIBRACIÓN RTL", color = AccentYel, fontSize = 12.sp,
                fontWeight = FontWeight.Bold, letterSpacing = 1.sp)
            Spacer(Modifier.height(4.dp))
            Text("Ajusta la compensación de latencia del dispositivo",
                color = TextSec, fontSize = 11.sp)
            Spacer(Modifier.height(8.dp))
            Row(verticalAlignment = Alignment.CenterVertically) {
                Slider(
                    value          = currentMs,
                    onValueChange  = onValueChange,
                    valueRange     = 0f..200f,
                    steps          = 199,
                    modifier       = Modifier.weight(1f),
                    colors         = SliderDefaults.colors(
                        thumbColor        = AccentYel,
                        activeTrackColor  = AccentYel
                    )
                )
                Spacer(Modifier.width(12.dp))
                Text("${currentMs.toInt()} ms", color = AccentYel,
                    fontSize = 14.sp, fontWeight = FontWeight.Bold,
                    modifier = Modifier.width(52.dp))
            }
            Text("Graba un beat → escucha si las capas están alineadas → ajusta hasta alinear",
                color = TextSec, fontSize = 10.sp)
        }
    }
}

// ─────────────────────────────────────────────────────────────────────────────
@Composable
fun LayerList(
    layerCount: Int,
    layerRMS:   FloatArray,
    onMute:     (Int) -> Unit,
    modifier:   Modifier = Modifier
) {
    LazyColumn(modifier = modifier.fillMaxWidth()) {
        item {
            Text("CAPAS", color = TextSec, fontSize = 10.sp,
                fontWeight = FontWeight.Bold, letterSpacing = 1.sp,
                modifier = Modifier.padding(bottom = 6.dp))
        }
        items(layerCount) { idx ->
            val rms   = if (idx < layerRMS.size) layerRMS[idx] else 0f
            val barW  = (rms * 200f).coerceIn(0f, 200f)
            Row(
                modifier = Modifier
                    .fillMaxWidth()
                    .padding(vertical = 3.dp)
                    .clip(RoundedCornerShape(6.dp))
                    .background(BgCard)
                    .padding(horizontal = 10.dp, vertical = 6.dp),
                verticalAlignment = Alignment.CenterVertically
            ) {
                val layerLabel = if (idx == 0) "BASE" else "L${idx + 1}"
                Text(layerLabel, color = if (idx == 0) AccentGrn else AccentBlu,
                    fontSize = 11.sp, fontWeight = FontWeight.Bold,
                    modifier = Modifier.width(36.dp))
                // Mini VU por capa
                Box(
                    modifier = Modifier
                        .weight(1f)
                        .height(8.dp)
                        .clip(RoundedCornerShape(4.dp))
                        .background(Color(0xFF1A1A2A))
                ) {
                    Box(
                        modifier = Modifier
                            .fillMaxWidth(rms.coerceIn(0f, 1f))
                            .fillMaxHeight()
                            .clip(RoundedCornerShape(4.dp))
                            .background(
                                if (idx == 0) AccentGrn.copy(alpha = 0.7f)
                                else AccentBlu.copy(alpha = 0.7f)
                            )
                    )
                }
                Spacer(Modifier.width(8.dp))
                IconButton(onClick = { onMute(idx) }, modifier = Modifier.size(28.dp)) {
                    Icon(Icons.Default.VolumeMute, contentDescription = "Mute capa $idx",
                        tint = TextSec, modifier = Modifier.size(16.dp))
                }
            }
        }
    }
}

// ─────────────────────────────────────────────────────────────────────────────
@Composable
fun StatusBar(exporting: Boolean, exportPath: String?, errorMsg: String?) {
    when {
        exporting -> {
            Row(verticalAlignment = Alignment.CenterVertically) {
                CircularProgressIndicator(
                    color    = AccentYel,
                    modifier = Modifier.size(16.dp),
                    strokeWidth = 2.dp
                )
                Spacer(Modifier.width(8.dp))
                Text("Exportando MP3…", color = AccentYel, fontSize = 12.sp)
            }
        }
        exportPath != null -> {
            Column(horizontalAlignment = Alignment.CenterHorizontally) {
                Text("✓ Sesión exportada", color = AccentGrn,
                    fontSize = 12.sp, fontWeight = FontWeight.Bold)
                Text(exportPath.substringAfterLast("/"),
                    color = TextSec, fontSize = 10.sp)
            }
        }
        errorMsg != null -> {
            Text("⚠ $errorMsg", color = AccentRed, fontSize = 11.sp)
        }
        else -> {
            Text("Chamalanquimyst Labs · Loopera Eje Z",
                color = TextSec.copy(alpha = 0.4f), fontSize = 10.sp,
                letterSpacing = 1.sp)
        }
    }
    Spacer(Modifier.height(8.dp))
}
