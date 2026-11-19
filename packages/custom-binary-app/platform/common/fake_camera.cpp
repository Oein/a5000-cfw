// Fake CameraControl with plausible a5000 behaviour, for the host simulator and for
// SD_DRYRUN on the device: a snapshot like SDCom 0x1002, async shutter/aperture shifts
// (applied after ~1.5 s like the real camera), mode gating (EV/ISO set silently ignored
// in AUTO/scene modes), AF result events, capture counting. Platform helper (not core).
#include "uicore/platform.h"
#include "uicore/camera_model.h"
#include "uicore/log.h"
#include <string.h>
#include <stdlib.h>
#include <stdio.h>
#include <sys/time.h>

using namespace uicore;

namespace {

long nowMs() { struct timeval t; gettimeofday(&t, 0); return (long)t.tv_sec * 1000 + t.tv_usec / 1000; }

struct SS { int n, d; };
const SS kSS[] = {   // 1/3-stop ladder, slow -> fast
    {30,1},{25,1},{20,1},{15,1},{13,1},{10,1},{8,1},{6,1},{5,1},{4,1},{3,1},{25,10},{2,1},{16,10},{13,10},{1,1},
    {10,13},{10,16},{1,2},{1,3},{1,4},{1,5},{1,6},{1,8},{1,10},{1,13},{1,15},{1,20},{1,25},{1,30},{1,40},{1,50},
    {1,60},{1,80},{1,100},{1,125},{1,160},{1,200},{1,250},{1,320},{1,400},{1,500},{1,640},{1,800},{1,1000},
    {1,1250},{1,1600},{1,2000},{1,2500},{1,3200},{1,4000}
};
const int kNSS = (int)(sizeof kSS / sizeof kSS[0]);
const int kAp[] = { 180,200,220,250,280,320,350,400,450,500,560,630,710,800,900,1000,1100,1300,1400,1600,1800,2000,2200 };
const int kNAp = (int)(sizeof kAp / sizeof kAp[0]);

class FakeCamera : public CameraControl {
public:
    FakeCamera() : connected_(false), ssIdx_(42), apIdx_(0), mode_(19), ev_(0), iso_(0), drive_(0), selfTimer_(0),
                   focusMode_(6), afMode_(1), shots_(38), battery_(98), applyAt_(0), applyAxis_(-1), applySteps_(0),
                   evHead_(0), evTail_(0), afAt_(0), afHeld_(false), movie_(false), movieStart_(0), zoomPm_(250) {
        memset(param_, 0, sizeof param_); memset(have_, 0, sizeof have_);
        const char *m = getenv("A5K_FAKE_MODE");      // e.g. 16 = P, 18 = A, 19 = M (default), 0 = AUTO
        if (m && *m) mode_ = atoi(m);
        const char *f = getenv("A5K_FAKE_FOCUS");     // 0 = AF, 6 = MF (default), 10 = DMF
        if (f && *f) focusMode_ = atoi(f);
        const char *t = getenv("A5K_FAKE_SELFTIMER"); // 0 (default) / 2 / 10
        if (t && *t) selfTimer_ = atoi(t);
        const char *b = getenv("A5K_FAKE_BATTERY");   // percent
        if (b && *b) battery_ = atoi(b);
    }

    bool connect() { connected_ = true; logf("fake-camera: connected"); return true; }
    void disconnect() { connected_ = false; }
    bool connected() const { return connected_; }
    bool dryrun() const { return false; }
    int  readInfo(int *ids, int *vals, int max) {
        int n = 0;
        if (n < max) { ids[n] = 97; vals[n] = 0x1000a; n++; }
        if (n < max) { ids[n] = 69; vals[n] = 2;       n++; }
        return n;
    }

    bool readState(CameraState *o) {
        service();
        o->reset();
        set(o, SET_EXPOSURE_MODE, mode_); set(o, SET_EV, ev_); set(o, SET_ISO, iso_);
        set(o, SET_DRIVE, drive_); set(o, SET_SELF_TIMER, selfTimer_);
        set(o, SET_FOCUS_MODE, focusMode_); set(o, SET_AF_MODE, afMode_); set(o, SET_FOCUS_AREA, focusArea_);
        set(o, SET_METERING, metering_); set(o, SET_WB, wb_); set(o, SET_STORAGE_FMT, storageFmt_); set(o, SET_AE_LOCK, aeLock_ || getenv("A5K_FAKE_AEL") ? 1 : 0);
        o->apertureX100 = kAp[apIdx_];
        o->shutterNum = kSS[ssIdx_].n; o->shutterDen = kSS[ssIdx_].d;
        // Golden-comparison fakes (host only): A5K_FAKE_APERTURE=0 hides F (fixed-lens-less body shot),
        // A5K_FAKE_SS="30/1" pins the displayed shutter.
        const char *fa = getenv("A5K_FAKE_APERTURE"); if (fa && *fa) o->apertureX100 = atoi(fa);
        const char *fs = getenv("A5K_FAKE_SS");
        if (fs && *fs) { int n = 0, d = 1; sscanf(fs, "%d/%d", &n, &d); o->shutterNum = n; o->shutterDen = d; }
        const char *fw = getenv("A5K_FAKE_WIFI");     // none / connecting / connected / ap
        if (fw) o->wifiStatus = !strcmp(fw, "connecting") ? WIFI_CONNECTING : !strcmp(fw, "connected") ? WIFI_CONNECTED
                              : !strcmp(fw, "ap") ? WIFI_AP : WIFI_NONE;
        o->batteryPct = battery_; o->shotsRemaining = shots_;
        o->movieState = movie_ ? MOVIE_REC : MOVIE_IDLE;
        o->movieSec = movie_ ? (int)((nowMs() - movieStart_) / 1000) : 0;
        o->movieRemainSec = movie_ ? 1800 - o->movieSec : 1800;
        o->zoomPermille = zoomPm_; o->focalMm = 16 + zoomPm_ * 34 / 1000; o->zoomMag10 = 10 + zoomPm_ * 30 / 1000;
        if (focusMode_ == 6 || focusMode_ == 10) { o->mfDistMm = 1200; o->mfNearestMm = 250; }
        o->valid = true;
        return true;
    }

    bool setSetting(int id, int val) {
        ExposureKind k = exposureKindOf(mode_);
        bool free = (k == EXK_PROGRAM || k == EXK_SHUTTER || k == EXK_APERTURE || k == EXK_MANUAL);
        if (id == SET_EV  && free && catalogHasValue(id, val) && !(k == EXK_MANUAL && iso_ != 0)) { ev_ = val; return true; }
        if (id == SET_ISO && free && catalogHasValue(id, val)) { iso_ = val; return true; }
        if (id == SET_EXPOSURE_MODE && catalogHasValue(id, val)) { mode_ = val; push(CEV_STATE_CHANGED, 0x2023); return true; }
        if (id == SET_DRIVE && catalogHasValue(id, val)) { drive_ = val; return true; }
        // Not mode-gated on the camera: stored so the menu/OSD read-back reflects them.
        if (!catalogHasValue(id, val)) return false;
        if (id == SET_FOCUS_MODE)  { focusMode_ = val;  return true; }
        if (id == SET_AF_MODE)     { afMode_ = val;     return true; }
        if (id == SET_FOCUS_AREA)  { focusArea_ = val;  return true; }
        if (id == SET_METERING)    { metering_ = val;   return true; }
        if (id == SET_WB)          { wb_ = val;         return true; }
        if (id == SET_STORAGE_FMT) { storageFmt_ = val; return true; }
        if (id == SET_AE_LOCK)     { aeLock_ = val;     return true; }
        return false;                                  // ignored, like the camera
    }

    // Generic apply (conversion table). Catalog-backed items keep the mode-gated setSetting path; every
    // other mapped item is accepted by the fake engine and kept in a param-id keyed store (like the
    // camera's 0x1002 snapshot) so host tests can read it back. Local/action/backup/unmapped never apply.
    int applySetting(int item, int value) {
        ApplyCmd c;
        if (!applyResolve(item, value, &c)) return APPLY_UNMAPPED;
        if (c.catalogId && c.sdcomOk) return CameraControl::applySetting(item, value);
        switch (c.method) {
            case AM_LOCAL: case AM_ACTION: return APPLY_LOCAL;
            case AM_BACKUP:   return APPLY_RECORDED;
            case AM_UNMAPPED: return APPLY_UNMAPPED;
        }
        if (c.sdcomOk && c.sdcomParam > 0 && c.sdcomParam < 2048) {
            param_[c.sdcomParam] = c.sdcomValue; have_[c.sdcomParam] = true;
            logf("fake-camera: apply %s param %d(msg 0x%x) = %d", kApply[item].key, c.sdcomParam, c.sdcomMsg, c.sdcomValue);
            return APPLY_OK;
        }
        if (c.inprocOk) {
            logf("fake-camera: apply %s inproc kind %d = %d", kApply[item].key, c.inprocKind, c.inprocValue);
            return APPLY_OK;
        }
        return APPLY_UNMAPPED;                          // mapped item, but this value has no data
    }

    bool exposureStep(int axis, int steps) {
        service();
        if (applyAxis_ >= 0) return false;
        applyAxis_ = axis; applySteps_ = steps; applyAt_ = nowMs() + 1500;
        return true;
    }

    bool autofocus(bool on) {
        afHeld_ = on;
        if (on) afAt_ = nowMs() + 400; else afAt_ = 0;
        return true;
    }
    bool shutterDown() { if (shots_ > 0) shots_--; push(CEV_CAPTURE_DONE, 0x2005); return true; }
    bool shutterUp()   { return true; }
    bool takePicture() { if (shots_ > 0) shots_--; push(CEV_CAPTURE_DONE, 0x2005); logf("fake-camera: capture, %d left", shots_); return true; }

    void zoom(int dir) { zoomPm_ += dir * 100; if (zoomPm_ < 0) zoomPm_ = 0; if (zoomPm_ > 1000) zoomPm_ = 1000; }
    bool movieRec(bool on) { movie_ = on; movieStart_ = nowMs(); return true; }

    // Host sim: the stock view / app hand-off has no real engine here, so report a simulated
    // success (SA_DONE) -- lets the host prove the menu wiring + confirm modal + toast end to end.
    int systemAction(int kind, const char *arg) {
        logf("fake-camera: systemAction kind=%d arg=%s (simulated)", kind, arg ? arg : "(launcher)");
        return SA_DONE;
    }

    int pollEvents(CameraEvent *out, int max) {
        service();
        int n = 0;
        while (n < max && evHead_ != evTail_) { out[n++] = q_[evHead_]; evHead_ = (evHead_ + 1) % 16; }
        return n;
    }

private:
    int  focusArea_ = 0, metering_ = 0, wb_ = 0, storageFmt_ = 2, aeLock_ = 0;
    int  param_[2048];
    bool have_[2048];
    static void set(CameraState *o, int id, int v) { o->has[id] = true; o->v[id] = v; }
    void push(int kind, int msg, int a = 0) {
        int nt = (evTail_ + 1) % 16; if (nt == evHead_) return;
        q_[evTail_].kind = kind; q_[evTail_].msgId = msg; q_[evTail_].a = a; q_[evTail_].b = 0; evTail_ = nt;
    }
    void service() {
        long now = nowMs();
        if (applyAxis_ >= 0 && now >= applyAt_) {
            if (applyAxis_ == AXIS_SHUTTER) {
                int n = ssIdx_ + applySteps_; ssIdx_ = n < 0 ? 0 : n >= kNSS ? kNSS - 1 : n;
                push(CEV_SHUTTER_CHANGED, 0x2007);
            } else if (applyAxis_ == AXIS_APERTURE) {
                int n = apIdx_ + applySteps_; apIdx_ = n < 0 ? 0 : n >= kNAp ? kNAp - 1 : n;
                push(CEV_STATE_CHANGED, 0x2023);
            } else if (applyAxis_ == AXIS_PROGRAM) {           // program shift moves SS and F opposite
                int s = ssIdx_ + applySteps_, a = apIdx_ - applySteps_;
                ssIdx_ = s < 0 ? 0 : s >= kNSS ? kNSS - 1 : s; apIdx_ = a < 0 ? 0 : a >= kNAp ? kNAp - 1 : a;
                push(CEV_SHUTTER_CHANGED, 0x2007);
            }
            applyAxis_ = -1;
        }
        if (afAt_ && now >= afAt_) {
            push(CEV_FOCUS_DONE, 0x200a, focusMode_ == 6 ? 0 : 1);
            afAt_ = 0;
        }
    }

    bool connected_;
    int ssIdx_, apIdx_, mode_, ev_, iso_, drive_, selfTimer_, focusMode_, afMode_, shots_, battery_;
    long applyAt_; int applyAxis_, applySteps_;
    CameraEvent q_[16]; int evHead_, evTail_;
    long afAt_; bool afHeld_;
    bool movie_; long movieStart_; int zoomPm_;
};

} // namespace

namespace platform { CameraControl *createFakeCamera() { return new FakeCamera(); } }
