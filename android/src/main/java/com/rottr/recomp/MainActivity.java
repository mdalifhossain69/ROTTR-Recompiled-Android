package com.rottr.recomp;

import android.app.Activity;
import android.graphics.SurfaceTexture;
import android.os.Bundle;
import android.util.Log;
import android.view.Surface;
import android.view.TextureView;
import android.view.View;
import android.view.WindowManager;

/** Android launcher activity with startup and native-boundary diagnostics. */
public class MainActivity extends Activity implements TextureView.SurfaceTextureListener {
    private static final String TAG = "ROTTR-MainActivity";
    private Surface surface;

    @Override
    protected void onCreate(Bundle savedInstanceState) {
        super.onCreate(savedInstanceState);
        LogUtils.init(this);
        LogUtils.i(TAG, "onCreate entered");
        try {
            getWindow().addFlags(WindowManager.LayoutParams.FLAG_KEEP_SCREEN_ON);
            getWindow().getDecorView().setSystemUiVisibility(
                    View.SYSTEM_UI_FLAG_IMMERSIVE_STICKY
                    | View.SYSTEM_UI_FLAG_FULLSCREEN
                    | View.SYSTEM_UI_FLAG_HIDE_NAVIGATION
                    | View.SYSTEM_UI_FLAG_LAYOUT_STABLE
                    | View.SYSTEM_UI_FLAG_LAYOUT_HIDE_NAVIGATION
                    | View.SYSTEM_UI_FLAG_LAYOUT_FULLSCREEN);

            TextureView textureView = new TextureView(this);
            textureView.setSurfaceTextureListener(this);
            setContentView(textureView);
            LogUtils.i(TAG, "Launcher UI created; waiting for render surface");
        } catch (Throwable error) {
            LogUtils.exception("MainActivity.onCreate", error);
            throw error;
        }
    }

    @Override
    protected void onStart() {
        super.onStart();
        LogUtils.i(TAG, "onStart");
    }

    @Override
    protected void onResume() {
        super.onResume();
        LogUtils.i(TAG, "onResume");
    }

    @Override
    protected void onPause() {
        LogUtils.i(TAG, "onPause");
        super.onPause();
    }

    @Override
    protected void onStop() {
        LogUtils.i(TAG, "onStop");
        super.onStop();
    }

    @Override
    protected void onDestroy() {
        LogUtils.i(TAG, "onDestroy");
        try {
            NativeBridge.shutdown();
        } catch (Throwable error) {
            LogUtils.exception("NativeBridge.shutdown", error);
        } finally {
            if (surface != null) {
                surface.release();
                surface = null;
            }
            LogUtils.close();
            super.onDestroy();
        }
    }

    @Override
    public void onSurfaceTextureAvailable(SurfaceTexture texture, int width, int height) {
        LogUtils.i(TAG, "Surface available: " + width + "x" + height);
        try {
            surface = new Surface(texture);
            NativeBridge.init(width, height);
            NativeBridge.setSurface(surface);
            LogUtils.i(TAG, "Native initialization and surface binding returned");
        } catch (Throwable error) {
            LogUtils.exception("surface initialization", error);
            Log.e(TAG, "Native initialization failed", error);
        }
    }

    @Override
    public void onSurfaceTextureSizeChanged(SurfaceTexture texture, int width, int height) {
        LogUtils.i(TAG, "Surface size changed: " + width + "x" + height);
        try {
            NativeBridge.init(width, height);
        } catch (Throwable error) {
            LogUtils.exception("surface resize", error);
        }
    }

    @Override
    public boolean onSurfaceTextureDestroyed(SurfaceTexture texture) {
        LogUtils.i(TAG, "Surface destroyed");
        try {
            NativeBridge.releaseSurface();
        } catch (Throwable error) {
            LogUtils.exception("surface release", error);
        }
        if (surface != null) {
            surface.release();
            surface = null;
        }
        return true;
    }

    @Override
    public void onSurfaceTextureUpdated(SurfaceTexture texture) {
        // Frame callbacks are intentionally not logged to avoid flooding diagnostics.
    }
}
