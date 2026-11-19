package dev.oein.ilceremote;

import android.os.Handler;
import android.os.Looper;

import java.io.File;
import java.io.FileInputStream;

/**
 * /dev/input/event0 을 직접 읽어 다이얼(컨트롤 휠) 이벤트를 저지연으로 받는다.
 *
 * 배경: ScalarInput 의 onCameraKeyDown 경로는 커널이 ~50ms(20/s) 간격으로 내보내는
 * 다이얼 이벤트를 ~145ms(7/s) 로 느리게 전달한다(프레임워크 페이싱). event0 을 직접
 * 읽으면 커널 속도 그대로 받을 수 있어 휠 반응이 ~3배 빨라진다.
 *
 * 접근 권한: 일반 앱 uid 는 input 그룹(1004)이 없어 기본 0660 노드를 열 수 없다.
 * 개발 환경에서는 네이티브 root(telnet)로 `chmod 666 /android/dev/input/event0`,
 * 최종 펌웨어에서는 시스템 권한으로 돈다. 열기에 실패하면 start()가 false 를 반환하고
 * 호출측은 기존 ScalarInput 경로로 폴백한다.
 *
 * input_event (32-bit ARM, 16 bytes, little-endian):
 *   [0..3] tv_sec  [4..7] tv_usec  [8..9] type  [10..11] code  [12..15] value
 */
final class DialReader {

    interface Listener {
        /** direction: +1 시계방향, -1 반시계방향. UI 스레드에서 호출된다. */
        void onFastDial(int direction);
    }

    private static final String NODE = "/dev/input/event0";
    private static final int EV_KEY = 0x0001;

    private final Listener listener;
    private final Handler ui = new Handler(Looper.getMainLooper());
    private volatile boolean running;
    private Thread thread;

    DialReader(Listener listener) {
        this.listener = listener;
    }

    /** 읽기에 성공하면 true, 권한/장치 문제로 열 수 없으면 false(→ 폴백). */
    boolean start() {
        File f = new File(NODE);
        if (!f.exists() || !f.canRead()) {
            Logger.info("DialReader: " + NODE + " not readable (canRead=" + f.canRead() + ") → fallback to ScalarInput");
            return false;
        }
        running = true;
        thread = new Thread(new Runnable() {
            @Override public void run() { loop(); }
        }, "DialReader");
        thread.setDaemon(true);
        thread.start();
        Logger.info("DialReader: started, reading " + NODE + " directly");
        return true;
    }

    void stop() {
        running = false;
        Thread t = thread;
        if (t != null) t.interrupt();
        thread = null;
    }

    private void loop() {
        FileInputStream in = null;
        try {
            in = new FileInputStream(NODE);
            byte[] buf = new byte[16];
            while (running) {
                if (!readFully(in, buf)) break;
                int type = u16(buf, 8);
                int code = u16(buf, 10);
                int value = s32(buf, 12);
                // 디텐트 1칸 = press(1)+release(0). press 에서만 카운트해 디텐트당 1회.
                if (type == EV_KEY && value == 1 && ShootingControls.isDialKey(code)) {
                    final int dir = ShootingControls.dialDirection(code);
                    if (dir != 0) {
                        ui.post(new Runnable() {
                            @Override public void run() { listener.onFastDial(dir); }
                        });
                    }
                }
            }
        } catch (Exception e) {
            Logger.info("DialReader: read loop ended: " + e);
        } finally {
            if (in != null) {
                try { in.close(); } catch (Exception ignore) {}
            }
        }
    }

    private boolean readFully(FileInputStream in, byte[] buf) throws java.io.IOException {
        int off = 0;
        while (off < buf.length) {
            int n = in.read(buf, off, buf.length - off);
            if (n < 0) return false;
            off += n;
        }
        return true;
    }

    private static int u16(byte[] b, int i) {
        return (b[i] & 0xff) | ((b[i + 1] & 0xff) << 8);
    }

    private static int s32(byte[] b, int i) {
        return (b[i] & 0xff) | ((b[i + 1] & 0xff) << 8)
                | ((b[i + 2] & 0xff) << 16) | ((b[i + 3] & 0xff) << 24);
    }
}
