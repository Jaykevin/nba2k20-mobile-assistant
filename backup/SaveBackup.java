package com.local.nba2k20.backup;

import android.app.Instrumentation;
import android.os.Bundle;
import java.io.File;
import java.io.FileInputStream;
import java.io.FileOutputStream;
import java.io.IOException;
import java.io.InputStream;
import java.util.zip.ZipEntry;
import java.util.zip.ZipOutputStream;

/**
 * Runs inside the game process (same signing key) and zips the game's private data directory and the
 * external DAT folder into <external files>/save-backup.zip, readable by adb. Read-only for the game data.
 */
public final class SaveBackup extends Instrumentation {
    @Override public void onCreate(Bundle arguments) {
        super.onCreate(arguments);
        start();
    }

    @Override public void onStart() {
        Bundle result = new Bundle();
        try {
            File dataDir = new File(getTargetContext().getApplicationInfo().dataDir);
            File external = getTargetContext().getExternalFilesDir(null);
            File target = new File(external, "save-backup.zip");
            long[] totals = new long[2];
            try (ZipOutputStream zip = new ZipOutputStream(new java.io.BufferedOutputStream(new FileOutputStream(target), 1 << 20))) {
                zip.setLevel(java.util.zip.Deflater.NO_COMPRESSION);
                add(zip, dataDir, "data/", totals);
                add(zip, new File(external, "DAT"), "external-DAT/", totals);
                File userData = new File(external, "usrdat.tpt");
                if (userData.isFile()) addFile(zip, userData, "external/usrdat.tpt", totals);
                zip.putNextEntry(new ZipEntry("index.txt"));
                zip.write(index.toString().getBytes("UTF-8"));
                zip.closeEntry();
            }
            try (FileOutputStream out = new FileOutputStream(new File(external, "save-backup-index.txt"))) {
                out.write(index.toString().getBytes("UTF-8"));
            }
            target.setReadable(true, false);
            result.putString("backup", target.getAbsolutePath());
            result.putLong("files", totals[0]);
            result.putLong("bytes", totals[1]);
            result.putLong("zip_bytes", target.length());
            finish(0, result);
        } catch (Throwable error) {
            result.putString("error", String.valueOf(error));
            finish(1, result);
        }
    }

    private static void add(ZipOutputStream zip, File dir, String prefix, long[] totals) throws IOException {
        File[] children = dir.listFiles();
        if (children == null) return;
        for (File child : children) {
            String name = child.getName();
            if (prefix.equals("data/") && (name.equals("cache") || name.equals("code_cache") || name.equals("lib"))) continue;
            if (child.isDirectory()) add(zip, child, prefix + name + "/", totals);
            else if (child.isFile()) addFile(zip, child, prefix + name, totals);
        }
    }

    static final long MAX_FILE = 32L << 20;   /* saves are small; skip big asset/cache blobs */
    static final StringBuilder index = new StringBuilder();

    private static void addFile(ZipOutputStream zip, File file, String entry, long[] totals) throws IOException {
        index.append(file.length()).append('\t').append(entry).append('\n');
        if (file.length() > MAX_FILE) {
            index.append("SKIPPED (too large)\t").append(entry).append('\n');
            return;
        }
        zip.putNextEntry(new ZipEntry(entry));
        byte[] buffer = new byte[65536];
        try (InputStream in = new FileInputStream(file)) {
            int n;
            while ((n = in.read(buffer)) > 0) {
                zip.write(buffer, 0, n);
                totals[1] += n;
            }
        }
        zip.closeEntry();
        totals[0]++;
    }
}
