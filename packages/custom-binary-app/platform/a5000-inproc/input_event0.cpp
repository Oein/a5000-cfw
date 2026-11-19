// a5000 in-process Input adapter (route (b)).
// Reads the virtual evdev /dev/input/event0 (the "D keyboard" im.elf creates via
// uinput, CLAUDE.md §5) and emits raw EV_KEY codes; the core maps raw ISV codes
// to abstract actions with the a5000 keymap. event0 is not grabbed, so reading it
// in parallel with the engine is safe (TRACKB_RUNTIME §2). Being in-process (inside
// im.elf, already a front view owner) no ABT notification is needed.
#include "uicore/platform.h"
#include "uicore/log.h"
#include <poll.h>
#include <fcntl.h>
#include <unistd.h>
#include <linux/input.h>

#ifdef EV_KEY
static const int LINUX_EV_KEY = EV_KEY;
#undef EV_KEY
#endif

using namespace uicore;

namespace {

class Event0Input : public Input {
public:
    Event0Input(const char *dev) : fd_(-1), dev_(dev) {}

    bool init() {
        fd_ = ::open(dev_, O_RDONLY | O_NONBLOCK);
        logf("input: %s %s", dev_, fd_ >= 0 ? "opened" : "FAILED (keys inert)");
        return true;  // keep running even without input
    }

    int poll(InputEvent *out, int max, int timeoutMs) {
        if (fd_ < 0) { if (timeoutMs > 0) usleep(timeoutMs * 1000); return 0; }
        struct pollfd pfd; pfd.fd = fd_; pfd.events = POLLIN;
        int rc = ::poll(&pfd, 1, timeoutMs);
        if (rc <= 0) return 0;
        int n = 0;
        struct input_event ev;
        while (n < max && ::read(fd_, &ev, sizeof ev) == (ssize_t)sizeof ev) {
            if (ev.type != LINUX_EV_KEY) continue;
            out[n].kind = EV_KEY; out[n].raw = ev.code; out[n].value = ev.value; out[n].arg[0] = 0;
            n++;
        }
        return n;
    }

    void shutdown() { if (fd_ >= 0) ::close(fd_); fd_ = -1; }

private:
    int fd_;
    const char *dev_;
};

} // namespace

namespace a5kinproc {
uicore::Input *createEvent0Input(const char *dev) { return new Event0Input(dev); }
}
