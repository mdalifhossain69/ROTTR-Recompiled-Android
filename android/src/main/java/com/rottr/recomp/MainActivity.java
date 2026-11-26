package com.rottr.recomp;

import android.app.Activity;
import android.app.InputQueue;
import android.graphics.SurfaceTexture;
import android.os.Bundle;
import android.util.Log;
import android.view.Surface;
import android.view.View;
import android.view.WindowManager;

/**
 * Main activity for ROTTR-Recomp.
 * Demonstrates integration of native subsystems with Android lifecycle.
 */
public class MainActivity extends Activity implements SurfaceTextureListener {
    private static final String TAG = "ROTTR-Recomp-MainActivity";
    
    private SurfaceTexture mSurfaceTexture;
    private Surface mSurface;
    private boolean mSurfaceValid = false;
    
    @Override
    protected void onCreate(Bundle savedInstanceState) {
        super.onCreate(savedInstanceState);
        
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
        
        Log.i(TAG, "Activity created");
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
        Log.i(TAG, "onDestroy");
        // Shutdown native subsystems
        NativeBridge.shutdown();
    }
    
    @Override
    public void onInputQueueCreated(InputQueue queue) {
        super.onInputQueueCreated(queue);
        Log.i(TAG, "onInputQueueCreated: " + queue);
        NativeBridge.setInputQueue(queue);
    }
    
    @Override
    public void onInputQueueDestroyed(InputQueue queue) {
        super.onInputQueueDestroyed(queue);
        Log.i(TAG, "onInputQueueDestroyed");
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
}