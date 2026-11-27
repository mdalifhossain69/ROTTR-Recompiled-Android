package com.rottr.recomp;

import android.content.Context;
import android.os.Build;
import android.os.Process;
import android.util.Log;

import java.io.BufferedReader;
import java.io.File;
import java.io.FileWriter;
import java.io.IOException;
import java.io.InputStreamReader;
import java.io.PrintWriter;
import java.io.StringWriter;
import java.text.SimpleDateFormat;
import java.util.Date;
import java.util.Locale;

/** Durable diagnostics for lifecycle events, uncaught exceptions and this process' logcat. */
public final class LogUtils {
    private static final String TAG = "ROTTR-Diagnostics";
    private static PrintWriter writer;
    private static File logFile;
    private static Process logcatProcess;
    private static Thread.UncaughtExceptionHandler previousHandler;
    private static boolean initialized;

    private LogUtils() {}

    public static synchronized void init(Context context) {
        if (initialized) return;
        Context app = context.getApplicationContext();
        File dir = app.getExternalFilesDir("ROTTR-logs");
        if (dir == null || (!dir.isDirectory() && !dir.mkdirs())) {
            dir = new File(app.getFilesDir(), "ROTTR-logs");
            if (!dir.isDirectory() && !dir.mkdirs()) dir = null;
        }
        if (dir == null) {
            Log.e(TAG, "Cannot create diagnostics directory");
            return;
        }

        String stamp = new SimpleDateFormat("yyyyMMdd_HHmmss", Locale.US).format(new Date());
        logFile = new File(dir, "rottr_" + stamp + ".log");
        try {
            writer = new PrintWriter(new FileWriter(logFile, true), true);
            initialized = true;
            append("=== ROTTR diagnostic session started ===");
            append("File: " + logFile.getAbsolutePath());
            append("Device: " + Build.MANUFACTURER + " " + Build.MODEL);
            append("Android: " + Build.VERSION.RELEASE + " (SDK " + Build.VERSION.SDK_INT + ")");
            append("Process ID: " + Process.myPid());
            installCrashHandler();
            startLogcatCapture();
            Log.i(TAG, "Diagnostics file: " + logFile.getAbsolutePath());
        } catch (IOException e) {
            Log.e(TAG, "Cannot open diagnostics file", e);
            writer = null;
        }
    }

    private static synchronized void append(String message) {
        if (writer == null) return;
        writer.println(timestamp() + " " + message);
        writer.flush();
    }

    public static void write(String message) {
        append(message);
        Log.d(TAG, message);
    }

    public static void i(String tag, String message) {
        write("I/" + tag + ": " + message);
        Log.i(tag, message);
    }

    public static void e(String tag, String message) {
        write("E/" + tag + ": " + message);
        Log.e(tag, message);
    }

    public static void d(String tag, String message) {
        write("D/" + tag + ": " + message);
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
            if (previousHandler != null) previousHandler.uncaughtException(thread, error);
        });
    }

    /** Captures Java and native logcat messages emitted by this app process. */
    private static void startLogcatCapture() {
        Thread capture = new Thread(() -> {
            try {
                logcatProcess = new ProcessBuilder("logcat", "-v", "threadtime",
                        "--pid=" + Process.myPid()).redirectErrorStream(true).start();
                append("Process logcat capture started");
                try (BufferedReader reader = new BufferedReader(
                        new InputStreamReader(logcatProcess.getInputStream()))) {
                    String line;
                    while ((line = reader.readLine()) != null) append("LOGCAT " + line);
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
