package dev.oein.ilceremote;

import java.io.File;

/**
 * 네이티브 root 로 셸 명령 실행(비동기, 결과 없음).
 *
 * OpenMemories Tweak 의 "Enable Telnet" 과 같은 경로: libtweak.so 의 shell_exec_async 가
 * OSAL 메시지(0x94020A, cmd 0x940005)를 네이티브 bootin 에 보내고, bootin 이 /bin/sh 로 root 실행한다.
 * 실행 위치는 chroot 바깥(네이티브)이라 Android 경로는 /android/... 로 써야 한다.
 * 명령은 255자 이하. Tweak 이 없으면 false(→ 호출측 폴백).
 */
final class RootShell {
    private static final String LIB = "/data/data/com.github.ma1co.openmemories.tweak/lib/libtweak.so";
    private static Boolean loaded;

    private RootShell() {}

    static synchronized boolean exec(String command) {
        if (loaded == null) {
            try {
                if (!new File(LIB).canRead()) throw new IllegalStateException("not found: " + LIB);
                System.load(LIB);
                loaded = Boolean.TRUE;
            } catch (Throwable t) {
                Logger.info("RootShell: libtweak load failed: " + t);
                loaded = Boolean.FALSE;
            }
        }
        if (!loaded.booleanValue()) return false;
        try {
            com.github.ma1co.openmemories.tweak.Shell.nativeExec(command);
            return true;
        } catch (Throwable t) {
            Logger.info("RootShell: exec failed: " + t);
            return false;
        }
    }
}
