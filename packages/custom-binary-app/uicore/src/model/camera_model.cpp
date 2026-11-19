#include "uicore/camera_model.h"
#include "uicore/log.h"
#include "uicore/apply.h"

namespace uicore {

ExposureKind exposureKindOf(int v) {
    switch (v) {
        case 0:  return EXK_AUTO;
        case 16: return EXK_PROGRAM;      // program-auto
        case 17: return EXK_SHUTTER;      // shutter-speed priority
        case 18: return EXK_APERTURE;     // aperture-priority
        case 19: return EXK_MANUAL;       // manual-exposure
        default: return EXK_SCENE;
    }
}

bool stepViaCatalog(CameraControl &cam, const CameraState &st, int id, int dir) {
    int cur = 0, nxt = 0;
    if (!st.get(id, &cur)) return false;
    if (!catalogNeighbor(id, cur, dir, &nxt)) return false;
    return cam.setSetting(id, nxt);
}

namespace {
const long kIdleReadMs = 2000;
const long kFastReadMs = 250;
const long kApplyTimeoutMs = 3500;     // a shift applies in ~2 s
const long kConnectRetryMs = 2000;
}

CameraModel::CameraModel()
    : wifi_(0), wifiOwned_(false), nfc_(0), cam_(0), clock_(0), nextRead_(0), nextConnect_(0), pollUntil_(0), sentMs_(0), focusSetMs_(0),
      inFlight_(-1), sigBefore_(0), dirty_(true), half_(false), fullHeld_(false), cdEndMs_(0), lastCaptureMs_(0), captures_(0), reads_(0), lastEvent_(0) {
    for (int i = 0; i <= AXIS_PROGRAM; i++) pend_[i] = 0;
}

void CameraModel::attach(CameraControl *cam, Clock *clock) { cam_ = cam; clock_ = clock; }

bool CameraModel::online() const {
    return cam_ && (cam_->connected() || cam_->dryrun()) && st_.valid;
}

bool CameraModel::refresh() {
    if (!cam_) return false;
    CameraState ns = st_;
    ns.valid = false;
    if (!cam_->readState(&ns)) return false;
    ns.valid = true;
    ns.focusState = st_.focusState;           // owned by the model (events/AF)
    ns.stampMs = clock_ ? clock_->nowMs() : 0;
    st_ = ns;
    syncWifi();
    reads_++;
    return true;
}

int CameraModel::signature() const {
    if (inFlight_ == AXIS_SHUTTER)  return st_.shutterNum * 100000 + st_.shutterDen;
    if (inFlight_ == AXIS_APERTURE) return st_.apertureX100;
    if (inFlight_ == AXIS_PROGRAM)  return st_.shutterNum * 100000 + st_.shutterDen + st_.apertureX100 * 7;
    return 0;
}

bool CameraModel::adjustable(Axis axis) const {
    int mv = 0; st_.get(SET_EXPOSURE_MODE, &mv);
    ExposureKind k = exposureKindOf(mv);
    int iso = 0; st_.get(SET_ISO, &iso);
    bool m = k == EXK_MANUAL, a = k == EXK_APERTURE, s = k == EXK_SHUTTER, p = k == EXK_PROGRAM;
    switch (axis) {
        case AXIS_SHUTTER:  return m || s;
        case AXIS_APERTURE: return m || a;
        case AXIS_ISO:      return m || a || s || p;
        case AXIS_EV:       return a || s || p || (m && iso == 0);
        case AXIS_PROGRAM:  return p;
    }
    return false;
}

void CameraModel::step(Axis axis, int dir) {
    if (!cam_ || dir == 0 || !st_.valid || !adjustable(axis)) return;
    if (axis == AXIS_EV || axis == AXIS_ISO) {
        int id = axis == AXIS_EV ? SET_EV : SET_ISO;
        bool ok = stepViaCatalog(*cam_, st_, id, dir);
        logf("model: step %s %+d -> %s", axis == AXIS_EV ? "EV" : "ISO", dir, ok ? "applied" : "not applied");
        refresh();
        return;
    }
    pend_[axis] += dir;
    if (clock_) sendPending(clock_->nowMs());
}

void CameraModel::sendPending(long now) {
    if (inFlight_ >= 0) return;                         // wait for the previous shift to apply
    for (int ax = AXIS_SHUTTER; ax <= AXIS_PROGRAM; ax++) {
        if (ax == AXIS_EV || ax == AXIS_ISO || pend_[ax] == 0) continue;
        int steps = pend_[ax];
        pend_[ax] = 0;
        inFlight_ = ax;
        sigBefore_ = signature();
        sentMs_ = now;
        bool ok = cam_->exposureStep(ax, steps);
        logf("model: shift axis=%d steps=%d -> %s", ax, steps, ok ? "sent" : "FAILED");
        if (!ok) { inFlight_ = -1; continue; }
        pollUntil_ = now + kApplyTimeoutMs;
        nextRead_ = now + kFastReadMs;
        return;
    }
}

bool CameraModel::setSetting(int id, int val) {
    if (!cam_) return false;
    bool ok = cam_->setSetting(id, val);
    logf("model: set %s=%d -> %s", catalogDef(id) ? catalogDef(id)->key : "?", val, ok ? "applied" : "not applied");
    refresh();
    return ok;
}

static const char *applyResultName(int r) {
    switch (r) {
        case APPLY_OK: return "applied"; case APPLY_REJECTED: return "rejected"; case APPLY_NO_ROUTE: return "no-route";
        case APPLY_UNMAPPED: return "unmapped"; case APPLY_LOCAL: return "local"; case APPLY_RECORDED: return "recorded-only";
    }
    return "?";
}

int CameraModel::applySetting(int item, int value) {
    if (item < 0 || item >= 140) return APPLY_UNMAPPED;
    int r = cam_ ? cam_->applySetting(item, value) : (int)APPLY_NO_ROUTE;
    logf("model: apply #%d %s value=%d [%s] -> %s", item, kApply[item].key, value, applyMethodName(kApply[item].method), applyResultName(r));
    if (r == APPLY_OK) refresh();
    return r;
}

void CameraModel::halfShutter(bool down) {
    if (!cam_) return;
    if (down == half_) return;                          // 516+517 both map here: dedupe
    half_ = down;
    int fm = 0; st_.get(SET_FOCUS_MODE, &fm);
    if (down) {
        if (fm == 6) return;                            // manual focus: no AF
        st_.focusState = FOCUS_BUSY;
        focusSetMs_ = clock_ ? clock_->nowMs() : 0;
        cam_->autofocus(true);
    } else {
        cam_->autofocus(false);
        focusSetMs_ = clock_ ? clock_->nowMs() : 0;
    }
}

void CameraModel::fullShutter(bool down) {
    if (!cam_) return;
    int drive = 0; st_.get(SET_DRIVE, &drive);
    bool burst = (drive == 1 || drive == 3);
    int stimer = 0; st_.get(SET_SELF_TIMER, &stimer);
    long now = clock_ ? clock_->nowMs() : 0;
    if (down && cdEndMs_) { cdEndMs_ = 0; logf("model: self-timer cancelled"); return; }
    if (down && stimer > 0 && st_.movieState == MOVIE_IDLE) {
        cdEndMs_ = now + stimer * 1000L;
        logf("model: self-timer %d s started", stimer);
        return;
    }
    if (down) {
        logf("model: shutter full press (drive=%d)", drive);
        if (burst) { fullHeld_ = cam_->shutterDown(); }
        else       { cam_->takePicture(); }
        dirty_ = true;
        nextRead_ = (clock_ ? clock_->nowMs() : 0) + 500;
    } else if (fullHeld_) {
        cam_->shutterUp();
        fullHeld_ = false;
    }
}

int CameraModel::countdownSec() const {
    if (!cdEndMs_ || !clock_) return 0;
    long left = cdEndMs_ - clock_->nowMs();
    return left <= 0 ? 1 : (int)((left + 999) / 1000);
}

void CameraModel::toggleMovie() {
    if (!cam_ || !st_.valid) return;
    bool on = st_.movieState == MOVIE_IDLE;
    cdEndMs_ = 0;
    bool ok = cam_->movieRec(on);
    logf("model: movie %s -> %s", on ? "start" : "stop", ok ? "ok" : "not applied");
    dirty_ = true; nextRead_ = clock_ ? clock_->nowMs() : 0;
}

void CameraModel::zoom(int dir) {
    if (!cam_ || dir == 0) return;
    cam_->zoom(dir);
    dirty_ = true; nextRead_ = clock_ ? clock_->nowMs() : 0;
}

void CameraModel::drainEvents(long now) {
    CameraEvent ev[8];
    int n = cam_->pollEvents(ev, 8);
    for (int i = 0; i < n; i++) {
        lastEvent_ = ev[i].kind;
        switch (ev[i].kind) {
            case CEV_STATE_CHANGED:
            case CEV_SHUTTER_CHANGED: dirty_ = true; nextRead_ = now; break;
            case CEV_FOCUS_DONE:
                st_.focusState = ev[i].a ? FOCUS_OK : FOCUS_FAIL; focusSetMs_ = now; break;
            case CEV_CAPTURE_DONE: dirty_ = true; nextRead_ = now; lastCaptureMs_ = now; captures_++; break;
            case CEV_LINK_LOST: st_.valid = false; break;
            default: break;
        }
    }
}

// The Wi-Fi backend (when it has a link / AP / connect in progress) owns the OSD icon; otherwise the
// camera-reported value stays (WIFI_NONE draws nothing).
void CameraModel::syncWifi() {
    if (!wifi_) return;
    WifiStatusInfo w; wifi_->status(&w);
    switch (w.state) {
        case WS_CONNECTING: case WS_AP_STARTING: st_.wifiStatus = WIFI_CONNECTING; break;
        case WS_CONNECTED:  st_.wifiStatus = WIFI_CONNECTED; break;
        case WS_AP_WAITING: case WS_AP_CLIENT: st_.wifiStatus = WIFI_AP; break;
        default:            // OFF / IDLE: drop our icon once, then leave the camera's own value alone
            if (wifiOwned_) st_.wifiStatus = WIFI_NONE;
            wifiOwned_ = false;
            return;
    }
    wifiOwned_ = true;
}

void CameraModel::tick() {
    syncWifi();
    if (nfc_) { NfcStatusInfo n; nfc_->status(&n); st_.nfcState = n.state; }
    if (!cam_ || !clock_) return;
    long now = clock_->nowMs();
    if (!cam_->connected() && !cam_->dryrun()) {
        if (st_.valid) { st_.valid = false; }
        if (now >= nextConnect_) { nextConnect_ = now + kConnectRetryMs; if (cam_->connect()) { dirty_ = true; nextRead_ = now; } }
        return;
    }
    drainEvents(now);
    if (cdEndMs_ && now >= cdEndMs_) {            // self-timer elapsed: fire the shot
        cdEndMs_ = 0;
        logf("model: self-timer fired");
        cam_->takePicture();
        dirty_ = true; nextRead_ = now + 500;
    }

    if (now >= nextRead_ || (dirty_ && now >= nextRead_)) {
        bool ok = refresh();
        dirty_ = false;
        long iv = (inFlight_ >= 0) ? kFastReadMs : kIdleReadMs;
        nextRead_ = now + (ok ? iv : kConnectRetryMs);
    }
    if (inFlight_ >= 0) {
        // Applied when the shifted value changed, else give up after the timeout.
        if (signature() != sigBefore_ || now > pollUntil_) {
            logf("model: shift axis=%d %s after %ldms", inFlight_, signature() != sigBefore_ ? "applied" : "timeout", now - sentMs_);
            inFlight_ = -1;
        }
    }
    if (inFlight_ < 0) sendPending(now);

    // Focus indicator fades 1.5 s after release / 3 s while held without a result.
    if (st_.focusState != FOCUS_IDLE) {
        long age = now - focusSetMs_;
        if ((!half_ && age > 1500) || (half_ && st_.focusState == FOCUS_BUSY && age > 3000)) st_.focusState = FOCUS_IDLE;
    }
}

} // namespace uicore
