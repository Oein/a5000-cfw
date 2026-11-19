package com.github.ma1co.openmemories.tweak;

/**
 * Binding to libtweak.so's native root-shell entry point. The JNI symbol
 * Java_com_github_ma1co_openmemories_tweak_Shell_nativeExec is provided by the
 * installed OpenMemories-Tweak libtweak.so, which RootShell loads with System.load().
 * Declared here (same package/signature) so the launcher builds without the
 * OpenMemories framework dependency. See app/docs/NATIVE_DEPLOY.md §1.
 */
public class Shell {
    public static native void nativeExec(String command);
}
