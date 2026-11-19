package com.github.ma1co.openmemories.tweak;

/**
 * JNI 바인딩 껍데기: OpenMemories Tweak 이 설치한 libtweak.so 의
 * Java_com_github_ma1co_openmemories_tweak_Shell_nativeExec 를 그대로 쓰기 위해 같은 이름의 클래스를 둔다.
 * 구현(dev.oein.ilceremote.RootShell)이 라이브러리를 System.load 한 뒤 호출한다.
 */
public class Shell {
    public static native void nativeExec(String command) throws NativeException;
}
