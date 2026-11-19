// CameraModel: the UI-side owner of CameraState. Drives a CameraControl (reads,
// event drain, relative exposure steps with async-apply tracking, shutter/AF) and gives
// screens a simple "state + commands" surface. Platform-agnostic.
#pragma once
#include "uicore/platform.h"
#include "uicore/camera_state.h"

namespace uicore {

class CameraModel {
public:
    CameraModel();
    void attach(CameraControl *cam, Clock *clock);
    void attachNfc(Nfc *n) { nfc_ = n; }       // CameraState.nfcState follows the NFC backend
    void attachWifi(Wifi *w) { wifi_ = w; }   // OSD Wi-Fi icon follows the Wi-Fi backend while it is active

    // Call every frame (cheap when idle): reconnect, drain events, poll while a shift
    // is in flight, periodic re-read.
    void tick();

    const CameraState &state() const { return st_; }
    bool online() const;                    // connected (or dryrun) and a snapshot exists
    bool refresh();                         // read now; returns success

    // Wheel/dial: dir = +1 (clockwise: faster shutter, smaller aperture, higher ISO, +EV) or -1.
    // Shutter/aperture/program steps are accumulated and sent as one shift (a shift applies
    // asynchronously, ~2 s on the a5000); EV/ISO are immediate (set + re-read).
    void step(Axis axis, int dir);
    bool adjustable(Axis axis) const;       // mode gating (Track A ShootingControls rules)
    bool busy() const { return inFlight_ >= 0; }

    bool setSetting(int id, int val);       // set + verify by re-read
    // Generic apply of a menu item (conversion table, uicore/apply.h): returns an ApplyResult.
    int  applySetting(int item, int value);

    void halfShutter(bool down);            // AF
    void fullShutter(bool down);            // capture (single: on press; burst: while held)
    bool halfHeld() const { return half_; }

    // Self-timer: the UI counts down (as Track A does), then fires one still. A second
    // full press while counting cancels. 0 = not counting.
    int  countdownSec() const;
    void toggleMovie();                     // MOVIE button: start / stop recording
    void zoom(int dir);                     // zoom lever step (+1 tele, -1 wide)
    unsigned captureCount() const { return captures_; }   // stills taken (for shutter feedback)
    long lastCaptureMs() const { return lastCaptureMs_; }

    unsigned refreshCount() const { return reads_; }
    int  lastEventKind() const { return lastEvent_; }

private:
    void drainEvents(long now);
    void sendPending(long now);
    int  signature() const;                 // value of the axis being shifted, to detect apply

    void syncWifi();
    Nfc           *nfc_;
    Wifi          *wifi_;
    bool           wifiOwned_;
    CameraControl *cam_;
    Clock         *clock_;
    CameraState    st_;
    long nextRead_, nextConnect_, pollUntil_, sentMs_, focusSetMs_;
    int  pend_[AXIS_PROGRAM + 1];
    int  inFlight_, sigBefore_;
    bool dirty_, half_, fullHeld_;
    long cdEndMs_, lastCaptureMs_;
    unsigned captures_;
    unsigned reads_;
    int  lastEvent_;
};

// Platform helper: step an EV/ISO-like catalog setting via setSetting (shared by adapters).
bool stepViaCatalog(CameraControl &cam, const CameraState &st, int settingId, int dir);

} // namespace uicore
