package com.chamalanquimyst.loopera

import android.Manifest
import android.content.Intent
import android.content.pm.PackageManager
import android.os.Bundle
import androidx.activity.ComponentActivity
import androidx.activity.compose.setContent
import androidx.activity.result.contract.ActivityResultContracts
import androidx.activity.viewModels
import androidx.compose.runtime.*
import androidx.core.content.ContextCompat

class MainActivity : ComponentActivity() {

    private val vm: LooperViewModel by viewModels()

    // Solicitud de permiso de micrófono
    private val requestMicPermission = registerForActivityResult(
        ActivityResultContracts.RequestPermission()
    ) { granted ->
        if (granted) {
            vm.startEngine(this)
        }
        // Si denegado: la UI muestra el error vía _errorMsg
    }

    override fun onCreate(savedInstanceState: Bundle?) {
        super.onCreate(savedInstanceState)

        setContent {
            LooperaTheme {
                LooperScreen(
                    vm = vm,
                    onRequestPermission = { checkAndRequestPermission() }
                )
            }
        }
    }

    private fun checkAndRequestPermission() {
        when {
            ContextCompat.checkSelfPermission(
                this, Manifest.permission.RECORD_AUDIO
            ) == PackageManager.PERMISSION_GRANTED -> {
                vm.startEngine(this)
            }
            else -> requestMicPermission.launch(Manifest.permission.RECORD_AUDIO)
        }
    }

    override fun onStop() {
        super.onStop()
        // El audio continúa en background via LooperForegroundService
    }

    override fun onDestroy() {
        // Detener el foreground service al cerrar la activity definitivamente
        stopService(Intent(this, LooperForegroundService::class.java))
        super.onDestroy()
    }
}
