package dev.oein.ilceremote;

import android.content.Context;

import java.io.File;
import java.io.FileInputStream;
import java.io.FileOutputStream;
import java.io.InputStream;

/**
 * MF 초점 거리(순정 "1.5m" 표시와 같은 값).
 *
 * 값은 SDCom/CameraEx 어디에도 없고 im.elf 의 ModelCamera 객체(+148 거리 mm, +152 near, +153 무한대)에만 있다
 * (re/notes/mf_distance.md). root 헬퍼 focusmem(app/native/focusmem, assets 포함)이 /proc/<im.elf>/mem 을
 * 읽기 전용으로 100ms 마다 읽어 /android/a5k/focus.txt("dist near inf")에 쓰고, 앱은 그 파일을 읽는다.
 * 헬퍼는 RootShell(Tweak 경유)로 앱 프로세스당 1회 띄우며, 앱 pid 가 사라지면 스스로 종료한다.
 * 모든 쓰기는 RAM tmpfs(/android/a5k) — 플래시 기록 없음.
 */
final class FocusDistance {
    /** 앱(chroot) 기준 경로 = 네이티브 /android/a5k/focus.txt */
    private static final String FILE = "/a5k/focus.txt";
    private static boolean started;

    private FocusDistance() {}

    /** 헬퍼 시작(앱 프로세스당 1회, 백그라운드 스레드). */
    static synchronized void start(final Context context) {
        if (started) return;
        started = true;
        new Thread(new Runnable() {
            @Override public void run() {
                try {
                    File bin = new File(context.getFilesDir(), "focusmem");
                    copyAsset(context, "focusmem", bin);
                    String cmd = "mkdir -p /android/a5k;cd /android/a5k;cp /android" + bin.getAbsolutePath()
                            + " fm;chmod 755 fm;./fm serve $(busybox pidof im.elf) " + android.os.Process.myPid()
                            + " /android/a5k/focus.txt >fm.log 2>&1 &";
                    boolean ok = RootShell.exec(cmd);
                    Logger.info("FocusDistance: helper " + (ok ? "started" : "start failed") + " (" + cmd.length() + " chars)");
                } catch (Throwable t) {
                    Logger.error("FocusDistance start failed", t);
                }
            }
        }, "FocusDistance").start();
    }

    /** 바이너리가 없거나 크기가 다르면 assets 에서 복사(앱 전용 저장소, 변경 시에만). */
    private static void copyAsset(Context context, String name, File dst) throws Exception {
        InputStream in = context.getAssets().open(name);
        try {
            int size = in.available();
            if (dst.exists() && dst.length() == size) return;
            FileOutputStream out = new FileOutputStream(dst);
            try {
                byte[] buf = new byte[8192];
                int n;
                while ((n = in.read(buf)) > 0) out.write(buf, 0, n);
            } finally {
                out.close();
            }
        } finally {
            in.close();
        }
    }

    /** {dist(mm, -1=무한대), near(0/1), inf(0/1)}; 아직 값이 없으면 null. */
    static int[] read() {
        FileInputStream in = null;
        try {
            in = new FileInputStream(FILE);
            byte[] buf = new byte[48];
            int n = in.read(buf);
            if (n <= 0) return null;
            String[] parts = new String(buf, 0, n, "US-ASCII").trim().split(" ");
            if (parts.length < 3) return null;
            return new int[] { Integer.parseInt(parts[0]), Integer.parseInt(parts[1]), Integer.parseInt(parts[2]) };
        } catch (Throwable t) {
            return null;
        } finally {
            if (in != null) try { in.close(); } catch (Throwable ignore) {}
        }
    }

    /** 순정 ACT_FocusDistance 와 같은 표기: ∞ / "12m" / "1.5m". 표시할 수 없는 값이면 null. */
    static String label(int dist, boolean inf) {
        if (inf || dist < 0 || dist > 999998) return "∞";
        if (dist < 100) return null;
        if (dist >= 10000) return ((dist + 500) / 1000) + "m";
        int tenths = (dist + 50) / 100;
        return (tenths / 10) + "." + (tenths % 10) + "m";
    }
}
