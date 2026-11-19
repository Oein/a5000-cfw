package dev.oein.a5knative;

import android.app.Activity;
import android.content.Intent;
import android.os.Bundle;
import android.util.Log;
import android.view.KeyEvent;

/**
 * Transparent foreground Activity that (1) keeps the app (Scalar) mode and key
 * routing open via the arbiter (AppInfoReceive / APO), and (2) launches the native
 * a5kui UI process as root through the OpenMemories backdoor (NATIVE_DEPLOY §3).
 *
 * The Activity stays transparent so the native Rigel window (live view) shows through
 * (TRACKB_RUNTIME §4.2). Leaving the app (MENU / going home) runs onPause -> stop,
 * which SIGTERMs a5kui so it disconnects SDCom and tears down its window cleanly.
 */
public class LauncherActivity extends Activity {
    private static final String TAG = "a5kui";
    private static final String PKG = "dev.oein.a5knative";
    private boolean started;

    @Override
    protected void onCreate(Bundle b) {
        super.onCreate(b);
        // No content view: the native process owns the screen. Keep it transparent.
    }

    @Override
    protected void onResume() {
        super.onResume();
        notifyAppInfo();              // open full key routing (ABT APP_INFO)
        setAutoPowerOffEnabled(false);
        startNative();
    }

    @Override
    protected void onPause() {
        super.onPause();
        stopNative();                 // SIGTERM -> clean SDCom disconnect + window teardown
    }

    @Override
    protected void onDestroy() {
        super.onDestroy();
        stopNative();
    }

    @Override
    public boolean onKeyUp(int keyCode, KeyEvent event) {
        // MENU (scan 514) or BACK leaves the app and returns to the stock shooting screen.
        int scan = event.getScanCode();
        if (scan == 514 || keyCode == KeyEvent.KEYCODE_BACK) {
            finish();
            startActivity(new Intent(Intent.ACTION_MAIN)
                    .addCategory(Intent.CATEGORY_HOME)
                    .setFlags(Intent.FLAG_ACTIVITY_NEW_TASK));
            return true;
        }
        return super.onKeyUp(keyCode, event);
    }

    // ---- native process lifecycle (via root backdoor) -----------------------

    private synchronized void startNative() {
        if (started) return;
        // One-liner runs as native root OUTSIDE the chroot (paths are /android/...).
        // 1) locate the extracted libs (device layout varies), 2) copy to tmpfs /android/a5k,
        // 3) exec the UI inside `chroot /android` (bionic linker + /system/lib + Rigel libs).
        String cmd =
            "mkdir -p /android/a5k; " +
            "L=/android/data/data/" + PKG + "/lib; " +
            "[ -f $L/liba5kui.so ] || L=$(ls -d /android/data/app-lib/" + PKG + "-* 2>/dev/null | head -1); " +
            "cp $L/liba5kui.so /android/a5k/a5kui; " +
            "cp $L/liba5kctl.so /android/a5k/a5kctl 2>/dev/null; " +
            "chmod 755 /android/a5k/a5kui /android/a5k/a5kctl 2>/dev/null; " +
            "cd /; " +
            "busybox chroot /android /system/bin/sh -c " +
                "'LD_LIBRARY_PATH=/system/lib /a5k/a5kui --emu' " +
                "> /android/a5k/boot.log 2>&1 &";
        boolean ok = RootShell.exec(cmd);
        started = ok;
        Log.i(TAG, "startNative: " + (ok ? "sent" : "FAILED (no libtweak?)"));
    }

    private synchronized void stopNative() {
        if (!started) return;
        String cmd =
            "P=$(cat /android/a5k/a5kui.pid 2>/dev/null); " +
            "[ -n \"$P\" ] && kill -TERM $P; " +
            "rm -f /android/a5k/a5kui.pid";
        RootShell.exec(cmd);
        started = false;
        Log.i(TAG, "stopNative: SIGTERM sent");
    }

    // ---- arbiter / app-manager notifications (as Track A BaseActivity) -------

    private void notifyAppInfo() {
        Intent i = new Intent("com.android.server.DAConnectionManagerService.AppInfoReceive");
        i.putExtra("package_name", getComponentName().getPackageName());
        i.putExtra("class_name", getComponentName().getClassName());
        sendBroadcast(i);
    }

    private void setAutoPowerOffEnabled(boolean enabled) {
        Intent i = new Intent("com.android.server.DAConnectionManagerService.apo");
        i.putExtra("apo_info", enabled ? "APO/NORMAL" : "APO/NO");
        sendBroadcast(i);
    }
}
