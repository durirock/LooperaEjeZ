package com.chamalanquimyst.loopera

import androidx.compose.material3.*
import androidx.compose.runtime.Composable
import androidx.compose.ui.graphics.Color

private val DarkColorScheme = darkColorScheme(
    primary         = Color(0xFF40E870),
    onPrimary       = Color(0xFF0A0A0F),
    secondary       = Color(0xFF4090E8),
    onSecondary     = Color(0xFF0A0A0F),
    tertiary        = Color(0xFFE8C040),
    background      = Color(0xFF0A0A0F),
    surface         = Color(0xFF12121A),
    onBackground    = Color(0xFFEEEEEE),
    onSurface       = Color(0xFFEEEEEE),
    error           = Color(0xFFE84040),
    onError         = Color(0xFFFFFFFF),
)

@Composable
fun LooperaTheme(content: @Composable () -> Unit) {
    MaterialTheme(
        colorScheme = DarkColorScheme,
        content     = content
    )
}
