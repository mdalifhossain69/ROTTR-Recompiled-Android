package com.rottr.recomp;

import android.content.ContentResolver;
import android.content.Context;
import android.net.Uri;

import org.apache.commons.compress.archivers.tar.TarArchiveEntry;
import org.apache.commons.compress.archivers.tar.TarArchiveInputStream;
import org.apache.commons.compress.compressors.xz.XZCompressorInputStream;
import org.apache.commons.compress.compressors.zstandard.ZstdCompressorInputStream;
import org.json.JSONObject;

import java.io.BufferedInputStream;
import java.io.File;
import java.io.FileInputStream;
import java.io.FileOutputStream;
import java.io.IOException;
import java.io.InputStream;
import java.io.OutputStream;
import java.nio.charset.StandardCharsets;

/**
 * Imports Winlator-compatible WCP component packages into app-private storage.
 *
 * WCP is a tar archive compressed with Zstandard or XZ. This class validates the
 * profile and archive paths before extracting. It does not execute Wine or claim
 * that importing the packages alone enables x64 execution.
 */
public final class RuntimePackageInstaller {
    private static final long MAX_UNPACKED_BYTES = 3L * 1024L * 1024L * 1024L;
    private static final int MAX_ENTRIES = 250_000;
    private static final int MAX_PROFILE_BYTES = 1024 * 1024;

    public static final String TYPE_PROTON = "Proton";
    public static final String TYPE_FEXCORE = "FEXCore";

    private RuntimePackageInstaller() {}

    public static final class InstallResult {
        public final String type;
        public final String version;
        public final File directory;

        InstallResult(String type, String version, File directory) {
            this.type = type;
            this.version = version;
            this.directory = directory;
        }
    }

    public static InstallResult install(Context context, Uri source, String expectedType)
            throws IOException {
        if (!TYPE_PROTON.equals(expectedType) && !TYPE_FEXCORE.equals(expectedType)) {
            throw new IOException("Unsupported runtime component type");
        }

        File root = new File(context.getFilesDir(), "runtime-components");
        if (!root.exists() && !root.mkdirs()) throw new IOException("Cannot create runtime directory");
        File stage = new File(root, ".install-" + expectedType.toLowerCase() + "-" + System.nanoTime());
        if (!stage.mkdirs()) throw new IOException("Cannot create staging directory");

        boolean success = false;
        try {
            ContentResolver resolver = context.getContentResolver();
            InputStream raw = resolver.openInputStream(source);
            if (raw == null) throw new IOException("Selected package cannot be opened");
            JSONObject profile;
            try (InputStream input = openArchiveStream(new BufferedInputStream(raw));
                 TarArchiveInputStream tar = new TarArchiveInputStream(input)) {
                profile = extractTar(tar, stage);
            }

            String type = profile.optString("type", "");
            if (!expectedType.equalsIgnoreCase(type)) {
                throw new IOException("Package type mismatch: expected " + expectedType
                        + ", found " + (type.isEmpty() ? "unknown" : type));
            }
            String version = profile.optString("versionName",
                    profile.optString("version", profile.optString("name", expectedType)));
            if (version.trim().isEmpty()) version = expectedType;

            if (TYPE_FEXCORE.equals(expectedType)) {
                File arm64ec = new File(stage, "system32/libarm64ecfex.dll");
                File wow64 = new File(stage, "system32/libwow64fex.dll");
                if (!arm64ec.isFile() || !wow64.isFile()) {
                    throw new IOException("FEXCore package is missing its ARM64EC/WOW64 bridge DLLs");
                }
            } else {
                if (!new File(stage, "profile.json").isFile()) {
                    throw new IOException("Proton package is missing profile.json");
                }
            }

            File destination = new File(root, TYPE_FEXCORE.equals(expectedType)
                    ? "fexcore-arm64ec" : "proton-arm64ec");
            File backup = new File(root, destination.getName() + ".previous");
            deleteTree(backup);
            if (destination.exists() && !destination.renameTo(backup)) {
                throw new IOException("Cannot move the previous runtime package");
            }
            if (!stage.renameTo(destination)) {
                if (backup.exists()) backup.renameTo(destination);
                throw new IOException("Cannot activate the imported runtime package");
            }
            deleteTree(backup);
            success = true;
            return new InstallResult(expectedType, version, destination);
        } finally {
            if (!success) deleteTree(stage);
        }
    }

    private static InputStream openArchiveStream(BufferedInputStream input) throws IOException {
        input.mark(8);
        int a = input.read();
        int b = input.read();
        int c = input.read();
        int d = input.read();
        input.reset();
        if (a == 0x28 && b == 0xB5 && c == 0x2F && d == 0xFD) {
            return new ZstdCompressorInputStream(input);
        }
        if (a == 0xFD && b == 0x37 && c == 0x7A && d == 0x58) {
            return new XZCompressorInputStream(input, true);
        }
        throw new IOException("Unsupported WCP compression; expected Zstandard or XZ");
    }

    private static JSONObject extractTar(TarArchiveInputStream tar, File root) throws IOException {
        byte[] buffer = new byte[64 * 1024];
        long total = 0;
        int count = 0;
        String profileText = null;
        TarArchiveEntry entry;
        while ((entry = tar.getNextTarEntry()) != null) {
            if (++count > MAX_ENTRIES) throw new IOException("WCP contains too many files");
            String name = safeRelativeName(entry.getName());
            if (name.isEmpty()) continue;
            if (entry.isSymbolicLink() || entry.isLink() || entry.isSparse()) {
                throw new IOException("WCP contains an unsupported link or sparse file: " + name);
            }
            File output = new File(root, name);
            String rootPath = root.getCanonicalPath() + File.separator;
            String outputPath = output.getCanonicalPath();
            if (!outputPath.startsWith(rootPath)) throw new IOException("WCP path escapes install directory");

            if (entry.isDirectory()) {
                if (!output.isDirectory() && !output.mkdirs()) {
                    throw new IOException("Cannot create directory: " + name);
                }
                continue;
            }
            if (!entry.isFile()) continue;
            if (entry.getSize() < 0) throw new IOException("Invalid archive entry size");
            total += entry.getSize();
            if (total > MAX_UNPACKED_BYTES) throw new IOException("WCP exceeds 3 GiB unpacked limit");
            File parent = output.getParentFile();
            if (parent != null && !parent.isDirectory() && !parent.mkdirs()) {
                throw new IOException("Cannot create parent directory for " + name);
            }

            try (OutputStream out = new FileOutputStream(output)) {
                int read;
                long written = 0;
                while ((read = tar.read(buffer)) != -1) {
                    out.write(buffer, 0, read);
                    written += read;
                    if (written > entry.getSize()) throw new IOException("Tar entry exceeds declared size");
                    if (name.equals("profile.json") && profileText == null) {
                        // profile.json is normally small; it is parsed from the extracted file below.
                    }
                }
                if (written != entry.getSize()) throw new IOException("Truncated WCP entry: " + name);
            }
            if ("profile.json".equals(name)) {
                if (entry.getSize() > MAX_PROFILE_BYTES) throw new IOException("WCP profile is too large");
                try (FileInputStream file = new FileInputStream(output)) {
                    byte[] bytes = new byte[(int) entry.getSize()];
                    int offset = 0;
                    while (offset < bytes.length) {
                        int read = file.read(bytes, offset, bytes.length - offset);
                        if (read < 0) throw new IOException("Truncated profile.json");
                        offset += read;
                    }
                    profileText = new String(bytes, StandardCharsets.UTF_8);
                }
            }
        }
        if (profileText == null) throw new IOException("WCP archive has no profile.json");
        try {
            return new JSONObject(profileText);
        } catch (Exception error) {
            throw new IOException("Invalid WCP profile.json", error);
        }
    }

    private static String safeRelativeName(String raw) throws IOException {
        if (raw == null || raw.isEmpty() || raw.startsWith("/") || raw.startsWith("\\")) {
            throw new IOException("Unsafe WCP path");
        }
        String name = raw.replace('\\', '/');
        while (name.startsWith("./")) name = name.substring(2);
        if (name.isEmpty() || ".".equals(name)) return "";
        String[] parts = name.split("/");
        for (String part : parts) {
            if ("..".equals(part) || part.isEmpty()) throw new IOException("Unsafe WCP path: " + raw);
        }
        return name;
    }

    public static String status(Context context) {
        File root = new File(context.getFilesDir(), "runtime-components");
        File proton = new File(root, "proton-arm64ec/profile.json");
        File fex = new File(root, "fexcore-arm64ec/system32/libarm64ecfex.dll");
        File wow = new File(root, "fexcore-arm64ec/system32/libwow64fex.dll");
        return "Proton ARM64EC package: " + (proton.isFile() ? "imported" : "missing")
                + "\nFEXCore ARM64EC bridge: "
                + (fex.isFile() && wow.isFile() ? "imported" : "missing")
                + "\nExecution: not connected to Android process launcher";
    }

    private static void deleteTree(File file) {
        if (file == null || !file.exists()) return;
        File[] children = file.listFiles();
        if (children != null) for (File child : children) deleteTree(child);
        //noinspection ResultOfMethodCallIgnored
        file.delete();
    }
}
