package com.studio.bondageclub

import android.app.Activity
import android.os.Bundle
import android.util.Log
import android.webkit.WebResourceRequest
import android.webkit.WebResourceResponse
import android.webkit.WebView
import android.webkit.WebViewClient
import android.widget.Toast
import java.net.HttpURLConnection
import java.net.URL

class MainActivity : Activity() {

    private lateinit var webView: WebView
    private var localOrigin: String = "http://$HOST:$PORT"

    override fun onCreate(savedInstanceState: Bundle?) {
        super.onCreate(savedInstanceState)

        // SBC_CONFIG_DIR / SBC_CACHE_DIR land in the app's private storage.
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
                // The native UA token tells originalPage.ts to skip the service
                // worker and rely on this hook instead.
                userAgentString = "$userAgentString StudioBC-Android"
            }
            webViewClient = LocalProxyWebViewClient(localOrigin)
        }
        setContentView(webView)
        webView.loadUrl("$localOrigin/")
    }

    override fun onDestroy() {
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
    }
}

/**
 * Native equivalent of web/src/service-worker.ts: cross-origin requests are
 * routed through the local server at `/<full-url>` (where the backend caches
 * them); same-origin requests pass straight through to the server.
 */
private class LocalProxyWebViewClient(localOrigin: String) : WebViewClient() {

    // e.g. "http://127.0.0.1:8080"
    private val localOriginUrl = URL(localOrigin)

    override fun shouldInterceptRequest(
        view: WebView,
        request: WebResourceRequest
    ): WebResourceResponse? {
        val uri = request.url
        val method = request.method ?: "GET"

        // Same-origin (game assets, /api, /socket.io): let the WebView load it.
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
            null // fall back to a direct network load
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
        // Mirror toRemoteLoaderURL: "<origin>/<full original url>".
        val loaderUrl = URL(localOriginUrl, "/$originalUrl")
        val conn = (loaderUrl.openConnection() as HttpURLConnection).apply {
            requestMethod = method
            instanceFollowRedirects = true
            connectTimeout = 30_000
            readTimeout = 30_000
            for ((name, value) in requestHeaders) {
                if (name.equals("Host", true)) continue // managed by the connection
                setRequestProperty(name, value)
            }
        }

        val status = conn.responseCode
        val reason = conn.responseMessage?.takeIf { it.isNotBlank() } ?: "OK"
        val (mime, encoding) = parseContentType(conn.contentType)

        val responseHeaders = LinkedHashMap<String, String>()
        for ((name, values) in conn.headerFields) {
            if (name == null) continue // the HTTP status line has a null key
            responseHeaders[name] = values.joinToString(", ")
        }
        // The response is delivered as if it came from the original cross-origin
        // URL, so make it CORS-safe for fetch()-mode loads.
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
