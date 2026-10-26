package com.fulger.popsot;

/** The launcher's native half (src/launcher_jni.c, libpopsot_launcher.so). */
final class Native {
    static { System.loadLibrary("popsot_launcher"); }

    /**
     * What a folder holds: {kind ("demo", "retail" or ""), exact ("1" when
     * default.xbe is the supported build), a line naming it, a note}.
     */
    static native String[] identify(String dir);

    private Native() {}
}
