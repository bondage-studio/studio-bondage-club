package com.studio.bondageclub

import android.app.Activity
import android.os.Build
import android.os.Bundle
import android.util.Log
import android.view.View
import android.view.WindowInsets
import android.view.WindowInsetsController
import android.webkit.WebResourceRequest
import android.webkit.WebResourceResponse
import android.webkit.WebView
import android.webkit.WebViewClient
import android.widget.Toast
import androidx.webkit.WebViewCompat
import androidx.webkit.WebViewFeature
import java.net.HttpURLConnection
import java.net.URL

class MainActivity : Activity() {

    private lateinit var webView: WebView
    private var localOrigin: String = "http://$HOST:$PORT"

    override fun onCreate(savedInstanceState: Bundle?) {
        super.onCreate(savedInstanceState)

        val address = NativeServer.nativeStart(filesDir.absolutePath, cacheDir.absolutePath, HOST, PORT)
        if (address.isEmpty()) {
            Toast.makeText(this, "Failed to start local server", Toast.LENGTH_LONG).show()
            finish()
            return
        }
        localOrigin = "http://$address"

        WebView.setWebContentsDebuggingEnabled(true)

        webView = WebView(this).apply {
            settings.apply {
                javaScriptEnabled = true
                domStorageEnabled = true        // panel + game use localStorage
                @Suppress("DEPRECATION")
                databaseEnabled = true
                mediaPlaybackRequiresUserGesture = false
                allowContentAccess = true
                // Android routes cross-origin requests through WebViewClient,
                // so the web bundle must not install its service worker.
                userAgentString = "$userAgentString StudioBC-Android"
            }
            webViewClient = LocalProxyWebViewClient(localOrigin)

            // The game canvas fills the viewport and handles its own drag
            // gestures. Kill the WebView's own scroll/over-scroll so a vertical
            // drag isn't stolen to bounce the page instead of dragging in-game.
            overScrollMode = View.OVER_SCROLL_NEVER
            isVerticalScrollBarEnabled = false
            isHorizontalScrollBarEnabled = false
            isNestedScrollingEnabled = false
        }
        setContentView(webView)

        // Must run after setContentView: the window's DecorView (and thus its
        // WindowInsetsController) only exists once content has been installed.
        enableImmersiveMode()

        installRpcBridge(localOrigin)

        webView.loadUrl("$localOrigin/")
    }

    // installRpcBridge wires the native RPC channel: an origin-scoped JS object
    // (window.__sbcNativeRpc) injected at document-start. The web bundle captures
    // it before any userscript runs (nativeBridge.ts) and routes RPC through it
    // instead of the localhost WebSocket. Each inbound frame carries the
    // capability token the C++ core verifies; the replyProxy pushes res/event
    // frames back. If the device WebView is too old to support the feature, we
    // skip it and the web bundle transparently falls back to the WebSocket.
    private fun installRpcBridge(localOrigin: String) {
        if (!WebViewFeature.isFeatureSupported(WebViewFeature.WEB_MESSAGE_LISTENER)) {
            return
        }
        NativeRpc.nativeInit()
        WebViewCompat.addWebMessageListener(
            webView,
            BRIDGE_NAME,
            setOf(localOrigin),
        ) { view, message, _, _, replyProxy ->
            // Capture the page's reply channel for native-initiated pushes (events).
            NativeRpc.onOutbound = { frame -> view.post { replyProxy.postMessage(frame) } }
            NativeRpc.nativeDeliver(message.data ?: "")
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
        if (this::webView.isInitialized) {
            webView.destroy()
        }
        NativeServer.nativeStop()
        super.onDestroy()
    }

    @Deprecated("Deprecated in Java")
    override fun onBackPressed() {
        if (this::webView.isInitialized && webView.canGoBack()) {
            webView.goBack()
        } else {
            @Suppress("DEPRECATION")
            super.onBackPressed()
        }
    }

    companion object {
        const val HOST = "127.0.0.1"
        const val PORT = 8080

        // Must match BRIDGE_NAME in web/src/rpc/nativeBridge.ts.
        private const val BRIDGE_NAME = "__sbcNativeRpc"
    }
}

/**
 * Native equivalent of web/src/service-worker.ts: cross-origin requests are
 * routed through the local server at `/<full-url>` (where the backend caches
 * them); same-origin requests pass straight through to the server.
 */
private class LocalProxyWebViewClient(localOrigin: String) : WebViewClient() {

    private val localOriginUrl = URL(localOrigin)

    override fun shouldInterceptRequest(
        view: WebView,
        request: WebResourceRequest
    ): WebResourceResponse? {
        val uri = request.url
        val method = request.method ?: "GET"

        if (isSameOrigin(uri.scheme, uri.host, uri.port)) {
            return null
        }
        // WebResourceRequest carries no body, so only idempotent requests can be
        // safely re-issued through the proxy. Others fall through to the network.
        if (!method.equals("GET", true) && !method.equals("HEAD", true)) {
            return null
        }

        return try {
            proxyThroughLocalServer(uri.toString(), method, request.requestHeaders)
        } catch (e: Exception) {
            Log.w(TAG, "proxy failed for $uri: ${e.message}")
            null
        }
    }

    private fun isSameOrigin(scheme: String?, host: String?, port: Int): Boolean {
        if (!scheme.equals(localOriginUrl.protocol, true)) return false
        if (!host.equals(localOriginUrl.host, true)) return false
        val effective = if (port == -1) localOriginUrl.defaultPort else port
        return effective == localOriginUrl.port
    }

    private fun proxyThroughLocalServer(
        originalUrl: String,
        method: String,
        requestHeaders: Map<String, String>
    ): WebResourceResponse {
        val loaderUrl = URL(localOriginUrl, "/$originalUrl")
        val conn = (loaderUrl.openConnection() as HttpURLConnection).apply {
            requestMethod = method
            instanceFollowRedirects = true
            connectTimeout = 30_000
            readTimeout = 30_000
            for ((name, value) in requestHeaders) {
                if (name.equals("Host", true)) continue
                setRequestProperty(name, value)
            }
        }

        val status = conn.responseCode
        val reason = conn.responseMessage?.takeIf { it.isNotBlank() } ?: "OK"
        val (mime, encoding) = parseContentType(conn.contentType)

        val responseHeaders = LinkedHashMap<String, String>()
        for ((name, values) in conn.headerFields) {
            if (name == null) continue
            responseHeaders[name] = values.joinToString(", ")
        }
        // WebView receives this as the target URL's response, so fetch() needs CORS.
        responseHeaders["Access-Control-Allow-Origin"] = "*"

        val body = if (status >= 400) conn.errorStream else conn.inputStream
        return WebResourceResponse(mime, encoding, status, reason, responseHeaders, body)
    }

    private fun parseContentType(contentType: String?): Pair<String, String?> {
        if (contentType.isNullOrBlank()) return "application/octet-stream" to null
        var mime = "application/octet-stream"
        var charset: String? = null
        for ((i, part) in contentType.split(';').withIndex()) {
            val token = part.trim()
            if (i == 0) {
                if (token.isNotEmpty()) mime = token
            } else if (token.startsWith("charset=", true)) {
                charset = token.substring("charset=".length).trim().trim('"').ifBlank { null }
            }
        }
        return mime to charset
    }

    private companion object {
        const val TAG = "StudioBC"
    }
}
