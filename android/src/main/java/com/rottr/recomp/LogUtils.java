package com.rottr.recomp;

import android.content.Context;
import android.os.Environment;
import android.util.Log;

import java.io.File;
import java.io.FileWriter;
import java.io.IOException;
import java.io.PrintWriter;
import java.text.SimpleDateFormat;
import java.util.Date;
import java.util.Locale;

/**
 * Auto-saves ROTTR-Recomp logs to external storage (SD card).
 *
 * On app open this creates {@code /sdcard/ROTTR-logs/} (falling back to the app's
 * external files directory on scoped-storage devices) and writes every log line to a
 * timestamped file so QA / testers can pull device logs without adb.
 */
public final class LogUtils {
    private static final String TAG = "ROTTR-Recomp-LogUtils";
    private static final String LOG_DIR_NAME = "ROTTR-logs";
    private static PrintWriter sLogWriter;
    private static File sLogFile;
    private static boolean sEnabled = false;

    private LogUtils() {
        // utility class
    }

    /** Call once from Application / Activity.onCreate(). */
    public static synchronized void init(Context context) {
        if (sEnabled) {
            return;
        }
        File dir = null;

        // Primary target: /sdcard/ROTTR-logs (user-requested location)
        File externalRoot = Environment.getExternalStorageDirectory();
        if (externalRoot != null) {
            File candidate = new File(externalRoot, LOG_DIR_NAME);
            if (tryMakeDir(candidate)) {
                dir = candidate;
            }
        }

        // Fallback: app-specific external files dir (always writable, no permission)
        if (dir == null && context != null) {
            File extFiles = context.getExternalFilesDir(null);
            if (extFiles != null) {
                File candidate = new File(extFiles, LOG_DIR_NAME);
                if (tryMakeDir(candidate)) {
                    dir = candidate;
                }
            }
        }

        // Last resort: internal cache dir
        if (dir == null && context != null) {
            File candidate = new File(context.getCacheDir(), LOG_DIR_NAME);
            if (tryMakeDir(candidate)) {
                dir = candidate;
            }
        }

        if (dir == null) {
            Log.e(TAG, "Failed to create any log directory");
            return;
        }

        String timestamp = new SimpleDateFormat("yyyyMMdd_HHmmss", Locale.getDefault())
                .format(new Date());
        sLogFile = new File(dir, "rottr_" + timestamp + ".log");

        try {
            sLogWriter = new PrintWriter(new FileWriter(sLogFile, true));
            sEnabled = true;
            write("=== ROTTR-Recomp log session started ===");
            write("Log file: " + sLogFile.getAbsolutePath());
            write("Device: " + android.os.Build.MANUFACTURER + " " + android.os.Build.MODEL);
            write("Android SDK: " + android.os.Build.VERSION.SDK_INT);
        } catch (IOException e) {
            Log.e(TAG, "Failed to open log file", e);
            sLogWriter = null;
        }
    }

    private static boolean tryMakeDir(File dir) {
        if (dir.exists()) {
            return dir.isDirectory();
        }
        return dir.mkdirs();
    }

    /** Write a single line to the log file (and to Logcat). */
    public static synchronized void write(String message) {
        if (!sEnabled || sLogWriter == null) {
            return;
        }
        String line = timestamp() + " " + message;
        sLogWriter.println(line);
        sLogWriter.flush();
        Log.d("ROTTR-Recomp-LogFile", message);
    }

    /** Convenience: log at Info level. */
    public static synchronized void i(String tag, String message) {
        write("I/" + tag + ": " + message);
        Log.i(tag, message);
    }

    /** Convenience: log at Error level. */
    public static synchronized void e(String tag, String message) {
        write("E/" + tag + ": " + message);
        Log.e(tag, message);
    }

    /** Convenience: log at Debug level. */
    public static synchronized void d(String tag, String message) {
        write("D/" + tag + ": " + message);
        Log.d(tag, message);
    }

    /** Return the path of the current log file, or null if not initialized. */
    public static synchronized File getLogFile() {
        return sLogFile;
    }

    public static synchronized void close() {
        if (sLogWriter != null) {
            write("=== ROTTR-Recomp log session ended ===");
            sLogWriter.close();
            sLogWriter = null;
        }
        sEnabled = false;
    }

    private static String timestamp() {
        return new SimpleDateFormat("yyyy-MM-dd HH:mm:ss.SSS", Locale.getDefault())
                .format(new Date());
    }
}
