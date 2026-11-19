#include "uicore/app.h"
#include "uicore/log.h"
#include "uicore/canvas.h"
#include "uicore/screen.h"
#include "uicore/keymap_shoot.h"
#include <stdio.h>

namespace uicore {

App::App()
    : canvas_(0), nScreens_(0), count_(0), infoN_(0),
      popPending_(false), stop_(false), frame_(0), lastInfoMs_(0), stockShooting_(false) {}

App::~App() {
    for (int i = 0; i < nScreens_; i++) delete screens_[i];
    delete canvas_;
}

void App::pushScreen(Screen *s) { if (nScreens_ < kScreens) screens_[nScreens_++] = s; }

const char *App::displayName() const { return plat_.display ? plat_.display->name() : "none"; }
bool App::cameraConnected() const { return plat_.camera && plat_.camera->connected(); }
bool App::cameraDryrun()    const { return plat_.camera && plat_.camera->dryrun(); }

Context App::activeContext() const {
    return nScreens_ > 0 ? screens_[nScreens_ - 1]->context() : CTX_GLOBAL;
}

bool App::init(const Options &opt, const Platform &plat) {
    opt_ = opt;
    plat_ = plat;
    log_set(plat.log);

    canvas_ = new Canvas();
    keymap_.setTable(plat.bindings, plat.bindingCount);
    if (plat.storage) keymap_.load(plat.storage);     // user remaps, if any
    shootKeymapBindStore(plat.keymapStore);           // shooting Custom Key map (conf file / in-memory)
    shootKeymapLoad();                                // defaults if no store or a bad/missing file

    // Platform main is responsible for display->init()/input->init() (so it can
    // handle backend fallback). The core only drives camera connect (it retries).
    if (plat.camera) plat.camera->connect();           // may fail until app mode; retried

    if (opt.assetsDir && *opt.assetsDir) {
        bool ok = gfx_.load(opt.assetsDir);
        logf("app: assets %s fonts=%d icons=%d", opt.assetsDir, (int)gfx_.hasFonts(), (int)gfx_.hasIcons());
        (void)ok;
    }
    model_.attach(plat.camera, plat.clock);
    model_.attachWifi(plat.wifi);
    model_.attachNfc(plat.nfc);
    pushScreen(opt.testScreen ? createTestScreen() : createShootingScreen());
    logf("app: init done display=%s camera=%s%s bindings=%d",
         displayName(), cameraConnected() ? "connected" : "disconnected",
         cameraDryrun() ? "(dry)" : "", plat.bindingCount);
    updateStockShooting();      // route-b: at the shooting root, hand live-view/OSD to stock
    return true;
}

void App::handleRaw(int raw, int value) {
    Action a = keymap_.resolve(raw, activeContext());
    dispatch(a, value, raw);
}

void App::injectAction(Action a, int value) { dispatch(a, value, 0); }

void App::dispatch(Action a, int value, int raw) {
    int idx = count_ % kLog;
    log_[idx].action = a; log_[idx].raw = raw; log_[idx].value = value;
    count_++;
    logf("action: %s raw=%d value=%d", action_name(a), raw, value);
    int sc0 = nScreens_;
    if (a != ACT_NONE && nScreens_ > 0) screens_[nScreens_ - 1]->onAction(a, value, *this);
    if (popPending_) {                                  // screen asked to be closed (never the last one)
        popPending_ = false;
        if (nScreens_ > 1) { delete screens_[--nScreens_]; }
    }
    if (nScreens_ != sc0) logf("dispatch: %s v=%d -> screens %d->%d stock=%d", action_name(a), value, sc0, nScreens_, stockShooting_ ? 1 : 0);
    updateStockShooting();      // a push/pop may have crossed the shooting-root boundary
}

// route-b stock-shooting passthrough: when only the shooting root is on the stack we hand
// the panel + input to the STOCK camera (Sony's own live-view/OSD) and keep our overlay
// dormant; the moment any screen (our MENU) is pushed on top we engage our UI. The test
// screen and the host-sim (no setStockShooting hook) never passthrough. Idempotent: the
// platform hook is called only on an actual transition.
void App::updateStockShooting() {
    bool want = plat_.setStockShooting && !opt_.testScreen && nScreens_ <= 1;
    if (want == stockShooting_) return;
    stockShooting_ = want;
    logf("app: stock-shooting passthrough %s (screens=%d)", want ? "ON (stock OSD)" : "OFF (our UI)", nScreens_);
    plat_.setStockShooting(want);
}

void App::refreshInfo() {
    if (!opt_.testScreen) return;                     // M1: the model owns camera polling
    if (!plat_.camera) { infoN_ = 0; return; }
    if (!plat_.camera->connected() && !plat_.camera->dryrun()) { plat_.camera->connect(); return; }
    infoN_ = plat_.camera->readInfo(infoIds_, infoVals_, kInfo);
    if (infoN_ < 0) infoN_ = 0;
}

const char *App::topTitle() const { return nScreens_ > 0 ? screens_[nScreens_ - 1]->title() : ""; }

void App::render() {
    if (stockShooting_) return;         // passthrough: the STOCK OSD owns the panel, we draw nothing
    canvas_->resetDirty();              // track what this draw touches -> present scans only dirty rows
    if (nScreens_ > 0) screens_[nScreens_ - 1]->draw(*canvas_, *this);
    if (plat_.display) { Rect full = { 0, 0, PANEL_W, PANEL_H }; plat_.display->present(*canvas_, full); }
    frame_++;
}

bool App::saveScreenshot(const char *path) {
    if (!path) return false;
    if (nScreens_ > 0) screens_[nScreens_ - 1]->draw(*canvas_, *this);
    bool ok = canvas_->writePPM(path);
    char pam[320]; snprintf(pam, sizeof pam, "%s.pam", path);   // alpha-preserving copy for host tools
    canvas_->writePAM(pam);
    logf("app: screenshot %s -> %s", path, ok ? "ok" : "FAIL");
    return ok;
}

int App::run() {
    long start = plat_.clock->nowMs();
    long nextFrame = start;
    long lastRenderMs = start; int warmFrames = 8;                  // render-on-change gating
    const long kMinInputRenderMs = 25;                             // immediate-render rate limit (issue (4))
    lastInfoMs_ = 0;

    while (!stop_) {
        long now = plat_.clock->nowMs();
        int timeout = (int)(nextFrame - now);
        if (timeout < 0) timeout = 0;

        InputEvent evs[32];
        int n = plat_.input ? plat_.input->poll(evs, 32, timeout) : 0;
        bool inputHandled = false;
        for (int i = 0; i < n; i++) {
            switch (evs[i].kind) {
                case EV_KEY:  handleRaw(evs[i].raw, evs[i].value); inputHandled = true; break;
                case EV_TOUCH: dispatch(ACT_TOUCH, 1, 0); inputHandled = true; break;
                case EV_SHOT: saveScreenshot(evs[i].arg[0] ? evs[i].arg : opt_.shotPath); break;
                case EV_QUIT: stop_ = true; break;
                case EV_NFC_TAP: if (plat_.nfc) plat_.nfc->injectTap(); break;
                default: break;
            }
        }

        now = plat_.clock->nowMs();
        // Immediate render on user input (issue (4): input must not wait for the next
        // frameMs tick / 500ms heartbeat). Navigation + OSD changes are applied
        // SYNCHRONOUSLY in handleRaw()->onAction(), so the canvas already reflects the
        // keypress here -- present it at once for a snappy feel. Rate-limited to
        // kMinInputRenderMs so a key/dial BURST cannot render-per-event and starve the
        // loop (the earlier unlimited fast-path regressed the slow /dev/mem software blit;
        // the default backend is now VDF detour-and-swap, and the rate limit bounds both).
        if (inputHandled && (now - lastRenderMs) >= kMinInputRenderMs) {
            render();
            lastRenderMs = now;
            nextFrame = now + opt_.frameMs;                 // reschedule the periodic tick
        } else if (inputHandled) {
            // Rate-limited this tick: do NOT make the keypress wait a whole frameMs (66ms) for the
            // periodic tick -- that is the fast-burst stutter (device 2026-11-19). Pull the next
            // render to the kMinInputRenderMs boundary after the last one so bursts render evenly.
            long due = lastRenderMs + kMinInputRenderMs;
            if (due < nextFrame) nextFrame = due;
        }
        if (now >= nextFrame) {
            if (now - lastInfoMs_ >= 2000) { refreshInfo(); lastInfoMs_ = now; }
            unsigned reads0 = model_.refreshCount();
            model_.tick();
            nfcService(*this);
            // Render only when something actually changed: a key/touch, a fresh camera
            // read (live OSD values), the warm-up frames, or a slow heartbeat (focus
            // fade / self-timer countdown). Idle no longer burns a full draw+blit every
            // frame, and input is handled without competing with a redundant render.
            bool changed = inputHandled || (model_.refreshCount() != reads0)
                         || warmFrames > 0 || (now - lastRenderMs) >= 500;
            if (changed) {
                render();
                lastRenderMs = now; if (warmFrames > 0) warmFrames--;
            }
            nextFrame = now + opt_.frameMs;
        }
        if (opt_.runSeconds > 0 && (now - start) >= (long)opt_.runSeconds * 1000) {
            logf("app: runSeconds elapsed");
            break;
        }
    }
    logf("app: loop end");
    emergencyShutdown();
    return 0;
}

void App::emergencyShutdown() {
    static bool done = false;
    if (done) return;
    done = true;
    if (plat_.camera)  plat_.camera->disconnect();
    if (plat_.display) plat_.display->shutdown();
}

} // namespace uicore
