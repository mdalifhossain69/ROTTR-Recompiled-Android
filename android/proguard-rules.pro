# ProGuard rules for ROTTR-Recomp
# Keep native bridge classes and methods

-keep class com.rottr.recomp.NativeBridge {
    native <methods>;
    public static <methods>;
}

-keep class com.rottr.recomp.MainActivity {
    *;
}

# Keep JNI method names
-keepclassmembers class * {
    native <methods>;
}