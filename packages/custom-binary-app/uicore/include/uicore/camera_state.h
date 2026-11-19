// CameraState: snapshot of the camera (from the platform's "read all" = a5000 SDCom
// 0x1002 + extras) updated by events. Plain data, platform-agnostic.
#pragma once
#include "uicore/catalog.h"

namespace uicore {

// Exposure axes for CameraControl::exposureStep / CameraModel::step.
enum Axis { AXIS_SHUTTER, AXIS_APERTURE, AXIS_EV, AXIS_ISO, AXIS_PROGRAM };

// Exposure mode kinds, decoded from the SET_EXPOSURE_MODE value (a5000 A-side values,
// settings_sdcom_map.tsv api:mode/sceneMode).
enum ExposureKind { EXK_UNKNOWN, EXK_AUTO, EXK_PROGRAM, EXK_SHUTTER, EXK_APERTURE, EXK_MANUAL, EXK_SCENE };
ExposureKind exposureKindOf(int modeValue);

enum FocusState { FOCUS_IDLE = 0, FOCUS_BUSY, FOCUS_OK, FOCUS_FAIL };

// Movie recorder state (Track A MovieRecorder: IDLE / STBY / STARTING / RECORDING).
enum MovieState { MOVIE_IDLE = 0, MOVIE_STANDBY, MOVIE_STARTING, MOVIE_REC };

// Events delivered by CameraControl::pollEvents (queued by the adapter; thread-safe on
// the adapter side). Values are hints: the model re-reads the state to get the truth.
enum CameraEventKind {
    CEV_NONE = 0,
    CEV_STATE_CHANGED,     // some setting changed (re-read)
    CEV_SHUTTER_CHANGED,   // shutter speed moved (async shift applied)
    CEV_FOCUS_DONE,        // a = 1 focused, 0 failed
    CEV_CAPTURE_DONE,      // still stored
    CEV_LINK_LOST          // connection dropped
};
// OSD Wi-Fi status icon: NONE draws nothing (and takes no space).
enum WifiStatus { WIFI_NONE = 0, WIFI_CONNECTING, WIFI_CONNECTED, WIFI_AP };

struct CameraEvent { int kind; int msgId; int a; int b; };

struct CameraState {
    bool valid;                        // at least one successful read
    bool has[SET_COUNT];               // setting present in the last snapshot
    int  v[SET_COUNT];                 // A-side value (SET_SHUTTER/APERTURE: see below)
    int  apertureX100;                 // 0 unknown (e.g. 180 = F1.8)
    int  shutterNum, shutterDen;       // 0 unknown (1/640 -> 1, 640)
    int  batteryPct;                   // -1 unknown (no SDCom source known in M1)
    int  shotsRemaining;               // -1 unknown
    int  focusState;                   // FocusState
    int  movieState;                   // MovieState
    int  movieSec;                     // recorded seconds while MOVIE_REC
    int  movieRemainSec;               // card time left for a movie, -1 unknown
    int  zoomPermille;                 // 0 wide .. 1000 tele, -1 = lens has no zoom info
    int  focalMm;                      // current focal length, 0 unknown
    int  zoomMag10;                    // magnification x10 (18 = x1.8), 0 unknown
    int  mfDistMm;                     // MF focus distance: 0 unknown, -1 infinity, else mm
    int  mfNearestMm;                  // lens minimum focus distance (mm), 0 unknown
    int  wifiStatus;                   // WifiStatus
    int  nfcState;                     // NfcState (NFS_OFF when there is no NFC backend)
    long stampMs;

    CameraState() { reset(); }
    void reset() {
        valid = false;
        for (int i = 0; i < SET_COUNT; i++) { has[i] = false; v[i] = 0; }
        apertureX100 = 0; shutterNum = shutterDen = 0;
        batteryPct = -1; shotsRemaining = -1; focusState = FOCUS_IDLE; stampMs = 0;
        movieState = MOVIE_IDLE; movieSec = 0; movieRemainSec = -1;
        wifiStatus = WIFI_NONE; nfcState = 0; zoomPermille = -1; focalMm = 0; zoomMag10 = 0; mfDistMm = 0; mfNearestMm = 0;
    }
    bool get(int id, int *out) const {
        if (id <= SET_NONE || id >= SET_COUNT || !has[id]) return false;
        if (out) *out = v[id];
        return true;
    }
};

} // namespace uicore
