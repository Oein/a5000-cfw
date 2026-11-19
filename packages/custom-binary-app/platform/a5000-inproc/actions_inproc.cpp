// a5000 in-process executor for the menu's NON-SETTING items (app launch + stock action views).
// This is the route-b counterpart of menu_actions.cpp (uicore, platform-agnostic): the UI matches a
// row and calls CameraControl::systemAction(kind,arg); InprocCamera::systemAction forwards here.
// Kept OUT of camera_inproc.cpp (settings/apply code) on purpose.
//
// Spec + the 43-item map: re/menu/app_launch_and_actions.md. Brick-0 (CLAUDE.md s1): everything here
// is RAM/mount only -- NOTHING writes Backup.bin / flash / NAND. The destructive gating already ran
// UI-side (confirm modal + EXPERIMENTAL gate in menu_actions.cpp); this layer only performs the
// (RAM-only) hand-off and never second-guesses it into a flash write.
#include "engine.h"
#include "uicore/platform.h"       // SysAction / SysActionResult
#include "uicore/log.h"
#include <unistd.h>
#include <sys/wait.h>
#include <string.h>
#include <stdlib.h>

namespace a5kinproc {

using uicore::logf;
using uicore::SYS_OPEN_VIEW;
using uicore::SYS_MODEL_EXEC;
using uicore::SYS_LAUNCH_APP;
using uicore::SA_DONE;
using uicore::SA_DEVICE_SEAL;
using uicore::SA_UNSUPPORTED;

// HAND-TO-STOCK (display_rigel.cpp): step our route-b overlay back + release input so
// an opened stock view is visible and interactive; it auto-resumes when the view closes.
void handToStock_begin(Engine *eng);

// Launch an Android PMCA app (OM Tweak etc.) through the chroot ActivityManager CLI. `am` runs in
// RAM and writes nothing to flash (brick-0). component = "pkg/.Class"; 0 = fall through to the Sony
// launcher path (handled by the caller). Returns 0 if `am` was spawned, -1 on fork/exec failure.
//
// [device-seal] `am start` brings the ACTIVITY up but NOT the display/input: the Android-world
// hand-off needs the Sony ABT arbiter (ABT_set_state(... ANDROID ...)) + SyncAndroid_act/_res, whose
// exact arg sequence must be captured on device (strace / ABT dump around stock Application tab).
// The route-b Rigel overlay teardown + that ABT sequence belong RIGHT HERE once observed
// (app_launch_and_actions.md §1.3/§4). Until then the activity may start behind our overlay.
static int spawn_am_start(const char *component) {
    if (!component || !*component) return -1;
    pid_t pid = fork();
    if (pid < 0) return -1;
    if (pid == 0) {
        // child: chroot /android /system/bin/am start -n <component>
        execl("/usr/bin/chroot", "chroot", "/android",
              "/system/bin/am", "start", "-n", component, (char *)0);
        _exit(127);
    }
    // parent: reap non-blocking-ish (am returns quickly); don't hang the UI thread
    int st = 0; waitpid(pid, &st, WNOHANG);
    return 0;
}

// Default OM-Tweak component (memory + app_launch_and_actions.md §1.2). The DIRECT launch is gated:
// only taken when the experimental marker is present (A5K_LAUNCH_DIRECT / the UI's own gate already
// bounded the destructive set; app-launch itself is brick-0 so it always runs the launcher path).
static const char *OM_TWEAK_COMPONENT =
    "com.github.ma1co.openmemories.tweak/.MainActivity";

// Forwarded from InprocCamera::systemAction (camera_inproc.cpp). eng may be 0.
int runSystemAction(Engine *eng, int kind, const char *arg) {
    switch (kind) {
        case SYS_LAUNCH_APP: {
            // App-launch needs the Sony ABT world hand-off (ANDROID state) which is NOT yet verified
            // on device: openStockView("ALAUNCHER")+am-start DISPATCH (device log openView=0 am=0) but
            // the display/input hand-off never completes, so stepping OUR overlay back just dumps the
            // user into stock shooting with no launcher until the 60s hand-to-stock timeout (device
            // 2026-11-19, issue 5). handToStock_begin() ALSO suspends our present, so even the
            // "device-seal" toast never shows. => By DEFAULT do NOT hand to stock: report device-seal
            // so the menu shows a toast and the user STAYS in our UI (no drop to stock). The real
            // hand-off (handToStock + openView + ABT) is gated behind an explicit experimental marker
            // (A5K_LAUNCH_DIRECT env, or the /android/a5k/launch_direct file for the ptrace-inject path
            // where env cannot be set) until the ABT sequence is captured and wired.
            const char *comp = (arg && *arg) ? arg : OM_TWEAK_COMPONENT;
            const char *ld = getenv("A5K_LAUNCH_DIRECT");
            bool tryLaunch = (ld && *ld && *ld != '0') ||
                             (::access("/android/a5k/launch_direct", F_OK) == 0);
            if (!tryLaunch) {
                logf("action: launch-app %s -> device-seal (ABT world hand-off not wired; staying in UI)", comp);
                return SA_DEVICE_SEAL;                            // UI shows toast, NO drop to stock
            }
            if (eng) handToStock_begin(eng);                      // step our overlay back + release input
            int sv = eng ? eng->openStockView("ALAUNCHER") : 1;   // ScalarALauncher view (device-seal)
            int am = spawn_am_start(comp);
            logf("action: launch-app comp=%s openView=%d am=%d (DIRECT gated)", comp, sv, am);
            // am spawned = we did something, but the WORLD hand-off (ABT) is unverified -> device-seal.
            return SA_DEVICE_SEAL;
        }
        case SYS_MODEL_EXEC: {
            // AIRPLANE toggle -- STATIC RE-PROVEN WRITE (2026-11-18, re/menu/app_launch_and_actions.md
            // sec.2.2 + re/scalar/settings_readwrite_full.md). The stock AirplaneMode menu item is a
            // CmnViewProcessDataMgr GROUP setting: settings_tree.txt `AirplaneMode 13=181` and the
            // `13=` field is the data-mgr eventId (PROVEN: Flash 13=8, DRO 13=24, Metering 13=22,
            // Drive 13=7, Focus 13=13, ExpComp 13=20 all match the disassembled Wrapper getters that
            // call mgr->vtable[3](mgr, eventId, &raw)). Leaf raws: On=`15=0`, Off=`15=1`. So the
            // airplane toggle write == cvpdmGroupWrite(181, raw) -- the SAME proven route-B W1 path
            // metering/drive/focus-area already use, read-back guarded. RAM/engine state (the engine
            // persists it exactly as the stock menu toggle does = "menu-write equivalent", NOT a raw
            // Backup/flash write) -> brick-0, safe action (CLAUDE.md sec.1 airplane = safe set). This
            // is a toggle (the action carries no value): read group 181, flip, write, read-back.
            if (eng && eng->ok() && arg && strstr(arg, "AIRPLANE")) {
                int cur = -1;
                if (eng->cvpdmGroupRead(181, &cur)) {
                    int nv = (cur == 0) ? 1 : 0;            // flip: raw 0 = ON, raw 1 = OFF
                    eng->cvpdmGroupWrite(181, nv);
                    usleep(200 * 1000);                     // let the appFw pump apply
                    int rb = -1;
                    bool ok = eng->cvpdmGroupRead(181, &rb) && rb == nv;
                    logf("action: airplane toggle group181 raw %d->%d read-back %d => %s",
                         cur, nv, rb, ok ? (nv == 0 ? "ON" : "OFF") : "device-seal(no read-back)");
                    return ok ? SA_DONE : SA_DEVICE_SEAL;
                }
                logf("action: airplane group181 read failed (CVPDM not constructed?) -> device-seal");
                return SA_DEVICE_SEAL;
            }
            // Other stock toggles: the exact `cmd`/eventId is still device-seal (not yet RE'd); we do
            // NOT fire a guessed cmd (that could nudge engine state) -> report staged.
            logf("action: model-exec %s -> cmd device-seal (no blind enqueue)", arg ? arg : "?");
            return SA_DEVICE_SEAL;
        }
        case SYS_OPEN_VIEW: {
            if (eng) handToStock_begin(eng);        // step our overlay back + release input so the view shows
            int r = eng ? eng->openStockView(arg) : 1;
            logf("action: open-view %s -> %s", arg ? arg : "?", r == 0 ? "dispatched" : "device-seal");
            return r == 0 ? SA_DONE : SA_DEVICE_SEAL;
        }
        default:
            return SA_UNSUPPORTED;
    }
}

} // namespace a5kinproc
