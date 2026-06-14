# Keep the JNI entry points so R8 never renames/strips the methods the native
# library binds to by name.
-keepclasseswithmembernames class com.studio.bondageclub.NativeServer {
    native <methods>;
}
