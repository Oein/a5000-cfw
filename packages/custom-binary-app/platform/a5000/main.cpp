// a5000 platform entry point. Wires the a5000 adapters into libuicore, installs the
// crash-safety signals (CLAUDE.md §1), and runs. Launched by the launcher APK inside
// `busybox chroot /android` as root with --emu (NATIVE_DEPLOY §3).
#include "uicore/app.h"
#include "uicore/platform.h"
#include "uicore/log.h"
#include <signal.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <unistd.h>

using namespace uicore;

namespace a5000 {
Display       *createRigelDisplay();
Input         *createInput(const char *dev, const char *ctlPath);
CameraControl *createCamera(bool dryrun);
Clock         *createClock();
Log           *createLog(const char *path);
Storage       *createStorage(const char *path);
ShootKeymapStore *createKeymapStore(const char *path);
const Binding *bindings(int *count);
}
namespace platform { Display *createStubDisplay(const char *mirror); }

static App *g_app = 0;
static const int kFatal[] = { SIGSEGV, SIGBUS, SIGILL, SIGFPE, SIGABRT, SIGTERM, SIGINT, SIGHUP };

static void on_signal(int sig) {
    for (unsigned i = 0; i < sizeof kFatal / sizeof kFatal[0]; i++) signal(kFatal[i], SIG_DFL);
    if (g_app) g_app->emergencyShutdown();
    _exit(128 + sig);
}

static bool has_env(const char *k) { const char *v = getenv(k); return v && *v && strcmp(v, "0"); }

int main(int argc, char **argv) {
    bool rgl_dry = has_env("RGL_DRYRUN");
    bool sd_dry  = has_env("SD_DRYRUN");
    const char *display = "rigel";
    const char *event0  = "/dev/input/event0";
    const char *ctlPath = "/a5k/a5kctl.sock";     // chroot-relative (NATIVE_DEPLOY §3)
    const char *logPath = "/a5k/a5kui.log";
    const char *prefs   = "/a5k/a5kui.prefs";
    const char *keymapConf = "/setting/a5k/keymap.conf";   // persistent (rw FAT, reboot-recoverable)
    Options opt; opt.shotPath = "/a5k/a5kui.ppm"; opt.allowWrite = false; opt.frameMs = 200; opt.runSeconds = 0;

    for (int i = 1; i < argc; i++) {
        const char *a = argv[i];
        if      (!strcmp(a, "--emu") || !strcmp(a, "--device")) {}  // defaults already device
        else if (!strcmp(a, "--allow-write")) opt.allowWrite = true;
        else if (!strcmp(a, "--stub"))        display = "stub";
        else if (!strcmp(a, "--seconds") && i + 1 < argc) opt.runSeconds = atoi(argv[++i]);
        else if (!strcmp(a, "--ctl")     && i + 1 < argc) ctlPath = argv[++i];
        else if (!strcmp(a, "--shot")    && i + 1 < argc) opt.shotPath = argv[++i];
        else { fprintf(stderr, "a5kui: unknown arg %s\n", a); return 2; }
    }
    if (rgl_dry) display = "stub";

    Log *log = a5000::createLog(logPath);
    log_set(log);
    logf("a5kui(a5000) start pid=%d display=%s sd_dry=%d rgl_dry=%d allowWrite=%d",
         (int)getpid(), display, (int)sd_dry, (int)rgl_dry, (int)opt.allowWrite);

    FILE *pf = fopen("/a5k/a5kui.pid", "w");
    if (pf) { fprintf(pf, "%d\n", (int)getpid()); fclose(pf); }

    Platform plat;
    plat.display = !strcmp(display, "stub") ? platform::createStubDisplay(opt.shotPath)
                                            : a5000::createRigelDisplay();
    if (!plat.display->init()) {                        // Rigel needs app mode; fall back to stub
        logf("a5kui: display '%s' init failed -> stub", plat.display->name());
        delete plat.display;
        plat.display = platform::createStubDisplay(opt.shotPath);
        plat.display->init();
    }
    plat.input   = a5000::createInput(event0, ctlPath);
    plat.input->init();
    plat.camera  = a5000::createCamera(sd_dry);
    plat.clock   = a5000::createClock();
    plat.storage = a5000::createStorage(prefs);
    plat.keymapStore = a5000::createKeymapStore(keymapConf);
    plat.log     = log;
    plat.bindings = a5000::bindings(&plat.bindingCount);

    for (unsigned i = 0; i < sizeof kFatal / sizeof kFatal[0]; i++) signal(kFatal[i], on_signal);
    signal(SIGPIPE, SIG_IGN);

    App app; g_app = &app;
    app.init(opt, plat);
    int rc = app.run();
    g_app = 0;
    logf("a5kui(a5000): exit %d", rc);
    return rc;
}
