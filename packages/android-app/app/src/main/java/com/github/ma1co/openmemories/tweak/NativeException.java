package com.github.ma1co.openmemories.tweak;

/** libtweak.so 가 실패 시 던지는 예외(JNI FindClass 대상이라 이름이 고정돼야 함). */
public class NativeException extends Exception {
    public NativeException(String message) {
        super(message);
    }
}
