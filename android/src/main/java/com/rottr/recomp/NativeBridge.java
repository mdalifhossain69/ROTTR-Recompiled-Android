package com.rottr.recomp;

import android.graphics.SurfaceTexture;
import android.util.Log;
import android.view.InputQueue;
import android.view.Surface;

/**
 * JNI bridge for ROTTR-Recomp native subsystems.
 * Provides Java/Kotlin access to native graphics, audio, input, and video initialization.
 */
public class NativeBridge {
    private static final String TAG = "ROTTR-Recomp-NativeBridge";

    private static final boolean sNativeAvailable;\n\n    public static boolean isNativeAvailable() {\n        return sNativeAvailable;\n    }

    static {
        boolean loaded;
        try {
            System.loadLibrary("rottr_android");
            loaded = true;
        } catch (UnsatisfiedLinkError e) {
            Log.e(TAG, "Failed to load librottr_android.so", e);
            loaded = false;
        }
        sNativeAvailable = loaded;
    }
    
    // Native methods
    public static native void nativeInit(int width, int height);
    public static native void nativeSetSurface(Surface surface);
    public static native void nativeReleaseSurface();
    public static native void nativeSetInputQueue(InputQueue inputQueue);
    public static native void nativeShutdown();
    
    /**
     * Initialize native subsystems with screen dimensions.
     * Call this from Activity.onCreate() or when the surface is first created.
     */
    public static void init(int width, int height) {
        if (!sNativeAvailable) return;
        Log.i(TAG, "Initializing native subsystems: " + width + "x" + height);
        nativeInit(width, height);
    }
    
    /**
     * Set the native window surface for rendering.
     * Call this from SurfaceTextureListener.onSurfaceTextureAvailable() or similar.
     */
    public static void setSurface(Surface surface) {
        if (!sNativeAvailable) return;
        Log.i(TAG, "Setting native surface: " + surface);
        nativeSetSurface(surface);
    }
    
    /**
     * Release the native window surface.
     * Call this from SurfaceTextureListener.onSurfaceTextureDestroyed() or similar.
     */
    public static void releaseSurface() {
        if (!sNativeAvailable) return;
        Log.i(TAG, "Releasing native surface");
        nativeReleaseSurface();
    }
    
    /**
     * Set the input queue for handling touch/key events.
     * Call this from Activity.onInputQueueCreated().
     */
    public static void setInputQueue(InputQueue inputQueue) {
        if (!sNativeAvailable) return;
        Log.i(TAG, "Setting native input queue: " + inputQueue);
        nativeSetInputQueue(inputQueue);
    }
    
    /**
     * Shutdown native subsystems.
     * Call this from Activity.onDestroy().
     */
    public static void shutdown() {
        if (!sNativeAvailable) return;
        Log.i(TAG, "Shutting down native subsystems");
        nativeShutdown();
    }
}