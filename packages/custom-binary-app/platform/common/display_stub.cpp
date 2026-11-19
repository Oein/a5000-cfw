// PPM-mirror stub display (platform common). No real screen; writes the back buffer
// to a PPM file each present so `a5kctl shot` / host inspection can see it. Used by
// the a5000 RGL_DRYRUN path and the host build without SDL.
#include "uicore/platform.h"
#include "uicore/canvas.h"
#include "uicore/log.h"
#include <string.h>

using namespace uicore;

namespace {
class StubDisplay : public Display {
public:
    StubDisplay(const char *mirror) : frames_(0) {
        mirror_[0] = 0;
        if (mirror) { strncpy(mirror_, mirror, sizeof mirror_ - 1); mirror_[sizeof mirror_ - 1] = 0; }
    }
    bool init() { logf("display(stub): mirror=%s", mirror_[0] ? mirror_ : "(none)"); return true; }
    void present(const Canvas &c, const Rect &) { frames_++; if (mirror_[0]) c.writePPM(mirror_); }
    void shutdown() { logf("display(stub): %d frames", frames_); }
    const char *name() const { return "stub"; }
private:
    char mirror_[256];
    int  frames_;
};
}

namespace platform { uicore::Display *createStubDisplay(const char *mirror) { return new StubDisplay(mirror); } }
