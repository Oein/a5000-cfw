// Shared control socket (platform code, not uicore). A unix-domain stream socket
// with a line protocol, turned into uicore InputEvents that the Input adapter feeds
// into the core loop. Used by both the a5000 and host platforms.
//   key <raw> [value]   -> EV_KEY (raw scan/key code, value default 1)
//   shot [path]         -> EV_SHOT
//   nfc                 -> EV_NFC_TAP (simulated phone tap; host fake / emulator test only)
//   quit                -> EV_QUIT
#pragma once
#include "uicore/platform.h"

class CtlServer {
public:
    CtlServer() : listen_(-1) { path_[0] = 0; }
    ~CtlServer() { close(); }
    bool open(const char *path);
    int  fd() const { return listen_; }
    // Accept one client, parse one command, write up to max events, return count.
    int  service(uicore::InputEvent *out, int max);
    void close();
private:
    int  listen_;
    char path_[256];
};
