// Host simulator adapters: proves libuicore has no a5000 dependency. Display = SDL2
// window (HAVE_SDL) else PPM stub; Input = ctl socket only; CameraControl = fake;
// Clock/Log/Storage = plain libc.
#include "uicore/platform.h"
#include "uicore/canvas.h"
#include "uicore/log.h"
#include "../common/ctl.h"
#include <poll.h>
#include <unistd.h>
#include <string.h>
#include <stdio.h>
#include <sys/time.h>
#include <time.h>

#ifdef HAVE_SDL
#include <SDL2/SDL.h>
#endif

using namespace uicore;

namespace {

// ---- Input: ctl only (keys injected with a5kctl) ----
class HostInput : public Input {
public:
    HostInput(const char *ctlPath) : ctlPath_(ctlPath) {}
    bool init() { if (ctlPath_) ctl_.open(ctlPath_); return true; }
    int poll(InputEvent *out, int max, int timeoutMs) {
        if (ctl_.fd() < 0) { usleep(timeoutMs * 1000); return 0; }
        struct pollfd p = { ctl_.fd(), POLLIN, 0 };
        int rc = ::poll(&p, 1, timeoutMs);
        if (rc > 0 && (p.revents & POLLIN)) return ctl_.service(out, max);
        return 0;
    }
    void shutdown() { ctl_.close(); }
private:
    const char *ctlPath_;
    CtlServer   ctl_;
};

// ---- Clock / Log / Storage ----
class HostClock : public Clock {
public: long nowMs() { struct timeval t; gettimeofday(&t, 0); return (long)t.tv_sec * 1000 + t.tv_usec / 1000; }
};
class HostLog : public Log {
public:
    HostLog(const char *path) : f_(0) { if (path && *path) f_ = fopen(path, "a"); }
    void line(const char *s) {
        struct timeval tv; gettimeofday(&tv, 0); struct tm tmv; localtime_r(&tv.tv_sec, &tmv);
        char ts[32]; snprintf(ts, sizeof ts, "%02d:%02d:%02d.%03d", tmv.tm_hour, tmv.tm_min, tmv.tm_sec, (int)(tv.tv_usec/1000));
        fprintf(stderr, "%s %s\n", ts, s); fflush(stderr);
        if (f_) { fprintf(f_, "%s %s\n", ts, s); fflush(f_); }
    }
private: FILE *f_;
};
class HostStorage : public Storage {};   // no-op for host M0

// ---- Display: SDL window or PPM stub ----
#ifdef HAVE_SDL
class SdlDisplay : public Display {
public:
    SdlDisplay(const char *mirror) : win_(0), ren_(0), tex_(0), frames_(0) {
        mirror_[0] = 0; if (mirror) { strncpy(mirror_, mirror, sizeof mirror_ - 1); mirror_[sizeof mirror_ - 1] = 0; }
    }
    bool init() {
        if (SDL_Init(SDL_INIT_VIDEO) != 0) { logf("host: SDL_Init failed"); return false; }
        win_ = SDL_CreateWindow("a5kui host", SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED, PANEL_W, PANEL_H, SDL_WINDOW_SHOWN);
        if (!win_) return false;
        ren_ = SDL_CreateRenderer(win_, -1, 0);
        tex_ = SDL_CreateTexture(ren_, SDL_PIXELFORMAT_RGBA32, SDL_TEXTUREACCESS_STREAMING, PANEL_W, PANEL_H);
        logf("host: SDL window up"); return true;
    }
    void present(const Canvas &c, const Rect &) {
        frames_++;
        SDL_UpdateTexture(tex_, 0, c.pixels(), PANEL_W * 4);
        SDL_RenderClear(ren_); SDL_RenderCopy(ren_, tex_, 0, 0); SDL_RenderPresent(ren_); SDL_PumpEvents();
        if (mirror_[0]) c.writePPM(mirror_);
    }
    void shutdown() { if (tex_) SDL_DestroyTexture(tex_); if (ren_) SDL_DestroyRenderer(ren_); if (win_) SDL_DestroyWindow(win_); SDL_Quit(); }
    const char *name() const { return "host"; }
private:
    SDL_Window *win_; SDL_Renderer *ren_; SDL_Texture *tex_; char mirror_[256]; int frames_;
};
#endif

} // namespace

namespace platform { Display *createStubDisplay(const char *mirror); CameraControl *createFakeCamera(); Media *createFakeMedia(int count); Wifi *createFakeWifi(Clock *clk); Nfc *createFakeNfc(Clock *clk); }

namespace host {
uicore::Input         *createInput(const char *ctlPath) { return new HostInput(ctlPath); }
uicore::CameraControl *createCamera()                   { return platform::createFakeCamera(); }
uicore::Media         *createMedia(int count)           { return platform::createFakeMedia(count); }
uicore::Wifi          *createWifi(uicore::Clock *clk)   { return platform::createFakeWifi(clk); }
uicore::Nfc           *createNfc(uicore::Clock *clk)    { return platform::createFakeNfc(clk); }
uicore::Clock         *createClock()                    { return new HostClock(); }
uicore::Log           *createLog(const char *path)      { return new HostLog(path); }
uicore::Storage       *createStorage()                  { return new HostStorage(); }
uicore::Display       *createDisplay(const char *mirror) {
#ifdef HAVE_SDL
    return new SdlDisplay(mirror);
#else
    return platform::createStubDisplay(mirror);
#endif
}
}
