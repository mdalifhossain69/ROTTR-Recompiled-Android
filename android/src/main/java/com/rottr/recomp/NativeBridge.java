package com.rottr.recomp;

import android.util.Log;
import android.view.Surface;

/** JNI bridge to the native Vulkan renderer. Call from the UI thread. */
public final class NativeBridge {
    private static final String TAG = "ROTTR-NativeBridge";
    private static final boolean sNativeAvailable;

    static {
        boolean loaded;
        try {
            System.loadLibrary("rottr_android");
            loaded = true;
        } catch (UnsatisfiedLinkError error) {
            Log.e(TAG, "Failed to load librottr_android.so", error);
            loaded = false;
        }
        sNativeAvailable = loaded;
        Log.i(TAG, "Native library available: " + sNativeAvailable);
    }

    private NativeBridge() {}

    public static boolean isNativeAvailable() { return sNativeAvailable; }

    /** Starts Vulkan rendering on the surface. Returns false if Vulkan could not start. */
    public static boolean setSurface(Surface surface) {
        return sNativeAvailable && nativeSetSurface(surface);
    }

    /** Stops rendering. The surface is no longer used once this returns. */
    public static void releaseSurface() {
        if (sNativeAvailable) nativeReleaseSurface();
    }

    /** Lights one block on the boot screen for each stage that really completed. */
    public static void setStagesDone(int count) {
        if (sNativeAvailable) nativeSetStagesDone(count);
    }

    /**
     * Read-only header check of an open Windows executable; safe on a background thread.
     * The first line of the result is "1" if it passed, otherwise "0".
     */
    public static String inspectExe(int fd) {
        return sNativeAvailable ? nativeInspectExe(fd) : "0\nNative library unavailable\n";
    }

    private static native String nativeInspectExe(int fd);
    private static native boolean nativeSetSurface(Surface surface);
    private static native void nativeReleaseSurface();
    private static native void nativeSetStagesDone(int count);
}
