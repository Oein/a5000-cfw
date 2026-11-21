// In-app debug command channel (DEBUG BUILD ONLY).
//
// Whole body is gated by A5K_DEBUG (make DEBUG=1). In the default build only the
// empty startDebugChannel() stub below is compiled, so nothing ships in release.
//
// Mechanism: a detached thread polls /android/a5k/dbgcmd (RAM tmpfs) every ~200ms,
// executes each line, appends results to /android/a5k/dbgout, then truncates dbgcmd.
// Only brick-safe backends are called (SDCom 0x1003 / engine vtable set / reads).
#include "engine.h"
#include "uicore/platform.h"

namespace a5kinproc {
#ifdef A5K_DEBUG
SdcomClient *debugSdcom(uicore::CameraControl *cam);   // camera_inproc.cpp (A5K_DEBUG only)
#endif
void startDebugChannel(uicore::CameraControl *cam, Engine *eng);
}

#ifdef A5K_DEBUG
#include "uicore/log.h"
#include <pthread.h>
#include <fcntl.h>
#include <unistd.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>
#include <time.h>

namespace a5kinproc {
namespace {

const char *kCmd = "/android/a5k/dbgcmd";
const char *kOut = "/android/a5k/dbgout";

struct Ctx { uicore::CameraControl *cam; Engine *eng; SdcomClient *sdc; };

void out(const char *fmt, ...) {
    char buf[320];
    va_list ap; va_start(ap, fmt);
    vsnprintf(buf, sizeof buf, fmt, ap);
    va_end(ap);
    uicore::logf("dbg: %s", buf);
    int fd = open(kOut, O_WRONLY | O_CREAT | O_APPEND, 0666);
    if (fd < 0) return;
    size_t n = strlen(buf);
    if (write(fd, buf, n) < 0) {}
    if (write(fd, "\n", 1) < 0) {}
    close(fd);
}

// parse int (dec or 0x hex); false on junk
bool num(const char *s, int *v) {
    if (!s || !*s) return false;
    char *e = 0;
    long x = strtol(s, &e, 0);
    if (e == s || *e) return false;
    *v = (int)x;
    return true;
}

const char *srName(int r) {
    switch (r) {
    case SR_OK: return "SR_OK"; case SR_REJECT: return "SR_REJECT";
    case SR_NOCONN: return "SR_NOCONN"; case SR_NOLIB: return "SR_NOLIB";
    case SR_DRYRUN: return "SR_DRYRUN"; default: return "SR_?";
    }
}

void exec(Ctx *c, char *line) {
    char *tok[6]; int n = 0;
    char *sp = 0;
    for (char *t = strtok_r(line, " \t\r\n", &sp); t && n < 6; t = strtok_r(0, " \t\r\n", &sp)) tok[n++] = t;
    if (n == 0 || tok[0][0] == '#') return;
    const char *cmd = tok[0];
    int a = 0, b = 0;

    if (!strcmp(cmd, "help")) {
        out("cmds: setparam <id> <val> | setsetting <catalogId> <val> | getparam <id> [selector=15] | "
            "getgroup <group> | sysaction <kind> [arg] | help");
    } else if (!strcmp(cmd, "setparam")) {
        if (n < 3 || !num(tok[1], &a) || !num(tok[2], &b)) { out("setparam: usage <id> <val>"); return; }
        if (!c->sdc) { out("setparam %d %d -> no SdcomClient", a, b); return; }
        int r = c->sdc->setParam(a, b);
        out("setparam %d %d -> %s (%d)", a, b, srName(r), r);
    } else if (!strcmp(cmd, "setsetting")) {
        if (n < 3 || !num(tok[1], &a) || !num(tok[2], &b)) { out("setsetting: usage <catalogId> <val>"); return; }
        if (!c->cam) { out("setsetting -> no camera"); return; }
        bool ok = c->cam->setSetting(a, b);
        out("setsetting %d %d -> %s", a, b, ok ? "true" : "false");
    } else if (!strcmp(cmd, "getparam")) {
        int sel = 15;
        if (n < 2 || !num(tok[1], &a) || (n >= 3 && !num(tok[2], &sel))) { out("getparam: usage <id> [selector]"); return; }
        if (!c->eng || !c->eng->ok()) { out("getparam -> engine not ready"); return; }
        int v = 0;
        bool ok = c->eng->readModelParam("model/CAMERA", (unsigned long)sel, (unsigned)a, &v);
        if (ok) out("getparam 0x%x sel=%d -> %d (0x%x)", a, sel, v, v);
        else    out("getparam 0x%x sel=%d -> read failed", a, sel);
    } else if (!strcmp(cmd, "getgroup")) {
        if (n < 2 || !num(tok[1], &a)) { out("getgroup: usage <group>"); return; }
        if (!c->eng || !c->eng->ok()) { out("getgroup -> engine not ready"); return; }
        int v = 0;
        bool ok = c->eng->cvpdmGroupRead(a, &v);
        if (ok) out("getgroup %d -> %d (0x%x)", a, v, v);
        else    out("getgroup %d -> read failed", a);
    } else if (!strcmp(cmd, "sysaction")) {
        if (n < 2 || !num(tok[1], &a)) { out("sysaction: usage <kind> [arg]"); return; }
        if (!c->cam) { out("sysaction -> no camera"); return; }
        int r = c->cam->systemAction(a, n >= 3 ? tok[2] : 0);
        out("sysaction %d %s -> %d", a, n >= 3 ? tok[2] : "", r);
    } else {
        out("unknown: %s", cmd);
    }
}

void *dbgThread(void *arg) {
    Ctx *c = (Ctx *)arg;
    out("debug channel up (%s)", kCmd);
    for (;;) {
        usleep(200 * 1000);
        int fd = open(kCmd, O_RDWR);
        if (fd < 0) continue;
        char buf[2048];
        ssize_t r = read(fd, buf, sizeof buf - 1);
        if (r > 0) {
            buf[r] = 0;
            if (ftruncate(fd, 0) < 0) {}          // consume first -> never re-run
            close(fd);
            char *sp = 0;
            for (char *ln = strtok_r(buf, "\n", &sp); ln; ln = strtok_r(0, "\n", &sp)) exec(c, ln);
        } else {
            close(fd);
        }
    }
    return 0;
}

} // namespace

void startDebugChannel(uicore::CameraControl *cam, Engine *eng) {
    static Ctx ctx;
    ctx.cam = cam; ctx.eng = eng; ctx.sdc = debugSdcom(cam);
    pthread_t t;
    if (pthread_create(&t, 0, dbgThread, &ctx) == 0) pthread_detach(t);
}
} // namespace a5kinproc

#else  // !A5K_DEBUG : release stub, links to nothing
namespace a5kinproc {
void startDebugChannel(uicore::CameraControl *, Engine *) {}
}
#endif
