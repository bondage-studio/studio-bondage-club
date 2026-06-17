package com.studio.bondageclub

import android.app.Activity
import android.content.pm.ApplicationInfo
import android.os.Build
import android.os.Bundle
import android.view.View
import android.view.WindowInsets
import android.view.WindowInsetsController
import android.widget.Toast
import java.io.File
import org.json.JSONObject
import org.mozilla.geckoview.GeckoResult
import org.mozilla.geckoview.GeckoRuntime
import org.mozilla.geckoview.GeckoRuntimeSettings
import org.mozilla.geckoview.GeckoSession
import org.mozilla.geckoview.GeckoView
import org.mozilla.geckoview.WebExtension

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

        // GeckoView renders no UI for HTML prompts on its own; without this the
        // game's <select> dropdowns can't be opened. See GeckoViewPrompt.
        session.promptDelegate = GeckoViewPrompt(this)

        // Auto-grant the Notification permission so the game can raise Web
        // Notifications (mirrored to Android notifications by GeckoWebNotification,
        // wired on the runtime below). This is a local single-purpose app, so the
        // user installing it is consent enough; other permission types fall
        // through to GeckoView's default handling (returning null).
        session.permissionDelegate = object : GeckoSession.PermissionDelegate {
            override fun onContentPermissionRequest(
                session: GeckoSession,
                perm: GeckoSession.PermissionDelegate.ContentPermission,
            ): GeckoResult<Int>? {
                return if (perm.permission ==
                    GeckoSession.PermissionDelegate.PERMISSION_DESKTOP_NOTIFICATION
                ) {
                    GeckoResult.fromValue(
                        GeckoSession.PermissionDelegate.ContentPermission.VALUE_ALLOW
                    )
                } else {
                    null
                }
            }
        }

        // Android 13+ gates posting notifications behind a runtime grant; request
        // it once so GeckoWebNotification's posts actually surface.
        requestPostNotificationsIfNeeded()

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

        installRpcBridge(runtime) { session.loadUri("$localOrigin/") }
    }

    // installRpcBridge registers the built-in WebExtension that exposes the native
    // RPC bridge to the page (assets/extensions/sbc-rpc). Its content script opens
    // a native-messaging port; we forward each frame to the C++ core and push
    // res/event frames back over the port. The page is loaded only once the
    // extension is registered, so window.__sbcNativeRpc exists before the bundle
    // runs; if registration fails we load anyway and fall back to the WebSocket.
    private fun installRpcBridge(runtime: GeckoRuntime, loadPage: () -> Unit) {
        runtime.webExtensionController
            .ensureBuiltIn(EXTENSION_URI, EXTENSION_ID)
            .accept(
                { extension ->
                    if (extension != null) {
                        NativeRpc.nativeInit()
                        extension.setMessageDelegate(
                            object : WebExtension.MessageDelegate {
                                override fun onConnect(port: WebExtension.Port) {
                                    NativeRpc.onOutbound = { frame ->
                                        runOnUiThread {
                                            try {
                                                port.postMessage(JSONObject().put("d", frame))
                                            } catch (e: Exception) {
                                                // Port closed; the page's RPCs time out.
                                            }
                                        }
                                    }
                                    port.setDelegate(
                                        object : WebExtension.PortDelegate {
                                            override fun onPortMessage(
                                                message: Any,
                                                port: WebExtension.Port,
                                            ) {
                                                if (message is String) {
                                                    NativeRpc.nativeDeliver(message)
                                                }
                                            }

                                            override fun onDisconnect(port: WebExtension.Port) {
                                                NativeRpc.onOutbound = null
                                            }
                                        }
                                    )
                                }
                            },
                            "browser",
                        )
                    }
                    loadPage()
                },
                { loadPage() },
            )
    }

    private fun requestPostNotificationsIfNeeded() {
        if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.TIRAMISU) {
            val granted = checkSelfPermission(android.Manifest.permission.POST_NOTIFICATIONS) ==
                android.content.pm.PackageManager.PERMISSION_GRANTED
            if (!granted) {
                requestPermissions(arrayOf(android.Manifest.permission.POST_NOTIFICATIONS), 0)
            }
        }
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
        NativeRpc.onOutbound = null
        NativeRpc.nativeReset()
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

        // Built-in RPC bridge extension, bundled under gecko/assets/extensions.
        private const val EXTENSION_URI = "resource://android/assets/extensions/sbc-rpc/"
        private const val EXTENSION_ID = "sbc-rpc@studio.bondageclub"

        // GeckoRuntime is a per-process singleton; reuse it across activity
        // re-creations so we never spin up a second Gecko process.
        @Volatile
        private var runtime: GeckoRuntime? = null

        private fun sharedRuntime(activity: Activity): GeckoRuntime {
            return runtime ?: synchronized(this) {
                runtime ?: run {
                    val debuggable =
                        (activity.applicationInfo.flags and ApplicationInfo.FLAG_DEBUGGABLE) != 0

                    val builder = GeckoRuntimeSettings.Builder()
                        // Dev-only: remote debugging (about:debugging from desktop
                        // Firefox) and console mirroring both pipe data across the
                        // IPC boundary, so keep them out of release builds.
                        .remoteDebuggingEnabled(debuggable)
                        .consoleOutput(debuggable)

                    // GPU acceleration (WebRender + accelerated 2D canvas) is
                    // opt-out via config.android.hardwareAcceleration. Gecko reads
                    // prefs from a YAML file at startup; on release builds
                    // (android:debuggable=false) the default location is ignored,
                    // so configFilePath must point at a file we control. An empty
                    // path disables all config file I/O (engine defaults).
                    if (NativeServer.nativeHardwareAccelerationEnabled()) {
                        builder.configFilePath(writeGeckoPrefs(activity).absolutePath)
                    } else {
                        builder.configFilePath("")
                    }

                    GeckoRuntime.create(activity.applicationContext, builder.build())
                        .also {
                            // Mirror the page's Web Notifications into Android
                            // notifications. Set on the per-process runtime, so it
                            // survives activity re-creation alongside the runtime.
                            it.webNotificationDelegate =
                               GeckoWebNotification(activity.applicationContext)
                            runtime = it
                        }
                }
            }
        }

        // Writes the GeckoView config file that forces GPU acceleration. WebRender
        // is GPU compositing; gfx.canvas.accelerated puts the game's 2D canvas on
        // the GPU; software.opengl keeps the software-WebRender fallback on OpenGL.
        // Format: https://firefox-source-docs.mozilla.org/mobile/android/geckoview/consumer/automation.html
        private fun writeGeckoPrefs(activity: Activity): File {
            val file = File(activity.filesDir, "geckoview-config.yaml")
            file.writeText(
                """
                prefs:
                  gfx.webrender.all: true
                  gfx.webrender.software.opengl: true
                  gfx.canvas.accelerated: true
                """.trimIndent() + "\n"
            )
            return file
        }
    }
}
