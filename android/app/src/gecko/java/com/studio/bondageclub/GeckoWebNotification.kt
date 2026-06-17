package com.studio.bondageclub

import android.app.Notification
import android.app.NotificationChannel
import android.app.NotificationManager
import android.app.PendingIntent
import android.content.Context
import org.mozilla.geckoview.WebNotification
import org.mozilla.geckoview.WebNotificationDelegate

// GeckoView raises Web Notifications (the page's Notification API) only through a
// WebNotificationDelegate on the runtime; with none set they are dropped. The
// game uses them to flag chat activity while backgrounded, so we mirror each one
// into a real Android notification. onCloseNotification fires when the page (or
// the user, via the page) revokes a notification, so we cancel the Android one to
// match. Use the application context — the runtime outlives any single activity.
//
// Posting needs the runtime POST_NOTIFICATIONS grant on Android 13+ (requested in
// MainActivity); without it NotificationManager.notify silently no-ops rather
// than throwing, so this stays safe either way.
class GeckoWebNotification(context: Context) : WebNotificationDelegate {

    private val appContext = context.applicationContext
    private val manager =
        appContext.getSystemService(Context.NOTIFICATION_SERVICE) as NotificationManager

    init {
        // minSdk for the gecko flavor is 26, so a channel always exists.
        manager.createNotificationChannel(
            NotificationChannel(CHANNEL_ID, CHANNEL_NAME, NotificationManager.IMPORTANCE_DEFAULT)
        )
    }

    override fun onShowNotification(notification: WebNotification) {
        val launch = appContext.packageManager.getLaunchIntentForPackage(appContext.packageName)
        val contentIntent = launch?.let {
            PendingIntent.getActivity(appContext, 0, it, PendingIntent.FLAG_IMMUTABLE)
        }

        val builder = Notification.Builder(appContext, CHANNEL_ID)
            .setSmallIcon(appContext.applicationInfo.icon)
            .setContentTitle(notification.title)
            .setContentText(notification.text)
            .setAutoCancel(true)
        if (contentIntent != null) builder.setContentIntent(contentIntent)

        manager.notify(keyFor(notification), NOTIFY_ID, builder.build())
    }

    override fun onCloseNotification(notification: WebNotification) {
        manager.cancel(keyFor(notification), NOTIFY_ID)
    }

    // The page's notification tag is the natural dedup/replace key (a new
    // notification with the same tag replaces the old). Tagless notifications get
    // a per-instance key so concurrent ones don't collide; close matches because
    // GeckoView hands back the same WebNotification instance.
    private fun keyFor(notification: WebNotification): String {
        val tag = notification.tag
        return if (!tag.isNullOrEmpty()) tag else "sbc-anon-" + System.identityHashCode(notification)
    }

    private companion object {
        const val CHANNEL_ID = "web-notifications"
        const val CHANNEL_NAME = "Game notifications"

        // Notifications are distinguished by their string tag (the notify() tag),
        // so a single fixed numeric id is enough.
        const val NOTIFY_ID = 1
    }
}
