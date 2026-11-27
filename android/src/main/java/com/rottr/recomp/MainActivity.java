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

/** Hosts the launcher and the boot screen. */
public class MainActivity extends Activity {
    private static final String TAG = "ROTTR-MainActivity";
    private static final int REQUEST_GAME_FOLDER = 4101;
    private static final String PREFS = "rottr_launcher";
    private Uri gameFolder;
    private LauncherScreen launcher;
    private boolean storagePromptShown;
    private boolean bootScreenVisible;

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
        bootScreenVisible = false;
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
        showBootScreen();
    }

    private void showBootScreen() {
        BootScreen boot = new BootScreen(this, this::showLauncher);
        bootScreenVisible = true;
        launcher = null;
        NativeBridge.setStagesDone(1);
        setContentView(boot);
        boot.addLine("CPU execution backend status:\n" + NativeBridge.x64RuntimeStatus());
        boot.addLine("Game launch remains blocked until a Windows x86-64 execution backend and Windows/DirectX runtime are integrated.\n");
        boot.addLine("Checking ROTTR.exe (read-only)...");
        checkGameExe(boot, gameFolder);
    }

    private void checkGameExe(BootScreen boot, Uri folder) {
        new Thread(() -> {
            String report;
            boolean passed = false;
            try {
                Uri exe = GameFolder.findFile(getContentResolver(), folder, "ROTTR.exe");
                if (exe == null) {
                    report = "ROTTR.exe was not found directly inside the selected folder.";
                } else {
                    try (android.os.ParcelFileDescriptor file =
                                 getContentResolver().openFileDescriptor(exe, "r")) {
                        if (file == null) throw new java.io.IOException("file could not be opened");
                        String result = NativeBridge.inspectExe(file.getFd());
                        passed = result.startsWith("1\n");
                        report = result.substring(result.indexOf('\n') + 1);
                    }
                }
            } catch (Exception error) {
                LogUtils.exception("check ROTTR.exe", error);
                report = "Could not check ROTTR.exe: " + error.getMessage()
                        + "\nTry selecting the game folder again.";
            }
            final String text = report;
            final boolean ok = passed;
            runOnUiThread(() -> {
                // The user may have gone back while the file was being read.
                if (isDestroyed() || !boot.isAttachedToWindow()) return;
                if (ok) NativeBridge.setStagesDone(2);
                boot.addLine(text);
            });
        }, "ROTTR-exe-check").start();
    }

    @Override public void onBackPressed() {
        if (bootScreenVisible) showLauncher();
        else super.onBackPressed();
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
        try { NativeBridge.releaseSurface(); } catch (Throwable error) { LogUtils.exception("NativeBridge.releaseSurface", error); }
        LogUtils.close();
        super.onDestroy();
    }
}
