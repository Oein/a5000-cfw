// a5000 input adapter: reads /dev/input/event0 (EV_KEY) and the ctl socket, and
// emits raw EV_KEY / control events. The core maps raw ISV codes to actions via the
// a5000 keymap. event0 is not grabbed, so reading in parallel with the engine is fine
// (TRACKB_RUNTIME §2). Full key routing needs app mode + the launcher's AppInfo.
#include "uicore/platform.h"
#include "uicore/log.h"
#include "../common/ctl.h"
#include <poll.h>
#include <fcntl.h>
#include <unistd.h>
#include <linux/input.h>

// <linux/input.h> defines EV_KEY as a macro (0x01) which clashes with the uicore
// EventKind enum. Capture the kernel value, then undef so EV_KEY means the enum.
#ifdef EV_KEY
static const int LINUX_EV_KEY = EV_KEY;
#undef EV_KEY
#endif

using namespace uicore;

namespace {
class A5000Input : public Input {
public:
    A5000Input(const char *dev, const char *ctlPath)
        : fd_(-1), dev_(dev), ctlPath_(ctlPath) {}

    bool init() {
        fd_ = ::open(dev_, O_RDONLY | O_NONBLOCK);
        logf("input: %s %s", dev_, fd_ >= 0 ? "opened" : "FAILED");
        if (ctlPath_) ctl_.open(ctlPath_);
        return true;
    }

    int poll(InputEvent *out, int max, int timeoutMs) {
        struct pollfd pfds[2]; int np = 0, inIdx = -1, ctlIdx = -1;
        if (fd_ >= 0)          { pfds[np].fd = fd_;        pfds[np].events = POLLIN; inIdx = np++; }
        if (ctl_.fd() >= 0)    { pfds[np].fd = ctl_.fd();  pfds[np].events = POLLIN; ctlIdx = np++; }
        int n = 0;
        if (np == 0) { usleep(timeoutMs * 1000); return 0; }
        int rc = ::poll(pfds, np, timeoutMs);
        if (rc <= 0) return 0;
        if (inIdx >= 0 && (pfds[inIdx].revents & POLLIN)) {
            struct input_event ev;
            while (n < max && ::read(fd_, &ev, sizeof ev) == (ssize_t)sizeof ev) {
                if (ev.type != LINUX_EV_KEY) continue;
                out[n].kind = EV_KEY; out[n].raw = ev.code; out[n].value = ev.value; out[n].arg[0] = 0;
                n++;
            }
        }
        if (ctlIdx >= 0 && (pfds[ctlIdx].revents & POLLIN) && n < max)
            n += ctl_.service(out + n, max - n);
        return n;
    }

    void shutdown() { if (fd_ >= 0) ::close(fd_); fd_ = -1; ctl_.close(); }

private:
    int         fd_;
    const char *dev_;
    const char *ctlPath_;
    CtlServer   ctl_;
};
}

namespace a5000 {
uicore::Input *createInput(const char *dev, const char *ctlPath) { return new A5000Input(dev, ctlPath); }
}
