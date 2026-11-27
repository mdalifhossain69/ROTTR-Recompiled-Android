package com.rottr.recomp;

import android.app.Activity;
import android.app.DownloadManager;
import android.content.BroadcastReceiver;
import android.content.Context;
import android.database.Cursor;
import android.app.AlertDialog;
import android.content.Intent;
import android.net.Uri;
import android.os.Bundle;
import android.os.Environment;
import android.provider.Settings;
import android.util.Log;
import android.view.View;
import android.view.WindowManager;
import androidx.core.content.ContextCompat;
import android.widget.FrameLayout;
import android.widget.Toast;

/** Hosts the launcher and the boot screen. */
public class MainActivity extends Activity {
    private static final String TAG = "ROTTR-MainActivity";
    private static final int REQUEST_GAME_FOLDER = 4101;
    private static final int REQUEST_PROTON_PACKAGE = 4201;
    private static final int REQUEST_FEXCORE_PACKAGE = 4202;
    private static final String PROTON_104_URL = "https://downloads.gamenative.app/proton-10.0-4-arm64ec.wcp";
    private static final String DOWNLOAD_PREF = "proton_104_download_id";
    private static final String FEXCORE_DOWNLOAD_PREF = "fexcore_2611_download_id";
    private static final String FEXCORE_2611_URL = "https://downloads.gamenative.app/FEXCore-2611-gn-8.wcp";
    private BootScreen activeBoot;
    private static final String PREFS = "rottr_launcher";
    private Uri gameFolder;
    private LauncherScreen launcher;
    private boolean storagePromptShown;
    private boolean bootScreenVisible;
    private boolean downloadReceiverRegistered;
    private final BroadcastReceiver runtimeDownloadReceiver = new BroadcastReceiver() {
        @Override public void onReceive(Context context, Intent intent) {
            if (!DownloadManager.ACTION_DOWNLOAD_COMPLETE.equals(intent.getAction())) return;
            long completedId = intent.getLongExtra(DownloadManager.EXTRA_DOWNLOAD_ID, -1L);
            android.content.SharedPreferences prefs = getSharedPreferences(PREFS, MODE_PRIVATE);
            long protonId = prefs.getLong(DOWNLOAD_PREF, -2L);
            long fexId = prefs.getLong(FEXCORE_DOWNLOAD_PREF, -3L);
            String componentType = completedId == protonId ? RuntimePackageInstaller.TYPE_PROTON
                    : completedId == fexId ? RuntimePackageInstaller.TYPE_FEXCORE : null;
            if (componentType == null) return;
            prefs.edit().remove(RuntimePackageInstaller.TYPE_PROTON.equals(componentType)
                    ? DOWNLOAD_PREF : FEXCORE_DOWNLOAD_PREF).apply();
            DownloadManager manager = (DownloadManager) getSystemService(DOWNLOAD_SERVICE);
            try (Cursor cursor = manager.query(new DownloadManager.Query().setFilterById(completedId))) {
                if (cursor == null || !cursor.moveToFirst()) {
                    showRuntimeDownloadError("Download status could not be read.");
                    return;
                }
                int status = cursor.getInt(cursor.getColumnIndexOrThrow(DownloadManager.COLUMN_STATUS));
                if (status != DownloadManager.STATUS_SUCCESSFUL) {
                    int reason = cursor.getInt(cursor.getColumnIndexOrThrow(DownloadManager.COLUMN_REASON));
                    showRuntimeDownloadError(componentType + " download failed (code " + reason + ").");
                    return;
                }
            } catch (Exception error) {
                LogUtils.exception("check runtime download", error);
                showRuntimeDownloadError("Could not verify runtime download: " + error.getMessage());
                return;
            }
            Uri downloaded = manager.getUriForDownloadedFile(completedId);
            if (downloaded == null) {
                showRuntimeDownloadError("Downloaded runtime package could not be opened.");
                return;
            }
            if (activeBoot != null && activeBoot.isAttachedToWindow()) {
                activeBoot.addLine("Installing compatibility component...");
            }
            installRuntimePackage(downloaded, componentType);
        }
    };

    @Override protected void onCreate(Bundle savedInstanceState) {
        super.onCreate(savedInstanceState);
        LogUtils.init(this);
        ContextCompat.registerReceiver(this, runtimeDownloadReceiver,
                new android.content.IntentFilter(DownloadManager.ACTION_DOWNLOAD_COMPLETE),
                ContextCompat.RECEIVER_NOT_EXPORTED);
        downloadReceiverRegistered = true;
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
        activeBoot = null;
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
        if ((requestCode == REQUEST_PROTON_PACKAGE || requestCode == REQUEST_FEXCORE_PACKAGE) && resultCode == RESULT_OK && data != null && data.getData() != null) {
            installRuntimePackage(data.getData(), requestCode == REQUEST_PROTON_PACKAGE ? RuntimePackageInstaller.TYPE_PROTON : RuntimePackageInstaller.TYPE_FEXCORE);
            return;
        }
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
        ensureRuntimeComponents();
    }

    private void showBootScreen() {
        BootScreen boot = new BootScreen(this, this::showLauncher);
        activeBoot = boot;
        bootScreenVisible = true;
        launcher = null;
        NativeBridge.setStagesDone(1);
        setContentView(boot);
        boot.addLine("CPU execution backend status:\n" + NativeBridge.x64RuntimeStatus());
        boot.addLine(RuntimePackageInstaller.status(this) + "\n");
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
                .setMessage("Graphics options are saved on this device; the native renderer does not use them yet.\n\n" + logStatus() + "\n\n" + RuntimePackageInstaller.status(this))
                .setPositiveButton("OK", null)
                .setNeutralButton("Log access", (dialog, which) -> openLogAccessSettings())
                .show();
    }

    private void showRuntimeInstaller() {
        new AlertDialog.Builder(this).setTitle("Install Windows runtime")
                .setMessage("Import the matching Winlator component packages. Proton and FEXCore are separate WCP files.")
                .setItems(new String[] {
                        "Download Proton 10.0-4 ARM64EC",
                        "Import Proton ARM64EC (.wcp)",
                        "Import FEXCore ARM64EC bridge (.wcp)"},
                        (dialog, which) -> {
                            if (which == 0) downloadProton104();
                            else chooseRuntimePackage(which == 1
                                    ? RuntimePackageInstaller.TYPE_PROTON
                                    : RuntimePackageInstaller.TYPE_FEXCORE);
                        })
                .setNegativeButton("Cancel", null).show();
    }

    private void ensureRuntimeComponents() {
        java.io.File root = new java.io.File(getFilesDir(), "runtime-components");
        boolean protonReady = new java.io.File(root, "proton-arm64ec/profile.json").isFile();
        boolean fexReady = new java.io.File(root, "runtime-prefix/drive_c/windows/system32/libarm64ecfex.dll").isFile()
                && new java.io.File(root, "runtime-prefix/drive_c/windows/system32/libwow64fex.dll").isFile();
        if (protonReady && fexReady) {
            if (activeBoot != null) activeBoot.addLine("Compatibility components are installed.");
            return;
        }
        if (!protonReady) {
            queueRuntimeDownload(RuntimePackageInstaller.TYPE_PROTON, PROTON_104_URL, DOWNLOAD_PREF,
                    "Proton 10.0-4 ARM64EC");
        } else if (!fexReady) {
            queueRuntimeDownload(RuntimePackageInstaller.TYPE_FEXCORE, FEXCORE_2611_URL,
                    FEXCORE_DOWNLOAD_PREF, "FEXCore ARM64EC");
        }
    }

    private void queueRuntimeDownload(String type, String url, String preferenceKey, String label) {
        try {
            DownloadManager manager = (DownloadManager) getSystemService(DOWNLOAD_SERVICE);
            if (manager == null) throw new IllegalStateException("Android Download Manager is unavailable");
            DownloadManager.Request request = new DownloadManager.Request(Uri.parse(url));
            request.setTitle("ROTTR game runtime");
            request.setDescription("Preparing game compatibility components");
            request.setMimeType("application/octet-stream");
            request.setNotificationVisibility(DownloadManager.Request.VISIBILITY_VISIBLE);
            request.setAllowedOverMetered(true);
            request.setAllowedOverRoaming(false);
            request.setDestinationInExternalFilesDir(this, Environment.DIRECTORY_DOWNLOADS,
                    type.toLowerCase() + "-runtime.wcp");
            long id = manager.enqueue(request);
            getSharedPreferences(PREFS, MODE_PRIVATE).edit().putLong(preferenceKey, id).apply();
            if (activeBoot != null) activeBoot.addLine("Downloading game compatibility files...");
            LogUtils.i(TAG, "Queued " + label + " download id=" + id);
        } catch (Exception error) {
            LogUtils.exception("download " + label, error);
            showRuntimeDownloadError(error.getMessage() == null ? error.toString() : error.getMessage());
        }
    }

    private void showRuntimeDownloadError(String message) {
        runOnUiThread(() -> {
            if (isFinishing() || isDestroyed()) return;
            new AlertDialog.Builder(this).setTitle("Proton download failed")
                    .setMessage(message).setPositiveButton("OK", null).show();
        });
    }

    private void chooseRuntimePackage(String type) {
        try {
            Intent intent = new Intent(Intent.ACTION_OPEN_DOCUMENT);
            intent.addCategory(Intent.CATEGORY_OPENABLE);
            intent.setType("*/*");
            intent.addFlags(Intent.FLAG_GRANT_READ_URI_PERMISSION);
            startActivityForResult(intent, RuntimePackageInstaller.TYPE_PROTON.equals(type)
                    ? REQUEST_PROTON_PACKAGE : REQUEST_FEXCORE_PACKAGE);
        } catch (Throwable error) {
            LogUtils.exception("open runtime package picker", error);
            Toast.makeText(this, "Unable to open runtime package picker", Toast.LENGTH_LONG).show();
        }
    }

    private void installRuntimePackage(Uri source, String type) {
        Toast.makeText(this, "Importing " + type + " package...", Toast.LENGTH_SHORT).show();
        new Thread(() -> {
            try {
                RuntimePackageInstaller.InstallResult result =
                        RuntimePackageInstaller.install(this, source, type);
                runOnUiThread(() -> {
                    if (isFinishing() || isDestroyed()) return;
                    Toast.makeText(this, result.type + " " + result.version + " imported",
                            Toast.LENGTH_LONG).show();
                    LogUtils.i(TAG, "Runtime component imported: " + result.type + " "
                            + result.version + " at " + result.directory);
                    ensureRuntimeComponents();
                    if (RuntimePackageInstaller.TYPE_FEXCORE.equals(type) && activeBoot != null)
                        activeBoot.addLine("Compatibility files installed. Preparing native game launch...");
                });
            } catch (Exception error) {
                LogUtils.exception("import " + type + " runtime package", error);
                runOnUiThread(() -> {
                    if (isFinishing() || isDestroyed()) return;
                    new AlertDialog.Builder(this).setTitle("Runtime import failed")
                            .setMessage(error.getMessage() == null ? error.toString() : error.getMessage())
                            .setPositiveButton("OK", null).show();
                });
            }
        }, "ROTTR-runtime-import").start();
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
        if (downloadReceiverRegistered) {
            try { unregisterReceiver(runtimeDownloadReceiver); }
            catch (IllegalArgumentException ignored) { }
            downloadReceiverRegistered = false;
        }
        try { NativeBridge.releaseSurface(); } catch (Throwable error) { LogUtils.exception("NativeBridge.releaseSurface", error); }
        LogUtils.close();
        super.onDestroy();
    }
}
