// M1 shooting OSD: mode / drive / focus icons, shots-remaining + battery, and the
// SS / F / EV / ISO row, laid out like Track A's ShootingOsd (853-wide logical layout
// squeezed X0.75 -> 640 physical; icons and the baked fonts are already physical).
// The control wheel changes the "active" value (yellow) exactly like Track A's
// ShootingControls: UP swaps shutter/aperture, RIGHT cycles ISO/EV, 6 s timeout.
// Also: REC / movie time, self-timer countdown, zoom + MF-distance bars, AE-lock tag,
// focus frame (white busy / green ok / orange fail), shutter blackout flash, DISP toggle.
// Consumes only abstract actions (platform keymap decides which button is which).
#include "uicore/screen.h"
#include "uicore/app.h"
#include "uicore/canvas.h"
#include "uicore/camera_model.h"
#include "uicore/gfx.h"
#include "uicore/keymap_shoot.h"
#include "uicore/log.h"
#include "../../../gen/icons.h"
#include <stdio.h>
#include <string.h>

namespace uicore {
namespace {

// Track A lays the OSD out on the 640x480 framebuffer at the emulator golden density 1.7
// (text sp -> px, dp() for margins/spacers); only its own shapes are pre-squeezed by X_SCALE.
// Measured from the golden: 24sp digits ~30px tall, 13dp battery 22 px tall.
static inline int dp(int v) { return (v * 17 + 5) / 10; }

const Color kActive   = { 0xff, 0xd4, 0x00, 255 };
const Color kDisabled = { 0x6e, 0x6e, 0x6e, 255 };
const Color kWhite    = { 255, 255, 255, 255 };
const long  kTargetTimeoutMs = 6000;
const long  kZoomShowMs = 2000;
const long  kMfShowMs = 3000;       // NEAR--FAR bar stays this long after a focus change
const long  kFlashMs = 220;          // shutter blackout fade

enum Target { T_MAIN, T_ALT, T_ISO, T_EV };

int modeIcon(int v) {
    switch (v) {
        case 0:  return ICON_S_16_DD_PARTS_OSD_ICON_MODE_IAUTO;
        case 16: return ICON_S_16_DD_PARTS_OSD_ICON_MODE_P;
        case 17: return ICON_S_16_DD_PARTS_OSD_ICON_MODE_S;
        case 18: return ICON_S_16_DD_PARTS_OSD_ICON_MODE_A;
        case 19: return ICON_S_16_DD_PARTS_OSD_ICON_MODE_M;
        case 24: return ICON_S_16_DD_PARTS_OSD_ICON_MODE_ANTIMOTIONBLUR;
        case 2:  return ICON_S_16_DD_PARTS_OSD_ICON_MODE_SCENESELECTION_PORTRAIT;
        case 3:  return ICON_S_16_DD_PARTS_OSD_ICON_MODE_SCENESELECTION_LANDSCAPE;
        case 4:  return ICON_S_16_DD_PARTS_OSD_ICON_MODE_SCENESELECTION_NIGHTVIEW;
        case 5:  return ICON_S_16_DD_PARTS_OSD_ICON_MODE_SCENESELECTION_NIGHTPORTRAIT;
        case 9:  return ICON_S_16_DD_PARTS_OSD_ICON_MODE_SCENESELECTION_SUNSET;
        case 12: return ICON_S_16_DD_PARTS_OSD_ICON_MODE_SCENESELECTION_SPORTSACTION;
        case 23: return ICON_S_16_DD_PARTS_OSD_ICON_MODE_SCENESELECTION_HANDHELDTWILIGHT;
        case 31: return ICON_S_16_DD_PARTS_OSD_ICON_MODE_SCENESELECTION_MACRO;
        default: return -1;
    }
}

int driveIcon(int drive, int selfTimer) {
    if (selfTimer == 10) return ICON_S_16_DD_PARTS_OSD_ICON_SETTING_DRIVEMODE_SELFTIMER_10;
    if (selfTimer == 2)  return ICON_S_16_DD_PARTS_OSD_ICON_SETTING_DRIVEMODE_SELFTIMER_2;
    switch (drive) {
        case 1:  return ICON_S_16_DD_PARTS_OSD_ICON_SETTING_DRIVEMODE_CONT;
        case 2:  return ICON_S_16_DD_PARTS_OSD_ICON_SETTING_DRIVEMODE_BRKC_03EV;
        case 3:  return ICON_S_16_DD_PARTS_OSD_ICON_SETTING_DRIVEMODE_SPEEDPRIORITYCONT;
        default: return ICON_S_16_DD_PARTS_OSD_ICON_SETTING_DRIVEMODE_SINGLE;
    }
}

int focusIcon(int focusMode, int afMode) {
    if (focusMode == 6)  return ICON_S_16_DD_PARTS_OSD_ICON_SETTING_AUTOFOCUSMODE_MF;
    if (focusMode == 10) return ICON_S_16_DD_PARTS_OSD_ICON_SETTING_AUTOFOCUSMODE_DMF;
    return afMode == 2 ? ICON_S_16_DD_PARTS_OSD_ICON_SETTING_AUTOFOCUSMODE_AF_C
                       : ICON_S_16_DD_PARTS_OSD_ICON_SETTING_AUTOFOCUSMODE_AF_S;
}

const char *modeText(int v) {
    switch (exposureKindOf(v)) {
        case EXK_AUTO: return "AUTO"; case EXK_PROGRAM: return "P"; case EXK_SHUTTER: return "S";
        case EXK_APERTURE: return "A"; case EXK_MANUAL: return "M";
        default: { const char *l = catalogLabel(SET_EXPOSURE_MODE, v); return l ? l : "?"; }
    }
}

// 1/640, 30", 1.3"
void fmtShutter(const CameraState &s, char *out, int n) {
    if (s.shutterNum <= 0 || s.shutterDen <= 0) { out[0] = 0; return; }
    if (s.shutterNum >= s.shutterDen) {
        int tenths = (s.shutterNum * 10 + s.shutterDen / 2) / s.shutterDen;
        if (tenths % 10 == 0) snprintf(out, n, "%d\"", tenths / 10);
        else                  snprintf(out, n, "%d.%d\"", tenths / 10, tenths % 10);
    } else if (s.shutterNum == 1) snprintf(out, n, "1/%d", s.shutterDen);
    else snprintf(out, n, "%d/%d", s.shutterNum, s.shutterDen);
}

void fmtEv(int ev, char *out, int n) {      // 1/3 EV steps
    int a = ev < 0 ? -ev : ev;
    int tenths = (a * 10 + 1) / 3;          // 1->0.3, 2->0.7, 3->1.0
    const char *sign = ev > 0 ? "+" : (ev < 0 ? "-" : "\xc2\xb1");
    snprintf(out, n, "%s%d.%d", sign, tenths / 10, tenths % 10);
}

// ---- Quick bar (CENTER/OK on the shooting OSD) ----
// Track A's horizontal quick-access bar (CameraMenu, opened by DOWN): the most-used settings in a
// bottom strip, LEFT/RIGHT to move, the wheel to change the value inline, OK/MENU to close. The slot
// set mirrors Track A's QUICK_DEFAULTS (whitebalance / focus-mode / focus-area-type ...) plus the other
// everyday dials the task calls out (drive / ISO / metering); EV is left out because the OSD already
// adjusts it on the wheel via the RIGHT target. All six are catalog-backed and apply through the model
// (CameraModel::setSetting -> SDCom), so RAM-only and brick-safe.
struct QbSlot { int id; const char *name; };
const QbSlot kQbSlots[] = {
    { SET_DRIVE,      "\xeb\x93\x9c\xeb\x9d\xbc\xec\x9d\xb4\xeb\xb8\x8c" },               // 드라이브
    { SET_ISO,        "ISO" },
    { SET_WB,         "\xed\x99\x94\xec\x9d\xb4\xed\x8a\xb8\xeb\xb0\xb8\xeb\x9f\xb0\xec\x8a\xa4" }, // 화이트밸런스
    { SET_METERING,   "\xec\xb8\xa1\xea\xb4\x91" },                                       // 측광
    { SET_FOCUS_AREA, "\xec\xb4\x88\xec\xa0\x90\xec\x98\x81\xec\x97\xad" },               // 초점영역
    { SET_FOCUS_MODE, "\xec\xb4\x88\xec\xa0\x90\xeb\xaa\xa8\xeb\x93\x9c" },               // 초점모드
};
const int kQbCount = (int)(sizeof kQbSlots / sizeof kQbSlots[0]);

// Short Korean value labels for the quick bar (0 = fall back to the catalog's own label / number).
const char *qbKoLabel(int id, int val) {
    switch (id) {
        case SET_DRIVE: switch (val) {
            case 0: return "\xeb\x8b\xa8\xec\x9d\xbc";        // 단일
            case 1: return "\xec\x97\xb0\xec\x86\x8d";        // 연속
            case 2: return "\xeb\xb8\x8c\xeb\x9e\x98\xeb\x9c\xb7"; // 브래킷
            case 3: return "\xec\x86\x8d\xeb\x8f\x84\xec\x9a\xb0\xec\x84\xa0"; } break; // 속도우선
        case SET_WB: switch (val) {
            case 0:  return "\xec\x9e\x90\xeb\x8f\x99";        // 자동
            case 1:  return "\xeb\xb0\xb1\xec\x97\xb4\xeb\x93\xb1"; // 백열등
            case 3:  return "\xec\x98\xa8\xeb\xb0\xb1\xec\x83\x89"; // 온백색
            case 4:  return "\xed\x83\x9c\xec\x96\x91\xea\xb4\x91"; // 태양광
            case 5:  return "\xed\x9d\x90\xeb\xa6\xbc";        // 흐림
            case 7:  return "\xea\xb7\xb8\xeb\x8a\x98";        // 그늘
            case 8:  return "\xec\xa3\xbc\xeb\xb0\xb1\xec\x83\x89"; // 주백색
            case 9:  return "\xeb\x83\x89\xeb\xb0\xb1\xec\x83\x89"; // 냉백색
            case 10: return "\xec\xa3\xbc\xea\xb4\x91\xec\x83\x89"; // 주광색
            case 11: return "\xed\x94\x8c\xeb\x9e\x98\xec\x8b\x9c"; // 플래시
            case 14: return "\xec\x82\xac\xec\x9a\xa9\xec\x9e\x90"; // 사용자
            case 15: return "\xec\x83\x89\xec\x98\xa8\xeb\x8f\x84"; // 색온도
            case 16: return "\xec\x88\x98\xec\xa4\x91"; } break;    // 수중
        case SET_METERING: switch (val) {
            case 0: return "\xeb\xa9\x80\xed\x8b\xb0";        // 멀티
            case 2: return "\xec\xa4\x91\xec\x95\x99\xec\xa4\x91\xec\xa0\x90"; // 중앙중점
            case 3: return "\xec\x8a\xa4\xed\x8c\x9f"; } break;     // 스팟
        case SET_FOCUS_AREA: switch (val) {
            case 0:  return "\xeb\xa9\x80\xed\x8b\xb0";       // 멀티
            case 1:  return "\xec\xa4\x91\xec\x95\x99";       // 중앙
            case 3:  return "\xed\x94\x8c\xeb\xa0\x89\xec\x8b\x9c\xeb\xb8\x94"; // 플렉시블
            case 5:  return "\xec\x8a\xa4\xed\x8c\x9f";       // 스팟
            case 6:  return "\xeb\xa1\x9c\xec\xbb\xac";       // 로컬
            case 11: return "\xec\x99\x80\xec\x9d\xb4\xeb\x93\x9c"; } break; // 와이드
        case SET_FOCUS_MODE: switch (val) {
            case 0:  return "AF";
            case 6:  return "MF";
            case 10: return "DMF"; } break;
    }
    return 0;
}

// Wrap-around neighbour over the catalog's value table (ENUM/LIST); returns the new value.
int qbCycle(const SettingDef *d, int cur, int dir) {
    if (!d || d->nVals <= 0) return cur;
    int idx = 0;
    for (int i = 0; i < d->nVals; i++) if (d->vals[i].value == cur) { idx = i; break; }
    idx = (idx + dir + d->nVals) % d->nVals;
    return d->vals[idx].value;
}

void qbValueLabel(int id, int val, char *out, int n) {
    const char *k = qbKoLabel(id, val);
    if (k) { snprintf(out, n, "%s", k); return; }
    const char *l = catalogLabel(id, val);
    if (l) { snprintf(out, n, "%s", l); return; }
    snprintf(out, n, "%d", val);
}

// ---- Mode / drive pickers (CENTER = exposure mode, LEFT = drive mode) ----
// Track A's onCameraKeyDown: ENTER -> openModePicker (exposure/scene mode), LEFT -> openDrivePicker.
// Two modals matched to Track A: the mode picker is OptionPicker.openGridScreen -- a full-screen black
// grid of GRID_COLS icon+name cells (4-way + dial), and the drive picker is DrivePicker -- a centred
// dark card with two columns (drive mode + self-timer). Both apply through CameraModel::setSetting.
enum ModalKind { MOD_NONE = 0, MOD_MODE, MOD_DRIVE };
enum { GRID_COLS = 3, GRID_ROWS = 5 };               // OptionPicker grid geometry

// Display order (iAuto, P, A, S, M, then the advanced/scene modes) over SET_EXPOSURE_MODE values.
const int kModeOrder[]  = { 0, 16, 18, 17, 19, 24, 23, 2, 3, 12, 31, 5, 4, 9 };
const int kModeOrderN   = (int)(sizeof kModeOrder / sizeof kModeOrder[0]);
// Track A drive order: single, burst, speed-priority burst, bracket.
const int kDriveOrder[] = { 0, 1, 3, 2 };
const int kDriveOrderN  = (int)(sizeof kDriveOrder / sizeof kDriveOrder[0]);
// Self-timer values the a5000 HAL exposes (SET_SELF_TIMER): off / 2s / 10s.
const int kTimerOrder[] = { 0, 2, 10 };
const int kTimerOrderN  = (int)(sizeof kTimerOrder / sizeof kTimerOrder[0]);
const char *timerLabelKo(int v) { return v == 0 ? "끔" : v == 2 ? "2초" : v == 10 ? "10초" : "?"; }

// UTF-8 aware truncation to fit maxPx physical pixels (keeps whole characters).
void fitPx(const Gfx &g, int font, const char *s, int maxPx, char *out, int outN) {
    int n = (int)strlen(s);
    if (n >= outN) n = outN - 1;
    memcpy(out, s, n); out[n] = 0;
    while (n > 0 && g.textWidth(font, out) > maxPx) {
        n--;
        while (n > 0 && (out[n] & 0xC0) == 0x80) n--;   // back to a character boundary
        out[n] = 0;
    }
}

// P/A/S/M are shown as a compact text glyph in the grid icon slot (balanced with the scene pictograms),
// so their labels drop the leading letter to avoid showing it twice. Scene modes keep their full name.
const char *modeLetter(int v) {
    switch (v) { case 16: return "P"; case 17: return "S"; case 18: return "A"; case 19: return "M"; }
    return 0;
}
const char *modeLabelKo(int v) {
    switch (v) {
        case 0:  return "자동";
        case 16: return "프로그램";
        case 18: return "조리개 우선";
        case 17: return "셔터 우선";
        case 19: return "수동 노출";
        case 24: return "안티모션블러";
        case 23: return "핸드헬드야경";
        case 2:  return "인물";
        case 3:  return "풍경";
        case 12: return "스포츠";
        case 31: return "매크로";
        case 5:  return "야경인물";
        case 4:  return "야경";
        case 9:  return "일몰";
    }
    return 0;
}
const char *driveLabelKo(int v) {
    switch (v) {
        case 0: return "단일 촬영";
        case 1: return "연속 촬영";
        case 3: return "속도우선 연속";
        case 2: return "연속 브래킷";
    }
    return 0;
}

class ShootingScreen : public Screen {
public:
    ShootingScreen() : target_(T_MAIN), lastUse_(0), zoomUntil_(0), mfUntil_(0), lastMf_(1), infoOn_(true),
                       qbOpen_(false), qbSel_(0), modal_(MOD_NONE), modalSel_(0),
                       dSel_(0), tSel_(0), dCol_(0) {}
    const char *title() const { return "SHOOTING"; }
    Context context() const { return CTX_SHOOTING; }

    Axis mainAxis(const CameraState &s) const {
        int mv = 0; s.get(SET_EXPOSURE_MODE, &mv);
        ExposureKind k = exposureKindOf(mv);
        if (k == EXK_APERTURE) return AXIS_APERTURE;
        if (k == EXK_SHUTTER || k == EXK_MANUAL) return AXIS_SHUTTER;
        return k == EXK_PROGRAM ? AXIS_PROGRAM : AXIS_SHUTTER;
    }

    // Which value the wheel changes now (-1 = none highlighted but program shift possible).
    int activeAxis(const CameraState &s) const {
        switch (target_) {
            case T_ISO: return AXIS_ISO;
            case T_EV:  return AXIS_EV;
            case T_ALT: return AXIS_APERTURE;
            default: return mainAxis(s);
        }
    }

    void onAction(Action a, int value, App &app) {
        CameraModel &m = app.model();
        long now = app.nowMs();
        if (target_ != T_MAIN && now - lastUse_ > kTargetTimeoutMs) target_ = T_MAIN;
        if (a == ACT_SHUTTER_HALF)  { m.halfShutter(value != 0); return; }
        if (a == ACT_SHUTTER_FULL)  { if (value != 2) m.fullShutter(value != 0); return; }
        if (value == 0) return;                      // act on key down / repeat only
        if (modal_ != MOD_NONE) { modalAction(a, m); return; }  // mode / drive picker grabs keys
        if (qbOpen_) { quickBarAction(a, m); return; }   // quick bar grabs all discrete keys while open
        switch (a) {
            case ACT_MOVIE_REC: m.toggleMovie(); break;
            case ACT_ZOOM_T: case ACT_ZOOM_W:
                m.zoom(a == ACT_ZOOM_T ? +1 : -1); zoomUntil_ = now + kZoomShowMs; break;
            case ACT_CUSTOM_1: dispatchKey(SK_C1, now, app, m); break;   // customizable (default: DISP)
            case ACT_DIAL_MAIN_CW:  case ACT_DIAL_MAIN_CCW: {
                lastUse_ = now;
                m.step((Axis)activeAxis(m.state()), a == ACT_DIAL_MAIN_CW ? +1 : -1);
                break;
            }
            case ACT_DIAL_SUB_CW: case ACT_DIAL_SUB_CCW: {   // 2nd dial (a6400): aperture / EV
                lastUse_ = now;
                int mv = 0; m.state().get(SET_EXPOSURE_MODE, &mv);
                Axis ax = exposureKindOf(mv) == EXK_MANUAL ? AXIS_APERTURE : AXIS_EV;
                m.step(ax, a == ACT_DIAL_SUB_CW ? +1 : -1);
                break;
            }
            // 4-way + CENTER route THROUGH the user-assignable Custom Key table (keymap_shoot):
            // key -> ShootFunc -> action. Defaults reproduce the scheme (UP = quick bar,
            // DOWN = exposure target, LEFT = drive picker, RIGHT = ISO/EV, CENTER = mode picker).
            case ACT_NAV_UP:    dispatchKey(SK_UP, now, app, m); break;
            case ACT_NAV_DOWN:  dispatchKey(SK_DOWN, now, app, m); break;
            case ACT_NAV_LEFT:  dispatchKey(SK_LEFT, now, app, m); break;
            case ACT_NAV_RIGHT: dispatchKey(SK_RIGHT, now, app, m); break;
            case ACT_CONFIRM:   dispatchKey(SK_CENTER, now, app, m); break;
            // Dedicated hardware buttons stay fixed (so MENU / PLAY are always reachable).
            case ACT_PLAYBACK:  app.pushScreen(createPlaybackScreen()); break;   // M3 gallery
            case ACT_MENU:      app.pushScreen(createMenuScreen()); break;        // M2 cascade main menu
            default: break;
        }
    }

    // Run one assignable function (shared by every customizable key). Keeps the quick-bar /
    // picker / target logic exactly as before; only the key->function routing is table-driven.
    void dispatchFunc(ShootFunc fn, long now, App &app, CameraModel &m) {
        switch (fn) {
            case SF_QUICK_BAR:    qbOpen_ = true; qbSel_ = 0; lastUse_ = now; break;
            case SF_EXPO_TARGET:  lastUse_ = now; target_ = (target_ == T_MAIN) ? T_ALT : T_MAIN; break;
            case SF_ISO_EV:       lastUse_ = now; target_ = (target_ == T_ISO)  ? T_EV  : T_ISO;  break;
            case SF_ISO:          lastUse_ = now; target_ = T_ISO;  break;
            case SF_SHUTTER:      lastUse_ = now; target_ = T_MAIN; break;
            case SF_MODE_PICKER:  openModal(MOD_MODE, m);  break;
            case SF_DRIVE_PICKER: openModal(MOD_DRIVE, m); break;
            case SF_PLAYBACK:     app.pushScreen(createPlaybackScreen()); break;
            case SF_MENU:         app.pushScreen(createMenuScreen());     break;
            case SF_DISP:         infoOn_ = !infoOn_; break;             // hide / show the info rows
            case SF_AE_LOCK: {                                           // toggle AE lock
                int cur = 0; m.state().get(SET_AE_LOCK, &cur);
                m.setSetting(SET_AE_LOCK, cur ? 0 : 1); break;
            }
            case SF_NONE: default: break;
        }
    }
    void dispatchKey(ShootKey k, long now, App &app, CameraModel &m) {
        dispatchFunc(shootKeymap().func(k), now, app, m);
    }

    // ---- mode / drive picker modals ----
    static int indexOf(const int *ord, int n, int v) { for (int i = 0; i < n; i++) if (ord[i] == v) return i; return 0; }
    bool bracketSel() const { return kDriveOrder[dSel_] == 2; }    // bracket forbids self-timer (Track A)

    void openModal(int kind, CameraModel &m) {
        modal_ = kind;
        const CameraState &st = m.state();
        if (kind == MOD_MODE) {
            int cur = 0; st.get(SET_EXPOSURE_MODE, &cur);
            modalSel_ = indexOf(kModeOrder, kModeOrderN, cur);
        } else {
            int dv = 0, tv = 0; st.get(SET_DRIVE, &dv); st.get(SET_SELF_TIMER, &tv);
            dSel_ = indexOf(kDriveOrder, kDriveOrderN, dv);
            tSel_ = indexOf(kTimerOrder, kTimerOrderN, tv);
            dCol_ = 0;
            if (bracketSel()) tSel_ = 0;                          // off
        }
    }

    void modalAction(Action a, CameraModel &m) {
        if (modal_ == MOD_MODE) { modeGridAction(a, m); return; }
        driveAction(a, m);
    }

    // OptionPicker grid: dial/LEFT/RIGHT step sequentially, UP/DOWN move one grid row (same column).
    void modeGridAction(Action a, CameraModel &m) {
        int n = kModeOrderN;
        switch (a) {
            case ACT_NAV_LEFT: case ACT_DIAL_MAIN_CCW: case ACT_DIAL_SUB_CCW:
                if (modalSel_ > 0) modalSel_--; break;
            case ACT_NAV_RIGHT: case ACT_DIAL_MAIN_CW: case ACT_DIAL_SUB_CW:
                if (modalSel_ < n - 1) modalSel_++; break;
            case ACT_NAV_UP:   if (modalSel_ - GRID_COLS >= 0) modalSel_ -= GRID_COLS; break;
            case ACT_NAV_DOWN: {
                int t = modalSel_ + GRID_COLS;
                if (t < n) modalSel_ = t;
                else if (modalSel_ / GRID_COLS < (n - 1) / GRID_COLS) modalSel_ = n - 1;  // clamp onto short last row
                break;
            }
            case ACT_CONFIRM: m.setSetting(SET_EXPOSURE_MODE, kModeOrder[modalSel_]); modal_ = MOD_NONE; break;
            case ACT_MENU: case ACT_BACK: modal_ = MOD_NONE; break;
            default: break;
        }
    }

    // DrivePicker: LEFT/RIGHT switch column (right blocked while bracket), UP/DOWN/dial move in the active
    // column (wrap), OK applies drive + self-timer, MENU cancels.
    void driveAction(Action a, CameraModel &m) {
        switch (a) {
            case ACT_NAV_LEFT:  dCol_ = 0; break;
            case ACT_NAV_RIGHT: if (!bracketSel()) dCol_ = 1; break;
            case ACT_NAV_UP: case ACT_DIAL_MAIN_CCW: case ACT_DIAL_SUB_CCW: driveMove(-1); break;
            case ACT_NAV_DOWN: case ACT_DIAL_MAIN_CW: case ACT_DIAL_SUB_CW:  driveMove(+1); break;
            case ACT_CONFIRM: {
                m.setSetting(SET_DRIVE, kDriveOrder[dSel_]);
                m.setSetting(SET_SELF_TIMER, bracketSel() ? 0 : kTimerOrder[tSel_]);
                modal_ = MOD_NONE; break;
            }
            case ACT_MENU: case ACT_BACK: modal_ = MOD_NONE; break;
            default: break;
        }
    }
    void driveMove(int d) {
        if (dCol_ == 0) {
            dSel_ = (dSel_ + d + kDriveOrderN) % kDriveOrderN;
            if (bracketSel()) { dCol_ = 0; tSel_ = 0; }           // bracket -> timer off, stay in drive column
        } else {
            if (bracketSel()) { dCol_ = 0; return; }
            tSel_ = (tSel_ + d + kTimerOrderN) % kTimerOrderN;
        }
    }

    // Quick bar input while open: LEFT/RIGHT move between slots (LEFT at the first slot closes),
    // the wheel (rotation or UP/DOWN) changes the selected value inline, OK/MENU/Fn close.
    void quickBarAction(Action a, CameraModel &m) {
        switch (a) {
            case ACT_NAV_RIGHT: qbSel_ = (qbSel_ + 1) % kQbCount; break;
            case ACT_NAV_LEFT:  if (qbSel_ > 0) qbSel_--; else qbOpen_ = false; break;
            case ACT_DIAL_MAIN_CW: case ACT_DIAL_SUB_CW: case ACT_NAV_UP:   qbAdjust(m, +1); break;
            case ACT_DIAL_MAIN_CCW: case ACT_DIAL_SUB_CCW: case ACT_NAV_DOWN: qbAdjust(m, -1); break;
            case ACT_CONFIRM: case ACT_MENU: case ACT_FN: qbOpen_ = false; break;
            default: break;
        }
    }

    void qbAdjust(CameraModel &m, int dir) {
        const QbSlot &sl = kQbSlots[qbSel_];
        const SettingDef *d = catalogDef(sl.id);
        if (!d) return;
        int cur = 0; bool has = m.state().get(sl.id, &cur);
        int nxt = qbCycle(d, cur, dir);
        if (nxt != cur || !has) m.setSetting(sl.id, nxt);   // set + re-read; SDCom = RAM-only
    }

    void draw(Canvas &c, App &app) {
        const Gfx &g = app.gfx();
        const CameraState &s = app.model().state();
        long now = app.nowMs();
        if (target_ != T_MAIN && now - lastUse_ > kTargetTimeoutMs) target_ = T_MAIN;
        c.clear(rgba(0, 0, 0, 0));                  // live view shows through

        if (!app.model().online()) {                // not connected / no snapshot yet
            static long lastRcLog = -100000;        // diag (issue 3): confirm if this branch is live
            if (now - lastRcLog > 2000) { logf("osd: READING CAMERA branch (online=0 conn=%d)", app.cameraConnected() ? 1 : 0); lastRcLog = now; }
            g.text(c, GF_OSD_16, 20, 40, app.cameraConnected() ? "READING CAMERA..." : "CAMERA NOT CONNECTED", rgb(255, 160, 80));
            return;
        }
        drawFlash(c, app.model(), now);
        if (infoOn_) {
            drawTop(c, g, s, now);
            drawBottom(c, g, s, app);
            drawBars(c, g, s, now);
        }
        drawFocusFrame(c, s);
        int cd = app.model().countdownSec();
        if (cd > 0) drawCountdown(c, g, cd);
        if (qbOpen_) drawQuickBar(c, g, s);         // overlay: live view + OSD stay underneath
        if (modal_ != MOD_NONE) drawModal(c, g, s); // mode / drive picker card
    }

private:
    // Track A quick bar: a bottom strip of setting cells (label over value), the selected cell in the
    // Sony accent (#D34100). Draw cost is deliberately cheap -- one opaque-ish strip blit over a bounded
    // bottom band plus a few small cell rects and short text runs; no supersampling or full-screen alpha.
    void drawQuickBar(Canvas &c, const Gfx &g, const CameraState &s) {
        const Color kPanel   = { 0x10, 0x10, 0x10, 0xf0 };   // near-opaque: covers the OSD rows under it in one blit
        const Color kAccentBg= { 0xd3, 0x41, 0x00, 255 };
        const Color kCellBg  = { 255, 255, 255, 0x2e };
        const Color kLabel   = { 0xb8, 0xb8, 0xb8, 255 };
        const Color kSelLabel= { 0x2a, 0x0d, 0x00, 255 };
        const Color kBlack   = { 0, 0, 0, 255 };
        const Color kHint    = { 0x9a, 0x9c, 0xa0, 255 };
        const int lf = GF_UI_19, vf = GF_UI_26, hf = GF_UI_20;
        const int padX = dp(10), gap = dp(4), minCW = dp(72);
        const int cellH = 58, barH = 96, barTop = PANEL_H - barH, cellTop = barTop + 8;

        char val[8][24];
        int  cw[8], cellX[8];
        int  x = 0;
        for (int i = 0; i < kQbCount; i++) {
            int cur = 0; s.get(kQbSlots[i].id, &cur);
            qbValueLabel(kQbSlots[i].id, cur, val[i], sizeof val[i]);
            int lw = g.textWidth(lf, kQbSlots[i].name), vw = g.textWidth(vf, val[i]);
            int w = (lw > vw ? lw : vw) + 2 * padX;
            if (w < minCW) w = minCW;
            cw[i] = w; cellX[i] = x; x += w + gap;
        }
        int contentW = x - gap;                      // drop the trailing gap
        const int margin = dp(6);
        int originX;
        if (contentW <= PANEL_W - 2 * margin) originX = (PANEL_W - contentW) / 2;
        else {                                       // too wide: centre the selected cell, clamped
            int selC = cellX[qbSel_] + cw[qbSel_] / 2;
            originX = PANEL_W / 2 - selC;
            int maxO = margin, minO = PANEL_W - margin - contentW;
            if (originX > maxO) originX = maxO;
            if (originX < minO) originX = minO;
        }

        c.fillRect(0, barTop, PANEL_W, barH, kPanel);
        c.fillRect(0, barTop, PANEL_W, 2, kAccentBg);     // thin accent edge at the top of the strip
        for (int i = 0; i < kQbCount; i++) {
            int cx = originX + cellX[i];
            if (cx + cw[i] < 0 || cx > PANEL_W) continue; // off-screen cell: skip
            bool sel = i == qbSel_;
            c.fillRect(cx, cellTop, cw[i], cellH, sel ? kAccentBg : kCellBg);
            const char *nm = kQbSlots[i].name;
            int lw = g.textWidth(lf, nm), vw = g.textWidth(vf, val[i]);
            g.text(c, lf, cx + (cw[i] - lw) / 2, cellTop + 22, nm,     sel ? kSelLabel : kLabel);
            g.text(c, vf, cx + (cw[i] - vw) / 2, cellTop + cellH - 12, val[i], sel ? kBlack : kWhite);
        }
        // hint: "◀▶ 항목   WHEEL 값   ●/MENU 닫기"
        const char *hint = "\xe2\x97\x80\xe2\x96\xb6 \xed\x95\xad\xeb\xaa\xa9    WHEEL \xea\xb0\x92    \xe2\x97\x8f/MENU \xeb\x8b\xab\xea\xb8\xb0";
        int hw = g.textWidth(hf, hint);
        g.text(c, hf, (PANEL_W - hw) / 2, PANEL_H - 8, hint, kHint);
    }

    void drawModal(Canvas &c, const Gfx &g, const CameraState &s) {
        if (modal_ == MOD_MODE) drawModeGrid(c, g, s);
        else                    drawDrivePicker(c, g, s);
    }

    // OptionPicker.openGridScreen: a full-screen black grid of GRID_COLS icon+name cells. The whole
    // screen is an opaque black fill (the app pauses the live view behind it) -- cheap: one memset-like
    // clear plus the small per-cell rects/icons/text, no alpha blend or supersampling.
    void drawModeGrid(Canvas &c, const Gfx &g, const CameraState &s) {
        const Color kBlack  = { 0, 0, 0, 255 };
        const Color kRule   = { 0x9a, 0x9a, 0x9a, 255 };
        const Color kSel    = { 0xe8, 0x60, 0x0c, 255 };          // OptionPicker ACCENT
        const Color kCur    = { 0x60, 0x3a, 0x12, 255 };          // 0x60e8600c flattened onto black
        const Color kCell   = { 0x2b, 0x2b, 0x2b, 255 };          // 0x30ffffff flattened onto black
        const Color kName    = { 0xe0, 0xe0, 0xe0, 255 };
        const Color kCurName = { 0xff, 0x9a, 0x2e, 255 };
        const Color kHint   = { 0xd0, 0xd0, 0xd0, 255 };
        const int HEADER_H = 58, FOOTER_H = 44, SIDE = 20, CELL_H = 68;

        c.fillRect(0, 0, PANEL_W, PANEL_H, kBlack);
        g.text(c, GF_UI_27, 28, 38, "촬영 모드", kWhite);
        c.fillRect(0, HEADER_H, PANEL_W, 2, kRule);

        int cur = 0; s.get(SET_EXPOSURE_MODE, &cur);
        int n = kModeOrderN;
        int rows = (n + GRID_COLS - 1) / GRID_COLS;
        int selRow = modalSel_ / GRID_COLS;
        int firstRow = selRow - GRID_ROWS / 2;
        if (firstRow > rows - GRID_ROWS) firstRow = rows - GRID_ROWS;
        if (firstRow < 0) firstRow = 0;
        int colW = (PANEL_W - 2 * SIDE) / GRID_COLS;
        int gridTop = HEADER_H + 4 + 14;                          // header + gap + more-up band
        const int hf = GF_UI_20;
        if (firstRow > 0) { const char *u = "\xe2\x96\xb2"; int w = g.textWidth(hf, u); g.text(c, hf, PANEL_W / 2 - w / 2, HEADER_H + 18, u, kHint); }

        for (int r = 0; r < GRID_ROWS; r++) {
            for (int col = 0; col < GRID_COLS; col++) {
                int idx = (firstRow + r) * GRID_COLS + col;
                if (idx >= n) continue;
                int v = kModeOrder[idx];
                bool sel = idx == modalSel_, isCur = v == cur;
                int cx = SIDE + col * colW + 3, cy = gridTop + r * CELL_H + 3;
                int cwd = colW - 6, chd = CELL_H - 6;
                c.fillRect(cx, cy, cwd, chd, sel ? kSel : (isCur ? kCur : kCell));
                Color nameCol = sel ? kWhite : (isCur ? kCurName : kName);
                const int SLOT = 40;                               // fixed icon slot
                // P/A/S/M + scene modes all have Sony OSD pictograms (modeIcon) -- use the
                // icon, centred in the slot. Only fall back to a letter glyph if the icon
                // atlas lacks it (keeps the row aligned either way).
                int ic = modeIcon(v), iw = 0, ih = 0;
                if (g.hasIcons() && ic >= 0 && g.iconSize(ic, &iw, &ih)) {
                    int sw = iw * 7 / 8, sh = ih * 7 / 8;        // ~87%: pictograms a touch smaller in-cell
                    g.iconScaled(c, ic, cx + 6 + (SLOT - sw) / 2, cy + (chd - sh) / 2, 7, 8);
                } else {
                    const char *letter = modeLetter(v);
                    if (letter) {
                        int lw = g.textWidth(GF_UI_27, letter);
                        g.text(c, GF_UI_27, cx + 6 + (SLOT - lw) / 2, cy + chd / 2 + 10, letter, nameCol, false);
                    }
                }
                int tx = cx + 6 + SLOT + 8;
                char lb[48]; fitPx(g, GF_UI_26, modeLabelKo(v), cx + cwd - tx - 4, lb, sizeof lb);
                g.text(c, GF_UI_26, tx, cy + chd / 2 + 9, lb, nameCol, false);
            }
        }
        int lastRow = firstRow + GRID_ROWS;
        if (lastRow < rows) { const char *d = "\xe2\x96\xbc"; int w = g.textWidth(hf, d); g.text(c, hf, PANEL_W / 2 - w / 2, PANEL_H - FOOTER_H - 4, d, kHint); }
        c.fillRect(0, PANEL_H - FOOTER_H, PANEL_W, 1, kRule);
        // guide: "▲▼◀▶ 선택   ● 확인   MENU 취소"
        const char *gd = "\xe2\x96\xb2\xe2\x96\xbc\xe2\x97\x80\xe2\x96\xb6 \xec\x84\xa0\xed\x83\x9d   \xe2\x97\x8f \xed\x99\x95\xec\x9d\xb8   MENU \xec\xb7\xa8\xec\x86\x8c";
        int gw = g.textWidth(GF_UI_24, gd);
        g.text(c, GF_UI_24, PANEL_W - 30 - gw, PANEL_H - 16, gd, kHint, false);
    }

    // DrivePicker: centred dark card with two columns -- drive mode (with icons) + self-timer. The active
    // column's selection is Sony accent, the inactive column's is a dim accent; bracket greys the timer
    // column. Opaque card only (no full-screen scrim) to stay cheap on the a5000 CPU.
    void drawDrivePicker(Canvas &c, const Gfx &g, const CameraState &s) {
        const Color kCard  = { 0x20, 0x22, 0x26, 255 };
        const Color kLine  = { 0x44, 0x47, 0x4c, 255 };
        const Color kSel   = { 0xd3, 0x41, 0x00, 255 };
        const Color kSelDim= { 0x5a, 0x33, 0x20, 255 };
        const Color kDim   = { 0xb9, 0xbc, 0xc0, 255 };
        const Color kHead  = { 0x8a, 0x8d, 0x92, 255 };
        const Color kGrey  = { 0x5a, 0x5d, 0x62, 255 };
        const Color kHint  = { 0xb9, 0xbc, 0xc0, 255 };
        (void)s;
        // Bigger, roomier card matched to the app's DrivePicker proportions (dp() at density 1).
        const int pad = 20, lcolW = 210, rcolW = 92, gapW = 16;   // outer padding + two columns
        const int cardW = pad + lcolW + 1 + gapW + rcolW + pad;   // ~359
        const int titleH = 40, headH = 30, rowH = 42, rowInset = 6, hintH = 34;
        const int bodyRows = kDriveOrderN < kTimerOrderN ? kTimerOrderN : kDriveOrderN;
        const int cardH = pad + titleH + headH + bodyRows * rowH + hintH + pad;
        int x0 = (PANEL_W - cardW) / 2, y0 = (PANEL_H - cardH) / 2;
        bool bracket = bracketSel();

        c.fillRect(x0, y0, cardW, cardH, kCard);
        c.rectOutline(x0, y0, cardW, cardH, kLine);
        g.text(c, GF_UI_27, x0 + pad, y0 + pad + 24, "드라이브 모드", kWhite);
        int colsY = y0 + pad + titleH;
        int lx = x0 + pad, rx = x0 + pad + lcolW + 1 + gapW;
        // column heads
        g.text(c, GF_UI_20, lx + rowInset, colsY + 18, "드라이브", dCol_ == 0 ? kWhite : kHead, false);
        g.text(c, GF_UI_20, rx + rowInset, colsY + 18, bracket ? "셀프타이머 (사용 불가)" : "셀프타이머",
               bracket ? kGrey : (dCol_ == 1 ? kWhite : kHead), false);
        // vertical rule between columns
        c.fillRect(x0 + pad + lcolW + gapW / 2, colsY, 1, headH + bodyRows * rowH, kLine);
        int rowsY = colsY + headH;

        // left column: drive modes with icons
        for (int i = 0; i < kDriveOrderN; i++) {
            int v = kDriveOrder[i];
            bool sel = i == dSel_;
            int y = rowsY + i * rowH;
            if (sel) c.fillRect(lx, y + 3, lcolW, rowH - 6, dCol_ == 0 ? kSel : kSelDim);
            int ic = driveIcon(v, 0), iw = 0, ih = 0;
            if (g.hasIcons() && g.iconSize(ic, &iw, &ih)) g.icon(c, ic, lx + rowInset + 2, y + (rowH - ih) / 2);
            else iw = 0;
            int tx = lx + rowInset + (iw ? iw + 12 : 4);
            char lb[48]; fitPx(g, GF_UI_26, driveLabelKo(v), lx + lcolW - tx - 6, lb, sizeof lb);
            g.text(c, GF_UI_26, tx, y + rowH / 2 + 9, lb, sel ? kWhite : kDim, false);
        }
        // right column: self-timer
        for (int i = 0; i < kTimerOrderN; i++) {
            int v = kTimerOrder[i];
            bool sel = i == tSel_ && !bracket;
            int y = rowsY + i * rowH;
            if (sel) c.fillRect(rx, y + 3, rcolW, rowH - 6, dCol_ == 1 ? kSel : kSelDim);
            g.text(c, GF_UI_26, rx + rowInset + 4, y + rowH / 2 + 9, timerLabelKo(v), bracket ? kGrey : (sel ? kWhite : kDim), false);
        }
        // hint: "◀▶ 열   ▲▼ 선택   ● 확인   MENU 취소"
        const char *hint = "\xe2\x97\x80\xe2\x96\xb6 \xec\x97\xb4   \xe2\x96\xb2\xe2\x96\xbc \xec\x84\xa0\xed\x83\x9d   \xe2\x97\x8f \xed\x99\x95\xec\x9d\xb8   MENU \xec\xb7\xa8\xec\x86\x8c";
        int hw = g.textWidth(GF_UI_20, hint);
        g.text(c, GF_UI_20, x0 + cardW - pad - hw, y0 + cardH - 14, hint, kHint, false);
    }

    void drawTop(Canvas &c, const Gfx &g, const CameraState &s, long now) {
        int mv = 0, dr = 0, st = 0, fm = 0, af = 1;
        s.get(SET_EXPOSURE_MODE, &mv); s.get(SET_DRIVE, &dr); s.get(SET_SELF_TIMER, &st);
        s.get(SET_FOCUS_MODE, &fm); s.get(SET_AF_MODE, &af);
        int mi = modeIcon(mv);
        int x = 17;
        if (g.hasIcons() && mi >= 0) {
            g.icon(c, mi, x, 10);
        } else {                                    // boxed text fallback (Track A: 16sp bold, 2dp white box)
            const char *t = modeText(mv);
            int w = g.textWidth(GF_OSD_26, t) + dp(12);
            c.rectOutline(x, 14, w, 36, kWhite);
            c.rectOutline(x + 1, 15, w - 2, 34, kWhite);
            g.text(c, GF_OSD_26, x + dp(6), 42, t, kWhite);
        }
        int di = driveIcon(dr, st), fi = focusIcon(fm, af);
        if (g.hasIcons()) {
            g.icon(c, di, 87, 15);
            g.icon(c, fi, 148, 15);
        } else {
            const char *ft = fm == 6 ? "MF" : fm == 10 ? "DMF" : (af == 2 ? "AF-C" : "AF-S");
            g.text(c, GF_OSD_26, 90, 40, ft, kWhite);
        }

        // Right side, right-aligned at 640-dp(10): [REC] [shots] [battery 26dp*0.75 x 13dp] [13sp pct]
        const int rowCy = 41;                       // vertical centre of the top row's content
        int rx = PANEL_W - dp(10);
        char buf[24];
        if (s.batteryPct >= 0) {
            snprintf(buf, sizeof buf, "%d%%", s.batteryPct);
            int w = g.textWidth(GF_OSD_21, buf);
            g.text(c, GF_OSD_21, rx - w, rowCy + 7, buf, kWhite);
            rx -= w + 8;
            int bw = 31, bh = dp(13);            // (int)(dp(26)*0.75) at the battery's measured scale
            drawBattery(c, rx - bw, rowCy - bh / 2, bw, bh, s.batteryPct);
            rx -= bw + dp(12);
        }
        if (s.wifiStatus != WIFI_NONE) {              // group order: [REC] shots  wifi  battery pct
            int ww = wifiWidth(g, s.wifiStatus);
            drawWifi(c, g, s.wifiStatus, now, rx - ww, rowCy);
            rx -= ww + dp(12);
        }
        bool movie = s.movieState != MOVIE_IDLE;
        if (movie ? s.movieRemainSec >= 0 : s.shotsRemaining >= 0) {   // shots left, or card time during a movie
            if (movie) fmtTime(s.movieRemainSec, buf, sizeof buf); else snprintf(buf, sizeof buf, "%d", s.shotsRemaining);
            int w = g.textWidth(GF_OSD_24, buf);
            g.text(c, GF_OSD_24, rx - w, rowCy + 7, buf, kWhite);
            rx -= w + dp(12);
        }
        if (movie) {
            if (s.movieState == MOVIE_REC) { char t[16]; fmtTime(s.movieSec, t, sizeof t); snprintf(buf, sizeof buf, "REC %s", t); }
            else snprintf(buf, sizeof buf, s.movieState == MOVIE_STARTING ? "REC ..." : "STBY ...");
            int w = g.textWidth(GF_OSD_24, buf);
            g.text(c, GF_OSD_24, rx - w, rowCy + 7, buf, rgb(255, 48, 48));
        }
        int ael = 0;
        if (s.get(SET_AE_LOCK, &ael) && ael) {         // AE lock tag under the mode icon
            c.rectOutline(17, 70, 52, 28, kWhite);
            g.text(c, GF_OSD_21, 23, 91, "AEL", kWhite);
        }
    }

    // Wi-Fi status icon (own design): dot + 3 concentric arcs (+-48 deg), white with the OSD shadow.
    // Geometry is in "square" units (x is pre-squeezed 3/4 for the stretched LCD) and rasterised with
    // 4x4 coverage sampling, so the arcs/dot are anti-aliased after the x1.333 stretch too.
    enum { WIFI_W = 26, WIFI_H = 26 };
    // coverage 0..16 of pixel (x,y) in the WIFI_W x WIFI_H box; arcMask bit i = arc i, dot always;
    // grow widens every band/dot (for the shadow).
    static int wifiCov(int x, int y, int arcMask, float grow) {
        static const float rIn[3] = { 7.0f, 12.5f, 18.0f }, T = 3.0f, DOT = 3.2f;
        const float ox = 13.0f, oy = 22.5f;
        int hit = 0;
        for (int j = 0; j < 4; j++) for (int i = 0; i < 4; i++) {
            float px = (x + (i + 0.5f) / 4 - ox) * (4.0f / 3.0f), py = oy - (y + (j + 0.5f) / 4);
            float d2 = px * px + py * py;
            bool on = d2 <= (DOT + grow) * (DOT + grow);
            if (!on && py + grow > 0) {
                float ax = px < 0 ? -px : px;
                if (py + grow >= ax * 0.9f - grow)
                    for (int k = 0; k < 3 && !on; k++) {
                        float r0 = rIn[k] - grow, r1 = rIn[k] + T + grow;
                        if ((arcMask >> k & 1) && d2 >= r0 * r0 && d2 < r1 * r1) on = true;
                    }
            }
            hit += on;
        }
        return hit;
    }
    static void wifiPass(Canvas &c, int x0, int y0, int arcMask, Color col, float grow) {
        for (int y = 0; y < WIFI_H; y++) for (int x = 0; x < WIFI_W; x++) {
            int cv = wifiCov(x, y, arcMask, grow);
            if (!cv) continue;
            c.setPixel(x0 + x, y0 + y, rgba(col.r, col.g, col.b, (u8)(col.a * cv / 16)));
        }
    }

    // Width reserved by the icon (0 for NONE) and its right-aligned draw; vertical centre = cy.
    static int wifiWidth(const Gfx &g, int st) {
        return st == WIFI_NONE ? 0 : WIFI_W + (st == WIFI_AP ? 3 + g.textWidth(GF_OSD_21, "AP") : 0);
    }
    void drawWifi(Canvas &c, const Gfx &g, int st, long now, int x, int cy) {
        if (st == WIFI_NONE) return;
        int lit = st == WIFI_CONNECTING ? (int)((now / 350) % 4) : 3;      // arcs lit: sweeps while connecting
        int litMask = (1 << lit) - 1;
        const int y = cy - 14;                    // bbox of dot+arcs spans box rows ~1.5..25.7 -> centre 13.6
        wifiPass(c, x, y, 7, rgba(0, 0, 0, 110), 1.3f);                 // soft shadow
        if (st == WIFI_CONNECTING) {
            wifiPass(c, x, y, 7, rgba(255, 255, 255, 80), 0);           // dim full fan
            wifiPass(c, x, y, litMask, kWhite, 0);
        } else wifiPass(c, x, y, 7, kWhite, 0);
        if (st == WIFI_AP) g.text(c, GF_OSD_21, x + WIFI_W + 3, cy + 7, "AP", kWhite);
    }

    static void fmtTime(int sec, char *out, int n) {   // 0:12, 29:00, 1:05:00
        if (sec < 0) sec = 0;
        int h = sec / 3600, m = sec / 60 % 60, ss = sec % 60;
        if (h > 0) snprintf(out, n, "%d:%02d:%02d", h, m, ss); else snprintf(out, n, "%d:%02d", m, ss);
    }

    // W--o--T zoom bar (after a zoom lever press) and NEAR--o--FAR bar (MF / DMF).
    void drawBars(Canvas &c, const Gfx &g, const CameraState &s, long now) {
        int fm = 0; s.get(SET_FOCUS_MODE, &fm);
        if (s.mfDistMm != lastMf_) { if (lastMf_ != 1) mfUntil_ = now + kMfShowMs; lastMf_ = s.mfDistMm; }
        if ((fm == 6 || fm == 10) && s.mfDistMm != 0 && now < mfUntil_) {   // transient, like Track A's focus-position bar
            int frac = s.mfDistMm < 0 ? 1000
                     : (s.mfNearestMm > 0 && s.mfDistMm > s.mfNearestMm ? 1000 - s.mfNearestMm * 1000 / s.mfDistMm : 0);
            char t[16];
            if (s.mfDistMm < 0) snprintf(t, sizeof t, "INF");
            else if (s.mfDistMm < 1000) snprintf(t, sizeof t, "%d.%02dm", 0, s.mfDistMm / 10);
            else snprintf(t, sizeof t, "%d.%dm", s.mfDistMm / 1000, (s.mfDistMm % 1000) / 100);
            drawBar(c, g, 372, GF_OSD_24, "NEAR", "FAR", frac, t);
        }
        if (s.zoomPermille >= 0 && now < zoomUntil_) {
            char t[24];
            if (s.focalMm > 0 && s.zoomMag10 > 0) snprintf(t, sizeof t, "%dmm  x%d.%d", s.focalMm, s.zoomMag10 / 10, s.zoomMag10 % 10);
            else if (s.focalMm > 0) snprintf(t, sizeof t, "%dmm", s.focalMm);
            else t[0] = 0;
            drawBar(c, g, 408, GF_OSD_26, "W", "T", s.zoomPermille, t);
        }
    }

    // ZoomBar row of Track A: label, dp(180) x dp(14) track + knob (6px wide, 2px inset), label, 12dp, 16sp text.
    static void drawBar(Canvas &c, const Gfx &g, int base, int lf, const char *l, const char *r, int permille, const char *txt) {
        const int bw = dp(180), bh = dp(14);
        const int tf = GF_OSD_26;
        int wl = g.textWidth(lf, l), wr = g.textWidth(lf, r);
        int wt = txt[0] ? g.textWidth(tf, txt) + dp(12) : 0;
        int total = wl + bw + wr + wt;
        int x = (PANEL_W - total) / 2;
        g.text(c, lf, x, base, l, kWhite); x += wl;
        int cy = base - 9;
        c.fillRect(x + 8, cy - 1, bw - 16, 3, rgba(255, 255, 255, 192));
        int kx = x + 8 + (bw - 16) * permille / 1000;
        c.fillRect(kx - 3, cy - bh / 2 + 1, 6, bh - 2, rgb(0xff, 0x9a, 0x2e));
        x += bw;
        g.text(c, lf, x, base, r, kWhite); x += wr;
        if (txt[0]) g.text(c, tf, x + dp(12), base, txt, kWhite);
    }

    // Shutter blackout: a short dark pulse over the live view right after a capture.
    void drawFlash(Canvas &c, const CameraModel &m, long now) {
        if (!m.lastCaptureMs()) return;
        long age = now - m.lastCaptureMs();
        if (age < 0 || age >= kFlashMs) return;
        c.fillRect(0, 0, PANEL_W, PANEL_H, rgba(0, 0, 0, (u8)(230 - 230 * age / kFlashMs)));
    }

    // Self-timer: large orange seconds in the middle (Track A: 96dp, #ff7a2e).
    void drawCountdown(Canvas &c, const Gfx &g, int sec) {
        char t[8]; snprintf(t, sizeof t, "%d", sec);
        g.textBig(c, GF_OSD_40, PANEL_W / 2, PANEL_H / 2 + 4, t, rgb(0xff, 0x7a, 0x2e), 4, 4, false);   // golden: no shadow on the countdown
    }

    // OsdIcons.Battery: stroked body (stroke = max(1.5, h/8)), nub 0.1w at 30..70% h, fill inset 1.6*stroke.
    static void drawBattery(Canvas &c, int x, int y, int w, int h, int pct) {
        int st = h / 8; if (st < 2) st = 2;
        int nub = w / 10; if (nub < 2) nub = 2;
        int bw = w - nub;
        c.fillRect(x, y, bw, st, kWhite);                       // body outline
        c.fillRect(x, y + h - st, bw, st, kWhite);
        c.fillRect(x, y, st, h, kWhite);
        c.fillRect(x + bw - st, y, st, h, kWhite);
        c.fillRect(x + bw, y + h * 3 / 10, nub, h * 4 / 10, kWhite);
        int inset = (st * 16 + 5) / 10;
        Color f = pct <= 20 ? rgb(255, 64, 64) : kWhite;
        int fw = (bw - 2 * inset) * pct / 100;
        if (fw > 0) c.fillRect(x + inset, y + inset, fw, h - 2 * inset, f);
    }

    void drawBottom(Canvas &c, const Gfx &g, const CameraState &s, App &app) {
        const CameraModel &m = app.model();
        int act = activeAxis(s);
        const int base = 462;
        // text pieces
        char ss[24], ap[16], ev[16], iso[16];
        fmtShutter(s, ss, sizeof ss);
        if (s.apertureX100 > 0) {
            int a = s.apertureX100;
            if (a % 100 == 0) snprintf(ap, sizeof ap, "F%d", a / 100);
            else snprintf(ap, sizeof ap, "F%d.%d", a / 100, (a % 100) / 10);
        } else ap[0] = 0;
        int evv = 0; bool hasEv = s.get(SET_EV, &evv);
        if (hasEv) fmtEv(evv, ev, sizeof ev); else ev[0] = 0;
        int isov = 0; bool hasIso = s.get(SET_ISO, &isov);
        if (hasIso) { if (isov == 0) snprintf(iso, sizeof iso, "AUTO"); else snprintf(iso, sizeof iso, "%d", isov); }
        else iso[0] = 0;

        bool enS = m.adjustable(AXIS_SHUTTER), enA = m.adjustable(AXIS_APERTURE);
        bool enE = m.adjustable(AXIS_EV), enI = m.adjustable(AXIS_ISO);

        // Track A bottom row is a centred LinearLayout: shutter, 26dp, aperture, 26dp, [EV icon 6dp EV],
        // 26dp, ISO icon, 5dp, ISO value. The spacers stay even when a text is hidden (no aperture ->
        // a double gap), so the row geometry is reproduced exactly instead of closing the gaps.
        const int F = GF_OSD_40;
        const int gap = dp(26), padEv = dp(6), padIso = dp(5);
        int evIcon = ICON_S_16_DD_PARTS_OSD_ICON_EV, isoIcon = ICON_S_16_DD_PARTS_OSD_ICON_ISO;
        int evW = 0, isoW = 0, evH = 0, isoH = 0;
        bool icons = g.hasIcons();
        if (icons) { g.iconSize(evIcon, &evW, &evH); g.iconSize(isoIcon, &isoW, &isoH); }

        int wS = ss[0] ? g.textWidth(F, ss) : 0;
        int wA = ap[0] ? g.textWidth(F, ap) : 0;
        int tE = ev[0] ? g.textWidth(F, ev) : 0;
        int wE = ev[0] ? tE + (icons ? evW + padEv : 0) : 0;
        int isoLabelW = icons ? isoW : g.textWidth(GF_OSD_26, "ISO");
        int wI = isoLabelW + padIso + (iso[0] ? g.textWidth(F, iso) : 0);
        int total = wS + gap + wA + gap + wE + gap + wI;
        int x = (PANEL_W - total) / 2 + 1;
        const int iconCy = base - 16;                  // compound drawable / ImageView centre

        if (wS) g.text(c, F, x, base, ss, color(act == AXIS_SHUTTER, enS));
        x += wS + gap;
        if (wA) g.text(c, F, x, base, ap, color(act == AXIS_APERTURE, enA));
        x += wA + gap;
        if (wE) {
            if (icons) { g.icon(c, evIcon, x, iconCy - evH / 2, enE ? 255 : 90); x += evW + padEv; }
            g.text(c, F, x, base, ev, color(act == AXIS_EV, enE));
        }
        x += (wE ? tE : 0) + gap;
        if (icons) g.icon(c, isoIcon, x, iconCy - isoH / 2, enI ? 255 : 90);
        else g.text(c, GF_OSD_26, x, base, "ISO", color(act == AXIS_ISO, enI));
        x += isoLabelW + padIso;
        if (iso[0]) g.text(c, F, x, base, iso, color(act == AXIS_ISO, enI));
        if (m.busy()) {                              // async shift in flight (~2 s)
            c.fillRect(PANEL_W / 2 - 12, base + 8, 24, 3, kActive);
        }
    }

    static Color color(bool active, bool enabled) { return !enabled ? kDisabled : active ? kActive : kWhite; }

    void drawFocusFrame(Canvas &c, const CameraState &s) {
        if (s.focusState == FOCUS_IDLE) return;
        // Track A FocusFrames: focused = green #3ecf6e, could not confirm = orange #ff9a2e.
        Color col = s.focusState == FOCUS_OK ? rgb(0x3e, 0xcf, 0x6e) : s.focusState == FOCUS_FAIL ? rgb(0xff, 0x9a, 0x2e) : kWhite;
        int w = 96, h = 72, x = (PANEL_W - w) / 2, y = (PANEL_H - h) / 2;
        Color dark = rgba(0, 0, 0, 150);
        c.rectOutline(x - 1, y - 1, w + 2, h + 2, dark);
        c.rectOutline(x + 1, y + 1, w - 2, h - 2, dark);
        c.rectOutline(x, y, w, h, col);
    }

    Target target_;
    long   lastUse_, zoomUntil_, mfUntil_;
    int    lastMf_;
    bool   infoOn_;
    bool   qbOpen_;     // quick bar (DOWN) is showing
    int    qbSel_;      // selected quick-bar slot
    int    modal_;      // ModalKind: mode / drive picker (MOD_NONE = closed)
    int    modalSel_;   // mode grid: selected index into kModeOrder
    int    dSel_, tSel_, dCol_;   // drive picker: drive row, timer row, active column (0=drive,1=timer)
};

} // namespace

Screen *createShootingScreen() { return new ShootingScreen(); }

} // namespace uicore
