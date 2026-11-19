// Host simulator entry point. Same libuicore, host adapters. Proves the library has
// no a5000 dependency.  a5kui_host [--sock PATH] [--shot PATH] [--seconds N] [--log PATH]
#include "uicore/app.h"
#include "uicore/platform.h"
#include "uicore/log.h"
#include <signal.h>
#include <unistd.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>

#ifndef UICORE_ASSETS_DIR
#define UICORE_ASSETS_DIR ""
#endif
using namespace uicore;

namespace host {
Input         *createInput(const char *ctlPath);
CameraControl *createCamera();
Media         *createMedia(int count);
Wifi          *createWifi(Clock *clk);
Nfc           *createNfc(Clock *clk);
Clock         *createClock();
Log           *createLog(const char *path);
Storage       *createStorage();
Display       *createDisplay(const char *mirror);
const Binding *bindings(int *count);
}

static App *g_app = 0;
static void on_signal(int sig) { if (g_app) g_app->emergencyShutdown(); _exit(128 + sig); }

int main(int argc, char **argv) {
    const char *sock = "/tmp/a5kui.sock";
    const char *logP = 0;
    int mediaCount = 14;                  // fake card contents (--media N; 0 = empty card)
    Options opt; opt.shotPath = "/tmp/a5kui.ppm"; opt.allowWrite = false; opt.frameMs = 200; opt.runSeconds = 0;
    opt.assetsDir = UICORE_ASSETS_DIR; opt.testScreen = false;
    for (int i = 1; i < argc; i++) {
        const char *a = argv[i];
        if      (!strcmp(a, "--sock")    && i + 1 < argc) sock = argv[++i];
        else if (!strcmp(a, "--shot")    && i + 1 < argc) opt.shotPath = argv[++i];
        else if (!strcmp(a, "--log")     && i + 1 < argc) logP = argv[++i];
        else if (!strcmp(a, "--seconds") && i + 1 < argc) opt.runSeconds = atoi(argv[++i]);
        else if (!strcmp(a, "--media")   && i + 1 < argc) mediaCount = atoi(argv[++i]);
        else if (!strcmp(a, "--allow-write")) opt.allowWrite = true;
        else if (!strcmp(a, "--assets")  && i + 1 < argc) opt.assetsDir = argv[++i];
        else if (!strcmp(a, "--testscreen")) opt.testScreen = true;
        else { fprintf(stderr, "a5kui_host: unknown arg %s\n", a); return 2; }
    }

    Log *log = host::createLog(logP); log_set(log);
    logf("a5kui(host) start sock=%s shot=%s", sock, opt.shotPath);

    Platform plat;
    plat.display = host::createDisplay(opt.shotPath); plat.display->init();
    plat.input   = host::createInput(sock);           plat.input->init();
    plat.camera  = host::createCamera();
    plat.clock   = host::createClock();
    plat.media   = host::createMedia(mediaCount);
    plat.wifi    = host::createWifi(plat.clock);
    plat.nfc     = host::createNfc(plat.clock);
    plat.storage = host::createStorage();
    plat.log     = log;
    plat.bindings = host::bindings(&plat.bindingCount);

    signal(SIGTERM, on_signal); signal(SIGINT, on_signal); signal(SIGPIPE, SIG_IGN);

    App app; g_app = &app;
    app.init(opt, plat);
    int rc = app.run();
    g_app = 0;
    logf("a5kui(host): exit %d", rc);
    return rc;
}
