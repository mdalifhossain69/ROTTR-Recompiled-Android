package com.rottr.recomp;

import android.util.Log;
import android.view.InputQueue;
import android.view.Surface;

/** JNI bridge for ROTTR-Recomp native subsystems. */
public class NativeBridge {
    private static final String TAG = "ROTTR-Recomp-NativeBridge";
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

    public static boolean isNativeAvailable() {
        return sNativeAvailable;
    }

    public static native void nativeInit(int width, int height);
    public static native void nativeSetSurface(Surface surface);
    public static native void nativeReleaseSurface();
    public static native void nativeSetInputQueue(InputQueue inputQueue);
    public static native void nativeShutdown();

    public static void init(int width, int height) {
        if (!sNativeAvailable) {
            Log.e(TAG, "Skipping nativeInit: library unavailable");
            return;
        }
        Log.i(TAG, "Initializing native subsystems: " + width + "x" + height);
        nativeInit(width, height);
    }

    public static void setSurface(Surface surface) {
        if (!sNativeAvailable) {
            Log.e(TAG, "Skipping nativeSetSurface: library unavailable");
            return;
        }
        Log.i(TAG, "Setting native surface: " + surface);
        nativeSetSurface(surface);
    }

    public static void releaseSurface() {
        if (!sNativeAvailable) return;
        Log.i(TAG, "Releasing native surface");
        nativeReleaseSurface();
    }

    public static void setInputQueue(InputQueue inputQueue) {
        if (!sNativeAvailable) return;
        Log.i(TAG, "Setting native input queue: " + inputQueue);
        nativeSetInputQueue(inputQueue);
    }

    public static void shutdown() {
        if (!sNativeAvailable) return;
        Log.i(TAG, "Shutting down native subsystems");
        nativeShutdown();
    }
}
