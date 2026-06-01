package com.chamalanquimyst.loopera

import android.app.*
import android.content.Intent
import android.os.IBinder
import androidx.core.app.NotificationCompat

// ─────────────────────────────────────────────────────────────────────────────
// LooperForegroundService — mantiene el proceso vivo mientras hay una sesión
// activa. Android mata el audio de background si no hay un foreground service.
// REGLA del contrato: obligatorio.
// ─────────────────────────────────────────────────────────────────────────────
class LooperForegroundService : Service() {

    companion object {
        const val CHANNEL_ID   = "loopera_audio_channel"
        const val NOTIFICATION_ID = 1001
    }

    override fun onCreate() {
        super.onCreate()
        createNotificationChannel()
    }

    override fun onStartCommand(intent: Intent?, flags: Int, startId: Int): Int {
        val notification = buildNotification()
        startForeground(NOTIFICATION_ID, notification)
        return START_STICKY
    }

    override fun onBind(intent: Intent?): IBinder? = null

    private fun createNotificationChannel() {
        val channel = NotificationChannel(
            CHANNEL_ID,
            "Loopera Eje Z — Sesión de audio",
            NotificationManager.IMPORTANCE_LOW
        ).apply {
            description = "Mantiene la grabación activa en background"
            setSound(null, null)
        }
        getSystemService(NotificationManager::class.java)
            .createNotificationChannel(channel)
    }

    private fun buildNotification(): Notification {
        val pendingIntent = PendingIntent.getActivity(
            this, 0,
            Intent(this, MainActivity::class.java),
            PendingIntent.FLAG_IMMUTABLE
        )
        return NotificationCompat.Builder(this, CHANNEL_ID)
            .setContentTitle("Loopera Eje Z")
            .setContentText("Sesión de audio activa")
            .setSmallIcon(android.R.drawable.ic_media_play)
            .setContentIntent(pendingIntent)
            .setOngoing(true)
            .setSilent(true)
            .build()
    }
}
