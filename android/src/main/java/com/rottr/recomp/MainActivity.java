package com.rottr.recomp;

import android.app.Activity;
import android.app.AlertDialog;
import android.content.Intent;
import android.net.Uri;
import android.os.Bundle;
import android.os.Environment;
import android.provider.Settings;
import android.util.Log;
import android.view.View;
import android.view.WindowManager;
import android.widget.FrameLayout;
import android.widget.Toast;

/** Main Activity hosts the first-run launcher and native render surface lifecycle. */
public class MainActivity extends Activity implements android.view.TextureView.SurfaceTextureListener {
    private static final String TAG = "ROTTR-MainActivity";
    private static final int REQUEST_GAME_FOLDER = 4101;
    private static final String PREFS = "rottr_launcher";
    private Uri gameFolder;
    private LauncherScreen launcher;
    private android.view.TextureView textureView;
    private android.view.Surface surface;
    private boolean storagePromptShown;
    private boolean runtimeSurfaceVisible;

    @Override protected void onCreate(Bundle savedInstanceState) {
        super.onCreate(savedInstanceState);
        LogUtils.init(this);
        LogUtils.i(TAG, "onCreate entered; native library loaded=" + NativeBridge.isNativeAvailable());
        getWindow().addFlags(WindowManager.LayoutParams.FLAG_KEEP_SCREEN_ON);
        hideSystemBars();
        String saved = getSharedPreferences(PREFS, MODE_PRIVATE).getString("game_folder", null);
        if (saved != null) gameFolder = Uri.parse(saved);
        // Draw into the camera-cutout area too, so there is no black bar in landscape.
        WindowManager.LayoutParams window = getWindow().getAttributes();
        window.layoutInDisplayCutoutMode =
                WindowManager.LayoutParams.LAYOUT_IN_DISPLAY_CUTOUT_MODE_SHORT_EDGES;
        getWindow().setAttributes(window);
        showLauncher();
        // Without this access, logs cannot go to Internal storage/ROTTR-logs.
        if (!LogUtils.isRootStorageAvailable()) showRootStoragePrompt();
    }

    @Override public void onWindowFocusChanged(boolean hasFocus) {
        super.onWindowFocusChanged(hasFocus);
        if (hasFocus) hideSystemBars();
    }

    private void hideSystemBars() {
        getWindow().getDecorView().setSystemUiVisibility(
                View.SYSTEM_UI_FLAG_IMMERSIVE_STICKY | View.SYSTEM_UI_FLAG_FULLSCREEN
                | View.SYSTEM_UI_FLAG_HIDE_NAVIGATION | View.SYSTEM_UI_FLAG_LAYOUT_STABLE
                | View.SYSTEM_UI_FLAG_LAYOUT_HIDE_NAVIGATION | View.SYSTEM_UI_FLAG_LAYOUT_FULLSCREEN);
    }

    private void showLauncher() {
        runtimeSurfaceVisible = false;
        launcher = new LauncherScreen(this, new LauncherScreen.Listener() {
            @Override public void onChooseGameFolder() { chooseGameFolder(); }
            @Override public void onPlay() { onPlayPressed(); }
            @Override public void onSettings() { showAppSettings(); }
            @Override public void onStorageCheck() { checkStorage(); }
        }, gameFolder);
        setContentView(launcher);
        LogUtils.i(TAG, "ROTTR launcher screen displayed");
    }

    private void chooseGameFolder() {
        try {
            Intent intent = new Intent(Intent.ACTION_OPEN_DOCUMENT_TREE);
            intent.addFlags(Intent.FLAG_GRANT_READ_URI_PERMISSION
                    | Intent.FLAG_GRANT_WRITE_URI_PERMISSION
                    | Intent.FLAG_GRANT_PERSISTABLE_URI_PERMISSION);
            startActivityForResult(intent, REQUEST_GAME_FOLDER);
        } catch (Throwable error) {
            LogUtils.exception("open game folder picker", error);
            Toast.makeText(this, "Unable to open Android folder picker", Toast.LENGTH_LONG).show();
        }
    }

    @Override protected void onActivityResult(int requestCode, int resultCode, Intent data) {
        super.onActivityResult(requestCode, resultCode, data);
        if (requestCode != REQUEST_GAME_FOLDER || resultCode != RESULT_OK || data == null || data.getData() == null) return;
        Uri selected = data.getData();
        try {
            int flags = data.getFlags() & (Intent.FLAG_GRANT_READ_URI_PERMISSION | Intent.FLAG_GRANT_WRITE_URI_PERMISSION);
            getContentResolver().takePersistableUriPermission(selected, flags);
            gameFolder = selected;
            getSharedPreferences(PREFS, MODE_PRIVATE).edit().putString("game_folder", selected.toString()).apply();
            if (launcher != null) launcher.setFolder(selected);
            LogUtils.i(TAG, "Game folder selected: " + selected);
            Toast.makeText(this, "Game folder saved", Toast.LENGTH_SHORT).show();
        } catch (Throwable error) {
            LogUtils.exception("persist game folder permission", error);
            Toast.makeText(this, "Could not save folder permission", Toast.LENGTH_LONG).show();
        }
    }

    private void onPlayPressed() {
        if (gameFolder == null) {
            new AlertDialog.Builder(this).setTitle("Select game folder")
                    .setMessage("Choose the folder containing your legally obtained Rise of the Tomb Raider game data before continuing.")
                    .setPositiveButton("Select folder", (d, w) -> chooseGameFolder())
                    .setNegativeButton("Cancel", null).show();
            return;
        }
        // Folder selection is persisted. The current native runtime does not yet expose
        // a game-data mount/boot JNI entry point, so do not switch to a blank render view.
        new AlertDialog.Builder(this).setTitle("Game folder saved")
                .setMessage("Your game folder is selected. The native game-data mounting and boot sequence is still being integrated; PLAY will be connected when that runtime path is ready.")
                .setPositiveButton("OK", null).show();
        LogUtils.i(TAG, "PLAY pressed; game folder selected, guest boot integration pending");
    }

    private void showAppSettings() {
        new AlertDialog.Builder(this).setTitle("ROTTR launcher")
                .setMessage("Graphics options are saved on this device; the native renderer does not use them yet.\n\n" + logStatus())
                .setPositiveButton("OK", null)
                .setNeutralButton("Log access", (dialog, which) -> openLogAccessSettings())
                .show();
    }

    private void checkStorage() {
        if (!LogUtils.isRootStorageAvailable()) {
            storagePromptShown = false;
            showRootStoragePrompt();
            return;
        }
        new AlertDialog.Builder(this).setTitle("Storage")
                .setMessage(logStatus()).setPositiveButton("OK", null).show();
    }

    private String logStatus() {
        java.io.File file = LogUtils.getLogFile();
        String where = file == null ? "no log file is open" : file.getAbsolutePath();
        return (LogUtils.isRootStorageAvailable()
                ? "All files access: allowed" : "All files access: NOT allowed")
                + "\nCurrent log: " + where;
    }

    @Override protected void onStart() { super.onStart(); LogUtils.i(TAG, "onStart"); }
    @Override protected void onResume() {
        super.onResume();
        LogUtils.init(this);
        LogUtils.i(TAG, "onResume");
        if (LogUtils.isRootStorageAvailable()) {
            boolean moved = LogUtils.migrateToRootStorage(this);
            LogUtils.i(TAG, "Root diagnostics storage ready; migration=" + moved + ", file=" + LogUtils.getLogFile());
        } else {
            LogUtils.i(TAG, "All-files access not granted; using app-private diagnostics storage");
        }
        if (launcher != null) launcher.refreshStatus();
    }

    private void showRootStoragePrompt() {
        if (storagePromptShown || isFinishing()) return;
        storagePromptShown = true;
        new AlertDialog.Builder(this).setTitle("Enable ROTTR crash logs")
                .setMessage("Allow access to all files to save diagnostics in Internal storage/ROTTR-logs.")
                .setPositiveButton("Open settings", (dialog, which) -> openLogAccessSettings())
                .setNegativeButton("Not now", null).show();
    }

    private void openLogAccessSettings() {
        try {
            Intent intent = new Intent(Settings.ACTION_MANAGE_APP_ALL_FILES_ACCESS_PERMISSION);
            intent.setData(Uri.parse("package:" + getPackageName()));
            startActivity(intent);
        } catch (Throwable error) {
            LogUtils.exception("open all-files-access settings for this app", error);
            try {
                // Some devices only offer the list of all apps.
                startActivity(new Intent(Settings.ACTION_MANAGE_ALL_FILES_ACCESS_PERMISSION));
            } catch (Throwable fallback) {
                LogUtils.exception("open all-files-access list", fallback);
                Toast.makeText(this, "Open Settings > Apps > Special access > All files access",
                        Toast.LENGTH_LONG).show();
            }
        }
    }

    @Override protected void onPause() { LogUtils.i(TAG, "onPause"); super.onPause(); }
    @Override protected void onStop() { LogUtils.i(TAG, "onStop"); super.onStop(); }

    @Override protected void onDestroy() {
        LogUtils.i(TAG, "onDestroy");
        try { NativeBridge.shutdown(); } catch (Throwable error) { LogUtils.exception("NativeBridge.shutdown", error); }
        if (surface != null) { surface.release(); surface = null; }
        LogUtils.close();
        super.onDestroy();
    }

    @Override public void onSurfaceTextureAvailable(android.graphics.SurfaceTexture texture, int width, int height) {
        LogUtils.i(TAG, "Runtime surface available: " + width + "x" + height);
        try {
            surface = new android.view.Surface(texture);
            if (NativeBridge.isNativeAvailable()) {
                NativeBridge.init(width, height);
                NativeBridge.setSurface(surface);
            } else LogUtils.e(TAG, "Native library unavailable");
        } catch (Throwable error) { LogUtils.exception("runtime surface initialization", error); }
    }
    @Override public void onSurfaceTextureSizeChanged(android.graphics.SurfaceTexture texture, int width, int height) {
        LogUtils.i(TAG, "Runtime surface resized: " + width + "x" + height);
        try { if (NativeBridge.isNativeAvailable()) NativeBridge.init(width, height); }
        catch (Throwable error) { LogUtils.exception("runtime surface resize", error); }
    }
    @Override public boolean onSurfaceTextureDestroyed(android.graphics.SurfaceTexture texture) {
        LogUtils.i(TAG, "Runtime surface destroyed");
        try { NativeBridge.releaseSurface(); } catch (Throwable error) { LogUtils.exception("surface release", error); }
        if (surface != null) { surface.release(); surface = null; }
        return true;
    }
    @Override public void onSurfaceTextureUpdated(android.graphics.SurfaceTexture texture) {}
}
