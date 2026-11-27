package com.rottr.recomp;

import android.content.Context;
import android.os.Build;
import android.os.Environment;
import android.os.Process;
import android.util.Log;

import java.io.BufferedReader;
import java.io.File;
import java.io.FileWriter;
import java.io.IOException;
import java.io.InputStreamReader;
import java.io.PrintWriter;
import java.io.StringWriter;
import java.nio.file.Files;
import java.nio.file.StandardCopyOption;
import java.text.SimpleDateFormat;
import java.util.Date;
import java.util.Locale;

/**
 * ROTTR diagnostics. The requested public log directory is
 * /storage/emulated/0/ROTTR-logs (shown as internal shared storage/ROTTR-logs).
 * Android 11+ requires the user to grant the app "All files access" in Settings.
 */
public final class LogUtils {
    private static final String TAG = "ROTTR-Diagnostics";
    private static final String LOG_DIR_NAME = "ROTTR-logs";
    // Keep diagnostics bounded even if a device or native component becomes noisy.
    private static final long MAX_LOG_BYTES = 8L * 1024L * 1024L;
    private static PrintWriter writer;
    private static File logFile;
    private static java.lang.Process logcatProcess;
    private static Thread.UncaughtExceptionHandler previousHandler;
    private static boolean initialized;
    private static boolean crashHandlerInstalled;
    private static Context appContext;

    private LogUtils() {}

    public static synchronized void init(Context context) {
        if (initialized) return;
        appContext = context.getApplicationContext();
        File rootDir = getRootLogDirectory();
        if (rootDir != null && canWriteRootStorage()) {
            openLogFile(rootDir, null);
        } else {
            // Temporary private fallback captures startup failures until the user grants
            // all-files access. It is app-internal, not Android/data on shared storage.
            File privateDir = new File(appContext.getFilesDir(), LOG_DIR_NAME);
            if (privateDir.isDirectory() || privateDir.mkdirs()) {
                openLogFile(privateDir, "ROOT STORAGE ACCESS NOT GRANTED; temporary internal log");
            } else {
                Log.e(TAG, "Unable to create root or internal diagnostics directory");
            }
        }
        if (initialized) {
            if (!crashHandlerInstalled) {
                installCrashHandler();
                crashHandlerInstalled = true;
            }
            if (logcatProcess == null) startLogcatCapture();
        }
    }

    private static File getRootLogDirectory() {
        File sharedRoot = Environment.getExternalStorageDirectory();
        return sharedRoot == null ? null : new File(sharedRoot, LOG_DIR_NAME);
    }

    private static boolean canWriteRootStorage() {
        if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.R) {
            return Environment.isExternalStorageManager();
        }
        return Environment.getExternalStorageState().equals(Environment.MEDIA_MOUNTED);
    }

    private static synchronized void openLogFile(File dir, String warning) {
        if (!dir.isDirectory() && !dir.mkdirs()) {
            Log.e(TAG, "Cannot create log directory: " + dir.getAbsolutePath());
            return;
        }
        String stamp = new SimpleDateFormat("yyyyMMdd_HHmmss", Locale.US).format(new Date());
        logFile = new File(dir, "ROTTR_" + stamp + ".log");
        try {
            writer = new PrintWriter(new FileWriter(logFile, true), true);
            initialized = true;
            append("=== ROTTR diagnostic session started ===");
            append("Log file: " + logFile.getAbsolutePath());
            append("Device: " + Build.MANUFACTURER + " " + Build.MODEL);
            append("Android: " + Build.VERSION.RELEASE + " (SDK " + Build.VERSION.SDK_INT + ")");
            append("PID: " + Process.myPid());
            if (warning != null) append(warning);
        } catch (IOException e) {
            Log.e(TAG, "Cannot open log file " + logFile, e);
            writer = null;
            initialized = false;
        }
    }

    /**
     * Call when the Activity returns from Android's All files access settings.
     * Moves the startup log into the requested public root folder and continues there.
     */
    public static synchronized boolean migrateToRootStorage(Context context) {
        appContext = context.getApplicationContext();
        if (!canWriteRootStorage()) {
            append("Root log access still not granted");
            return false;
        }
        File dir = getRootLogDirectory();
        if (dir == null || (!dir.isDirectory() && !dir.mkdirs())) {
            append("Cannot create requested root log directory");
            return false;
        }

        if (logFile != null && logFile.getParentFile() != null
                && !logFile.getParentFile().equals(dir) && logFile.isFile()) {
            File destination = new File(dir, logFile.getName());
            try {
                if (writer != null) {
                    writer.flush();
                    writer.close();
                    writer = null;
                }
                Files.copy(logFile.toPath(), destination.toPath(),
                        StandardCopyOption.REPLACE_EXISTING);
                logFile = destination;
                writer = new PrintWriter(new FileWriter(logFile, true), true);
                append("Startup diagnostics moved to public root storage");
                Log.i(TAG, "Diagnostics now writing to " + logFile.getAbsolutePath());
                return true;
            } catch (Exception e) {
                Log.e(TAG, "Failed to move startup diagnostics to root storage", e);
                if (writer == null && logFile.exists()) {
                    try { writer = new PrintWriter(new FileWriter(logFile, true), true); }
                    catch (IOException ignored) { }
                }
                return false;
            }
        }

        if (writer == null) openLogFile(dir, null);
        append("Public root log storage confirmed");
        return writer != null;
    }

    /**
     * Writes captured logcat lines to the file WITHOUT forwarding them back to
     * Android Log. Forwarding would make our own logcat reader capture its output
     * forever, creating an unbounded feedback loop.
     *
     * @return false when logging has reached its size limit or the writer is closed.
     */
    private static synchronized boolean appendCapturedLine(String message) {
        if (writer == null || logFile == null) return false;
        if (logFile.length() >= MAX_LOG_BYTES) {
            writer.flush();
            writer.close();
            writer = null;
            return false;
        }
        writer.println(timestamp() + " " + message);
        writer.flush();
        if (logFile.length() >= MAX_LOG_BYTES) {
            writer.close();
            writer = null;
            return false;
        }
        return true;
    }

    private static synchronized void append(String message) {
        String line = timestamp() + " " + message;
        if (writer != null) {
            if (logFile != null && logFile.length() >= MAX_LOG_BYTES) {
                writer.flush();
                writer.close();
                writer = null;
            } else {
                writer.println(line);
                writer.flush();
            }
        }
        // This is intentionally not mirrored by appendCapturedLine().
        Log.d(TAG, message);
    }

    public static void write(String message) { append(message); }

    public static void i(String tag, String message) {
        append("I/" + tag + ": " + message);
        Log.i(tag, message);
    }

    public static void e(String tag, String message) {
        append("E/" + tag + ": " + message);
        Log.e(tag, message);
    }

    public static void d(String tag, String message) {
        append("D/" + tag + ": " + message);
        Log.d(tag, message);
    }

    public static void exception(String where, Throwable error) {
        StringWriter sw = new StringWriter();
        error.printStackTrace(new PrintWriter(sw));
        append("EXCEPTION at " + where + "\n" + sw);
        Log.e(TAG, "Exception at " + where, error);
    }

    private static void installCrashHandler() {
        previousHandler = Thread.getDefaultUncaughtExceptionHandler();
        Thread.setDefaultUncaughtExceptionHandler((thread, error) -> {
            exception("uncaught thread " + thread.getName(), error);
            append("=== PROCESS TERMINATING AFTER UNCAUGHT EXCEPTION ===");
            synchronized (LogUtils.class) {
                if (writer != null) writer.flush();
            }
            if (previousHandler != null) previousHandler.uncaughtException(thread, error);
        });
    }

    /** Captures this app process' Java and native logcat output while it is running. */
    private static void startLogcatCapture() {
        Thread capture = new Thread(() -> {
            try {
                logcatProcess = new ProcessBuilder("logcat", "-v", "threadtime",
                        "--pid=" + Process.myPid()).redirectErrorStream(true).start();
                append("Process logcat capture started");
                try (BufferedReader reader = new BufferedReader(
                        new InputStreamReader(logcatProcess.getInputStream()))) {
                    String line;
                    while ((line = reader.readLine()) != null) {
                        // Never call append() here: that mirrors to logcat and
                        // would recursively capture this same line.
                        if (!appendCapturedLine("LOGCAT " + line)) {
                            Log.w(TAG, "Diagnostic log reached 8 MiB limit; stopping logcat capture");
                            break;
                        }
                    }
                }
                append("Process logcat capture ended");
            } catch (Exception e) {
                append("Process logcat capture unavailable: " + e);
                Log.w(TAG, "Could not capture process logcat", e);
            }
        }, "ROTTR-logcat-capture");
        capture.setDaemon(true);
        capture.start();
    }

    public static synchronized File getLogFile() { return logFile; }

    public static synchronized boolean isRootStorageAvailable() {
        return canWriteRootStorage();
    }

    public static synchronized void close() {
        append("=== ROTTR diagnostic session ended ===");
        if (logcatProcess != null) {
            logcatProcess.destroy();
            logcatProcess = null;
        }
        if (writer != null) {
            writer.flush();
            writer.close();
            writer = null;
        }
        initialized = false;
    }

    private static String timestamp() {
        return new SimpleDateFormat("yyyy-MM-dd HH:mm:ss.SSS", Locale.US).format(new Date());
    }
}
