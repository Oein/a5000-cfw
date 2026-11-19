package dev.oein.a5knative;

import android.util.Log;
import java.io.File;

/**
 * Run a native root shell command, asynchronously, with no result.
 *
 * Same path as OpenMemories-Tweak's "Enable Telnet": libtweak.so's shell_exec_async
 * posts an OSAL message (queue 0x94020A, cmd 0x940005) to native bootin, which runs the
 * command with /bin/sh as root. Execution is OUTSIDE the /android chroot, so Android
 * paths must be written as /android/... (see app/docs/NATIVE_DEPLOY.md §1).
 *
 * Commands are capped at 255 chars by the service. Returns false if Tweak is absent.
 */
final class RootShell {
    private static final String TAG = "a5kui";
    private static final String LIB =
            "/data/data/com.github.ma1co.openmemories.tweak/lib/libtweak.so";
    private static Boolean loaded;

    private RootShell() {}

    static synchronized boolean exec(String command) {
        if (loaded == null) {
            try {
                if (!new File(LIB).canRead()) throw new IllegalStateException("not found: " + LIB);
                System.load(LIB);
                loaded = Boolean.TRUE;
            } catch (Throwable t) {
                Log.i(TAG, "RootShell: libtweak load failed: " + t);
                loaded = Boolean.FALSE;
            }
        }
        if (!loaded.booleanValue()) return false;
        try {
            com.github.ma1co.openmemories.tweak.Shell.nativeExec(command);
            return true;
        } catch (Throwable t) {
            Log.i(TAG, "RootShell: exec failed: " + t);
            return false;
        }
    }
}
