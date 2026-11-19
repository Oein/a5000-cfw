#include "ctl.h"
#include "uicore/log.h"
#include <sys/socket.h>
#include <sys/un.h>
#include <unistd.h>
#include <string.h>
#include <stdlib.h>
#include <stdio.h>

using uicore::InputEvent;

bool CtlServer::open(const char *path) {
    listen_ = socket(AF_UNIX, SOCK_STREAM, 0);
    if (listen_ < 0) { uicore::logf("ctl: socket failed"); return false; }
    struct sockaddr_un sa; memset(&sa, 0, sizeof sa);
    sa.sun_family = AF_UNIX;
    strncpy(sa.sun_path, path, sizeof sa.sun_path - 1);
    unlink(path);
    if (bind(listen_, (struct sockaddr *)&sa, sizeof sa) != 0) {
        uicore::logf("ctl: bind %s failed", path);
        ::close(listen_); listen_ = -1; return false;
    }
    listen(listen_, 4);
    strncpy(path_, path, sizeof path_ - 1);
    uicore::logf("ctl: listening on %s", path);
    return true;
}

void CtlServer::close() {
    if (listen_ >= 0) { ::close(listen_); listen_ = -1; }
    if (path_[0]) { unlink(path_); path_[0] = 0; }
}

int CtlServer::service(InputEvent *out, int max) {
    int fd = accept(listen_, 0, 0);
    if (fd < 0) return 0;
    char buf[256];
    ssize_t r = read(fd, buf, sizeof buf - 1);
    if (r <= 0) { ::close(fd); return 0; }
    buf[r] = 0;
    for (char *p = buf; *p; p++) if (*p == '\n' || *p == '\r') { *p = 0; break; }

    char cmd[32] = {0}, a1[192] = {0}, a2[32] = {0};
    int nt = sscanf(buf, "%31s %191s %31s", cmd, a1, a2);
    char reply[128]; reply[0] = 0;
    int n = 0;

    if (nt >= 2 && strcmp(cmd, "key") == 0 && max > 0) {
        out[0].kind = uicore::EV_KEY;
        out[0].raw = (int)strtol(a1, 0, 0);
        out[0].value = (nt >= 3) ? (int)strtol(a2, 0, 0) : 1;
        out[0].arg[0] = 0;
        n = 1;
        snprintf(reply, sizeof reply, "ok key %d %d\n", out[0].raw, out[0].value);
    } else if (strcmp(cmd, "shot") == 0 && max > 0) {
        out[0].kind = uicore::EV_SHOT;
        out[0].arg[0] = 0;
        if (nt >= 2) { strncpy(out[0].arg, a1, sizeof out[0].arg - 1); out[0].arg[sizeof out[0].arg - 1] = 0; }
        n = 1;
        snprintf(reply, sizeof reply, "ok shot %s\n", nt >= 2 ? a1 : "(default)");
    } else if (strcmp(cmd, "nfc") == 0 && max > 0) {
        out[0].kind = uicore::EV_NFC_TAP; out[0].arg[0] = 0; n = 1;
        snprintf(reply, sizeof reply, "ok nfc tap\n");
    } else if (strcmp(cmd, "quit") == 0 && max > 0) {
        out[0].kind = uicore::EV_QUIT; out[0].arg[0] = 0; n = 1;
        snprintf(reply, sizeof reply, "ok quit\n");
    } else {
        snprintf(reply, sizeof reply, "err unknown: %s\n", cmd);
    }
    write(fd, reply, strlen(reply));
    ::close(fd);
    return n;
}
