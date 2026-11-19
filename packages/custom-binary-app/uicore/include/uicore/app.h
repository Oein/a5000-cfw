// App controller (platform-agnostic). Owns the Canvas, screen stack, keymap and the
// poll/render loop, driven entirely through the Platform interfaces.
#pragma once
#include "uicore/platform.h"
#include "uicore/keymap.h"
#include "uicore/action.h"
#include "uicore/camera_model.h"
#include "uicore/gfx.h"

namespace uicore {

class Canvas;
class Screen;

struct Options {
    const char *shotPath;    // default screenshot path (ctl "shot")
    bool allowWrite;         // --allow-write gate (M0: nothing writes yet)
    int  frameMs;            // frame interval
    int  runSeconds;         // 0 = until quit/signal
    const char *assetsDir;   // baked icon/font atlases (uicore/tools); 0 = built-in 5x7 fallback
    bool testScreen;         // M0 test screen instead of the shooting OSD
};

struct ActionLog { Action action; int raw; int value; };

class App {
public:
    App();
    ~App();

    bool init(const Options &opt, const Platform &plat);
    int  run();
    void requestStop() { stop_ = true; }
    void emergencyShutdown();          // crash-safe: camera disconnect + display free

    void injectAction(Action a, int value);
    bool saveScreenshot(const char *path);
    void pushScreen(Screen *s);
    void popScreen() { popPending_ = true; }     // deferred: applied after the current dispatch

    Canvas       &canvas() { return *canvas_; }
    CameraControl *camera() { return plat_.camera; }
    Media         *media()  { return plat_.media; }
    Wifi          *wifi()   { return plat_.wifi; }
    Nfc           *nfc()    { return plat_.nfc; }
    const char    *topTitle() const;
    CameraModel   &model()  { return model_; }
    const Gfx     &gfx()    const { return gfx_; }
    long nowMs() const { return plat_.clock ? plat_.clock->nowMs() : 0; }
    const Options &opt() const { return opt_; }

    // Telemetry for the test screen.
    const ActionLog *actionLog() const { return log_; }
    int  actionLogCount() const { return count_ < kLog ? count_ : kLog; }
    int  actionLogHead()  const { return count_; }
    const int *infoIds()  const { return infoIds_; }
    const int *infoVals() const { return infoVals_; }
    int  infoCount()      const { return infoN_; }
    bool cameraConnected() const;
    bool cameraDryrun()    const;
    unsigned frame()       const { return frame_; }
    const char *displayName() const;

private:
    void handleRaw(int raw, int value);
    void dispatch(Action a, int value, int raw);
    void refreshInfo();
    void render();
    Context activeContext() const;

    enum { kLog = 10, kInfo = 12, kScreens = 8 };

    Options   opt_;
    Platform  plat_;
    Canvas   *canvas_;
    KeyMap    keymap_;
    CameraModel model_;
    Gfx       gfx_;

    Screen   *screens_[kScreens];
    int       nScreens_;

    ActionLog log_[kLog];
    int       count_;
    int       infoIds_[kInfo];
    int       infoVals_[kInfo];
    int       infoN_;

    bool      popPending_;
    bool      stop_;
    unsigned  frame_;
    long      lastInfoMs_;
    bool      stockShooting_;   // route-b: panel+input handed to the STOCK camera (shooting root); our UI dormant
    void      updateStockShooting();   // sync stockShooting_ to the screen stack + call the platform hook
};

} // namespace uicore
