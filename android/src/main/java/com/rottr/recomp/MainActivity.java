package com.rottr.recomp;

import android.app.Activity;
import android.app.AlertDialog;
import android.content.Intent;
import android.net.Uri;
import android.graphics.SurfaceTexture;
import android.os.Bundle;
import android.os.Environment;
import android.provider.Settings;
import android.util.Log;
import android.view.Surface;
import android.view.TextureView;
import android.view.View;
import android.view.WindowManager;

/** Main launcher. Keeps the startup-crash guard from main and records every render boundary. */
public class MainActivity extends Activity implements TextureView.SurfaceTextureListener {
    private static final String TAG = "ROTTR-MainActivity";
    private Surface surface;
    private boolean storagePromptShown;

    @Override
    protected void onCreate(Bundle savedInstanceState) {
        super.onCreate(savedInstanceState);

        // Initialize diagnostics before touching the native bridge.
        LogUtils.init(this);
        LogUtils.i(TAG, "onCreate entered");
        LogUtils.i(TAG, "Android " + android.os.Build.VERSION.RELEASE
                + " / SDK " + android.os.Build.VERSION.SDK_INT);
        LogUtils.i(TAG, "Native library loaded: " + NativeBridge.isNativeAvailable());

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
            LogUtils.i(TAG, "TextureView installed; waiting for SurfaceTexture");
        } catch (Throwable error) {
            LogUtils.exception("MainActivity.onCreate", error);
            throw new RuntimeException("Launcher initialization failed", error);
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
        if (LogUtils.isRootStorageAvailable()) {
            boolean moved = LogUtils.migrateToRootStorage(this);
            LogUtils.i(TAG, "Root diagnostics storage ready; migration=" + moved
                    + ", file=" + LogUtils.getLogFile());
        } else {
            LogUtils.i(TAG, "All-files access not granted; root log folder unavailable");
            showRootStoragePrompt();
        }
    }

    private void showRootStoragePrompt() {
        if (storagePromptShown || isFinishing()) return;
        storagePromptShown = true;
        new AlertDialog.Builder(this)
                .setTitle("Enable ROTTR crash logs")
                .setMessage("To save diagnostics exactly at Internal storage/ROTTR-logs (not Android/data), allow ROTTR-Recomp to access all files. Android will open the system permission screen.")
                .setPositiveButton("Open settings", (dialog, which) -> {
                    try {
                        Intent intent = new Intent(Settings.ACTION_MANAGE_APP_ALL_FILES_ACCESS_PERMISSION);
                        intent.setData(Uri.parse("package:" + getPackageName()));
                        startActivity(intent);
                        LogUtils.i(TAG, "Opened Android all-files-access settings");
                    } catch (Throwable error) {
                        LogUtils.exception("open all-files-access settings", error);
                    }
                })
                .setNegativeButton("Not now", (dialog, which) ->
                        LogUtils.i(TAG, "User deferred public root log permission"))
                .show();
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
        }
        if (surface != null) {
            surface.release();
            surface = null;
        }
        LogUtils.close();
        super.onDestroy();
    }

    @Override
    public void onSurfaceTextureAvailable(SurfaceTexture texture, int width, int height) {
        LogUtils.i(TAG, "Surface available: " + width + "x" + height);
        try {
            surface = new Surface(texture);
            if (!NativeBridge.isNativeAvailable()) {
                LogUtils.e(TAG, "BLACK SCREEN: librottr_android did not load; native rendering cannot start");
                return;
            }
            LogUtils.i(TAG, "Calling NativeBridge.init");
            NativeBridge.init(width, height);
            LogUtils.i(TAG, "Calling NativeBridge.setSurface");
            NativeBridge.setSurface(surface);
            LogUtils.i(TAG, "Native surface initialization calls returned");
        } catch (Throwable error) {
            LogUtils.exception("surface initialization", error);
        }
    }

    @Override
    public void onSurfaceTextureSizeChanged(SurfaceTexture texture, int width, int height) {
        LogUtils.i(TAG, "Surface size changed: " + width + "x" + height);
        try {
            if (NativeBridge.isNativeAvailable()) NativeBridge.init(width, height);
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
        // Avoid per-frame log spam; native frame/render diagnostics are captured via logcat.
    }

    @Override
    public void onWindowFocusChanged(boolean hasFocus) {
        super.onWindowFocusChanged(hasFocus);
        LogUtils.i(TAG, "Window focus changed: " + hasFocus);
    }
}
