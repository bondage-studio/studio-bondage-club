package com.studio.bondageclub

import android.app.Activity
import android.os.Build
import android.os.Bundle
import android.view.View
import android.view.WindowInsets
import android.view.WindowInsetsController
import android.widget.Toast
import org.mozilla.geckoview.GeckoRuntime
import org.mozilla.geckoview.GeckoRuntimeSettings
import org.mozilla.geckoview.GeckoSession
import org.mozilla.geckoview.GeckoView

/**
 * GeckoView host: the bundled-browser flavor. Unlike the system-WebView flavor
 * (src/system/.../MainActivity.kt), the rendering engine ships inside the APK, so
 * it is immune to the device's (possibly stale) Android System WebView.
 *
 * Cross-origin asset caching is handled by the web bundle's own service worker —
 * NOT by a native shouldInterceptRequest hook. GeckoView is full Firefox and
 * supports service workers; `http://127.0.0.1` is a secure context, so the SW
 * registers and routes cross-origin requests through the local cache server,
 * exactly like the desktop build.
 */
class MainActivity : Activity() {

    private lateinit var geckoView: GeckoView
    private lateinit var session: GeckoSession
    private var canGoBack = false

    override fun onCreate(savedInstanceState: Bundle?) {
        super.onCreate(savedInstanceState)

        val address = NativeServer.nativeStart(filesDir.absolutePath, cacheDir.absolutePath, HOST, PORT)
        if (address.isEmpty()) {
            Toast.makeText(this, "Failed to start local server", Toast.LENGTH_LONG).show()
            finish()
            return
        }
        val localOrigin = "http://$address"

        val runtime = sharedRuntime(this)

        // LOAD-BEARING: we keep GeckoView's native Firefox user agent and do NOT
        // append the "StudioBC-Android" tag. That tag is the signal the web
        // bundle uses to *skip* its service worker (web/src/lib/platform.ts ->
        // isAndroidRuntime, the only consumer). Here we want the SW to register
        // and do the cross-origin proxying, so the tag must stay absent.
        session = GeckoSession()
        session.open(runtime)

        session.navigationDelegate = object : GeckoSession.NavigationDelegate {
            override fun onCanGoBack(session: GeckoSession, value: Boolean) {
                canGoBack = value
            }
        }

        geckoView = GeckoView(this)
        geckoView.setSession(session)
        setContentView(geckoView)

        // Must run after setContentView: the DecorView (and its
        // WindowInsetsController) only exists once content has been installed.
        enableImmersiveMode()

        session.loadUri("$localOrigin/")
    }

    override fun onWindowFocusChanged(hasFocus: Boolean) {
        super.onWindowFocusChanged(hasFocus)

        if (hasFocus) {
            enableImmersiveMode()
        }
    }

    private fun enableImmersiveMode() {
        if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.R) {
            window.setDecorFitsSystemWindows(false)
            window.insetsController?.apply {
                hide(WindowInsets.Type.systemBars())
                systemBarsBehavior =
                    WindowInsetsController.BEHAVIOR_SHOW_TRANSIENT_BARS_BY_SWIPE
            }
        } else {
            @Suppress("DEPRECATION")
            window.decorView.systemUiVisibility = (
                View.SYSTEM_UI_FLAG_LAYOUT_STABLE
                    or View.SYSTEM_UI_FLAG_LAYOUT_HIDE_NAVIGATION
                    or View.SYSTEM_UI_FLAG_LAYOUT_FULLSCREEN
                    or View.SYSTEM_UI_FLAG_HIDE_NAVIGATION
                    or View.SYSTEM_UI_FLAG_FULLSCREEN
                    or View.SYSTEM_UI_FLAG_IMMERSIVE_STICKY
                )
        }
    }

    override fun onDestroy() {
        if (this::session.isInitialized) {
            session.close()
        }
        NativeServer.nativeStop()
        super.onDestroy()
    }

    @Deprecated("Deprecated in Java")
    override fun onBackPressed() {
        if (canGoBack) {
            session.goBack()
        } else {
            @Suppress("DEPRECATION")
            super.onBackPressed()
        }
    }

    companion object {
        const val HOST = "127.0.0.1"
        const val PORT = 8080

        // GeckoRuntime is a per-process singleton; reuse it across activity
        // re-creations so we never spin up a second Gecko process.
        @Volatile
        private var runtime: GeckoRuntime? = null

        private fun sharedRuntime(activity: Activity): GeckoRuntime {
            return runtime ?: synchronized(this) {
                runtime ?: GeckoRuntime.create(
                    activity.applicationContext,
                    GeckoRuntimeSettings.Builder()
                        // Inspect from desktop Firefox via about:debugging.
                        .remoteDebuggingEnabled(true)
                        .consoleOutput(true)
                        .build()
                ).also { runtime = it }
            }
        }
    }
}
