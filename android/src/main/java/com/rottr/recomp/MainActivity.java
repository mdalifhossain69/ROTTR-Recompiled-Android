package com.rottr.recomp;

import android.Manifest;
import android.app.Activity;
import android.content.pm.PackageManager;
import android.graphics.SurfaceTexture;
import android.os.Build;
import android.os.Bundle;
import android.os.Environment;
import android.util.Log;
import android.view.InputQueue;
import android.view.Surface;
import android.view.TextureView;
import android.view.View;
import android.view.WindowManager;

import androidx.core.app.ActivityCompat;
import androidx.core.content.ContextCompat;

/**
 * Main activity for ROTTR-Recomp.
 * Demonstrates integration of native subsystems with Android lifecycle.
 */
public class MainActivity extends Activity implements TextureView.SurfaceTextureListener {
    private static final String TAG = "ROTTR-Recomp-MainActivity";

    private static final int REQUEST_STORAGE_PERMISSION = 1001;
    private static final String[] STORAGE_PERMISSIONS = {
            Manifest.permission.WRITE_EXTERNAL_STORAGE,
            Manifest.permission.READ_EXTERNAL_STORAGE,
    };

    private SurfaceTexture mSurfaceTexture;
    private Surface mSurface;
    private boolean mSurfaceValid = false;

    @Override
    protected void onCreate(Bundle savedInstanceState) {
        super.onCreate(savedInstanceState);

        // ---- Auto-save logs to SDcard/ROTTR-logs when the app opens ----
        LogUtils.init(this);

        // Request storage permission (needed on Android 6+ to write to /sdcard)
        requestStoragePermission();

        // Keep screen on for game
        getWindow().addFlags(WindowManager.LayoutParams.FLAG_KEEP_SCREEN_ON);
        
        // Hide system UI for immersive experience
        getWindow().getDecorView().setSystemUiVisibility(
            View.SYSTEM_UI_FLAG_IMMERSIVE_STICKY
            | View.SYSTEM_UI_FLAG_FULLSCREEN
            | View.SYSTEM_UI_FLAG_HIDE_NAVIGATION
            | View.SYSTEM_UI_FLAG_LAYOUT_STABLE
            | View.SYSTEM_UI_FLAG_LAYOUT_HIDE_NAVIGATION
            | View.SYSTEM_UI_FLAG_LAYOUT_FULLSCREEN
        );
        
        // Create a SurfaceView or TextureView for rendering
        // For simplicity, we'll use a TextureView
        TextureView textureView = new TextureView(this);
        textureView.setSurfaceTextureListener(this);
        setContentView(textureView);
        
        Log.i(TAG, "Activity created. Log dir: " +
                (Environment.getExternalStorageDirectory() != null
                        ? Environment.getExternalStorageDirectory().getAbsolutePath() + "/ROTTR-logs"
                        : "unavailable"));
    }
    
    @Override
    protected void onResume() {
        super.onResume();
        Log.i(TAG, "onResume");
    }
    
    @Override
    protected void onPause() {
        super.onPause();
        Log.i(TAG, "onPause");
    }
    
    @Override
    protected void onDestroy() {
        super.onDestroy();
        LogUtils.close();
        // Shutdown native subsystems
        NativeBridge.shutdown();
    }
    
    @Override
    public void onSurfaceTextureAvailable(SurfaceTexture surface, int width, int height) {
        Log.i(TAG, "onSurfaceTextureAvailable: " + width + "x" + height);
        mSurfaceTexture = surface;
        mSurface = new Surface(surface);
        mSurfaceValid = true;
        
        // Initialize native with surface dimensions
        NativeBridge.init(width, height);
        
        // Set the surface for rendering
        NativeBridge.setSurface(mSurface);
    }
    
    @Override
    public void onSurfaceTextureSizeChanged(SurfaceTexture surface, int width, int height) {
        Log.i(TAG, "onSurfaceTextureSizeChanged: " + width + "x" + height);
        // Re-initialize with new dimensions
        NativeBridge.init(width, height);
    }
    
    @Override
    public boolean onSurfaceTextureDestroyed(SurfaceTexture surface) {
        Log.i(TAG, "onSurfaceTextureDestroyed");
        NativeBridge.releaseSurface();
        mSurfaceValid = false;
        mSurface = null;
        mSurfaceTexture = null;
        return true;
    }
    
    @Override
    public void onSurfaceTextureUpdated(SurfaceTexture surface) {
        // Called when the surface is updated - could trigger a render frame
    }

    /* ---------------------------------------------------------------- */
    /* Storage permission                                               */
    /* ---------------------------------------------------------------- */
    private void requestStoragePermission() {
        // These permissions are capped at API 33 in the manifest; asking for them
        // on Android 14+ can fail immediately and take the activity down.
        if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.UPSIDE_DOWN_CAKE) {
            LogUtils.i(TAG, "Skipping legacy storage permission request on Android 14+");
            return;
        }

        // Already granted
        if (ContextCompat.checkSelfPermission(this, Manifest.permission.WRITE_EXTERNAL_STORAGE)
                == PackageManager.PERMISSION_GRANTED) {
            LogUtils.i(TAG, "Storage permission already granted");
            return;
        }

        ActivityCompat.requestPermissions(this, STORAGE_PERMISSIONS, REQUEST_STORAGE_PERMISSION);
    }

    @Override
    public void onRequestPermissionsResult(int requestCode,
                                           String[] permissions,
                                           int[] grantResults) {
        super.onRequestPermissionsResult(requestCode, permissions, grantResults);
        if (requestCode == REQUEST_STORAGE_PERMISSION) {
            boolean granted = grantResults.length > 0
                    && grantResults[0] == PackageManager.PERMISSION_GRANTED;
            LogUtils.i(TAG, "Storage permission " + (granted ? "granted" : "denied"));
        }
    }
}