package com.studio.bondageclub

object NativeServer {
    init {
        System.loadLibrary("sbc_jni")
    }

    external fun nativeStart(configDir: String, cacheDir: String, host: String, port: Int): String

    external fun nativeStop()

    external fun nativeHardwareAccelerationEnabled(): Boolean
}
