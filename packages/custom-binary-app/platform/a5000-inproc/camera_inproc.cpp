// a5000 in-process CameraControl adapter (route (b)).
//
// Reads live engine state through the stock in-process path getModelInfo(
// "model/CAMERA", selector, out) -> ParamList::get(out, id) (TRACKB_INPROCESS.md
// §13/§16; param 0x12C = shooting mode is the one empirically read in-process),
// and SETs via the global CmnViewProcessDataMgr vtable (§18, image aspect, guarded,
// visible + reversible).  SDCom (0x1002) is blocked in-guest (ret -5), so this uses
// the in-process engine path exclusively.
//
// IMPORTANT (honesty): the raw model/CAMERA param ids and value encodings are a
// DIFFERENT namespace from uicore's catalog "A-side" values (the model-param <->
// catalog "R1 conversion table" is the documented remaining RE, NATIVE_PORT_PLAN
// §7-2 / M2).  Only 0x12C is proven in-process.  So readState() exercises and LOGS
// the live getModelInfo path (proving the in-process read works from this backend),
// and renders the shooting OSD from a representative, fully-formed CameraState.
// Each live value that becomes mappable can be dropped in here with zero UI change.
#include "uicore/platform.h"
#include "uicore/camera_state.h"
#include "uicore/catalog.h"
#include "uicore/log.h"
#include "engine.h"
#include <unistd.h>
#include <pthread.h>

using namespace uicore;

// Menu app-launch / action executor (actions_inproc.cpp -- kept out of this settings file).
namespace a5kinproc { int runSystemAction(Engine *eng, int kind, const char *arg); }

// SdcomClient (engine.h) crash-guard statics -- defined exactly once (this is the
// only TU that instantiates the client). Brick-0: disconnect on fatal signal.
namespace a5kinproc {
    SdcomClient *SdcomClient::s_active = 0;
    struct sigaction SdcomClient::s_old[3];
    const int SdcomClient::s_sig[3] = { SIGSEGV, SIGABRT, SIGTERM };
    bool SdcomClient::s_installed = false;
}

namespace {

// model/CAMERA param 0x12C carries the engine "s_exposureMode" D value (NOT the
// uicore A-side value). Map it to uicore SET_EXPOSURE_MODE (app/docs/
// settings_sdcom_map.tsv "SD s_exposureMode D=" column <-> kVals_EXPOSURE_MODE).
// a5k-supported modes only; unknown D -> AUTO. (D=11 = landscape = the stock boot
// mode, whose mountain icon the live OSD then reproduces.)
static int modeDtoUicore(int d) {
    switch (d) {
        case 1:  return 16;  // program-auto (P)
        case 2:  return 18;  // aperture-priority (A)
        case 3:  return 17;  // shutter-speed (S)
        case 4:  return 19;  // manual-exposure (M)
        case 6:  return 0;   // auto / iAuto
        case 9:  return 2;   // portrait
        case 11: return 3;   // landscape
        case 12: return 4;   // night
        case 13: return 5;   // night-portrait
        case 14: return 9;   // sunset
        case 16: return 12;  // sports / action
        default: return 0;   // unknown -> AUTO (logged raw)
    }
}

// uicore SET_EXPOSURE_MODE value -> engine enum of model/CAMERA 0x12A (RE_MODE_DIAL.md table: 1=P 2=A 3=S 4=M).
static int modeUicoreToEnum(int v) {
    switch (v) { case 16: return 1; case 18: return 2; case 17: return 3; case 19: return 4; default: return -1; }
}

// ---- live-read decode tables (re/scalar/osd_model_params.md, cross-checked by
//      the runtime group/param dump emulator/tools/mdlscan_so.c, 2026-11-16) ------
//
// Two live in-process read namespaces, both reached through the global singletons
// camera_inproc already holds (no live view object needed):
//   * PATH A = Engine::readModelParam("model/CAMERA", selector, paramId)  (0x12C pattern)
//   * PATH B = Engine::cvpdmGroupRead(eventId, &raw)  -- the CmnViewProcessDataMgr
//              singleton getParameter(eventId) the stock Wrapper getters use.
// PATH B returns the *raw* value; the stock OSD renders the *wrapper* value its
// Wrapper getter computes from raw. We replicate those (decompile-PROVEN) raw->wrapper
// conversions, then map wrapper -> uicore catalog value.

// model/CAMERA READ selectors (osd_model_params.md §1): the selector is a small
// group number that differs per param (TRACKB_INPROCESS §16.2).
enum {
    SEL_MODE     = 15,   // 0x12B/0x12C capture mode    (PATH A)
    SEL_WB       = 19,   // 0x138 WB preset             (PATH A) WrapperWhiteBalance
    SEL_APERTURE = 52,   // 0x1B5 F*100, 0x27E valid    (PATH A)
    SEL_SHUTTER  = 53,   // 0x1B8 num / 0x1B9 den, 0x27F valid (PATH A) WrapperShutterSpeed
    SEL_AELOCK   = 63,   // 0x1DC  ==1 locked           (PATH A)
    SEL_ISO      = 72,   // 0x1F4 cur / 0x1F5 autoMin / 0x1F6 autoMax (PATH A) WrapperIso
};
// CmnViewProcessDataMgr eventIds (= cvpdmGroupRead group) used by the OSD.
enum {
    EV_IMAGEQUAL = 2,    // storage-fmt
    EV_DRIVE     = 7,
    EV_FOCUSMODE = 13,
    EV_EXPCOMP   = 20,
    EV_METERING  = 22,
    EV_FOCUSAREA = 187,  // WrapperFocusArea::getFocusArea (0x21F412): vtable[3](mgr,187,&raw)
};

// WrapperFocusMode::getFocusMode conversion sub_21F666 (raw -> WRAPPER_FOCUS_MODE,
// decompile-PROVEN). default 7 = invalid. [osd_model_params.md §2.3]
static int wrapFocus(int raw) {
    switch (raw) { case 0: return 2; case 1: return 3; case 2: return 4;
                   case 3: return 5; case 4: return 6; case 6: return 1; default: return 7; }
}
// WrapperMeteringMode conversion sub_2219DE (raw -> WRAPPER_METERING_MODE). default 4.
static int wrapMeter(int raw) {
    switch (raw) { case 0: case 2: return 1; case 1: return 2; case 4: return 3; default: return 4; }
}

// FOCUS AREA: WrapperFocusArea::getFocusArea (viewUnified2 0x21F412) reads the
// CmnViewProcessDataMgr singleton via vtable[3](mgr, eventId 187, &raw) -- the exact
// same read path cvpdmGroupRead uses -- then converts raw->WRAPPER_FOCUS_AREA through
// sub_21F334. Both the read slot (vtable off 12) and the conversion table are
// DISASSEMBLY-PROVEN here (arm-none-eabi-objdump of the stock .so, 2026-11-17):
//   non-flex (the area-TYPE read, flags [this+4]==[this+5]==0): tbb table ->
//     raw 0->7 1->9 2->10 3->11 4->12 5->13 6->8 ; raw>6 -> fail (wrapper stays 15).
//   (flex coordinate read uses eventId 188 + a different conv; not needed for the OSD
//    area-type indicator -- getFocusArea's a4==0 path takes eventId 187.)
// default 15 = invalid. wrapper 7=Wide 9=Zone 10=Center 11=FlexSpot (the a5000 four
// area types) 8/12/13 = expand/semi variants.
static int wrapFocusArea(int raw) {
    switch (raw) { case 0: return 7; case 1: return 9; case 2: return 10; case 3: return 11;
                   case 4: return 12; case 5: return 13; case 6: return 8; default: return 15; }
}
// raw(eventId 187) -> uicore kVals_FOCUS_AREA A-side {multi0,center1,flex-spot3,spot5,
// local6,wide11}. The a5000 menu area types map Wide/Zone/Center/FlexSpot = raw 0/1/2/3.
// Name-literal mapping (Wide->wide, Center->center, FlexSpot->flex-spot); Zone has no
// uicore entry so it folds to multi, and the expand/semi variants (raw 4-6) fold to
// flex-spot. wrapper->label is LIKELY (no device to pin wide-vs-multi); returns -1 to
// leave SET_FOCUS_AREA unset (no fabrication) on an unknown/invalid raw. [?]
static int focusAreaRawToUi(int raw) {
    switch (raw) {
        case 0:  return 11;   // Wide       -> wide   [?] (could be uicore multi=0)
        case 1:  return 0;    // Zone       -> multi  [?] (no uicore "zone")
        case 2:  return 1;    // Center     -> center
        case 3:  return 3;    // Flex Spot  -> flex-spot
        case 4: case 5: case 6: return 3;  // expand/semi variants -> flex-spot [?]
        default: return -1;   // unknown/invalid -> leave unset, log raw
    }
}
// INVERSE of focusAreaRawToUi (uicore kVals_FOCUS_AREA A-side -> eventId-187 raw). Used by
// the SET path (setSetting SET_FOCUS_AREA). Canonical inverse of the injective entries only:
//   wide11->0 multi0->1 center1->2 flex-spot3->3. uicore spot(5)/local(6) have NO engine-area
//   raw in focusAreaRawToUi, so they return -1 (reject, no fabrication). Group 187 is the SAME
//   singleton read slot the OSD already reads (EV_FOCUSAREA), so a round-trip is emulator-provable
//   even though the wide-vs-multi label is [?]. [V emulator group-route] / semantic [?].
static int focusAreaUiToRaw(int ui) {
    switch (ui) { case 11: return 0; case 0: return 1; case 1: return 2; case 3: return 3;
                  default: return -1; }   // spot/local/unknown -> no route (honest)
}

// ---- focus/drive/WB decode tables (RE 2026-11-16) ------------------------------
//
// Cross-RE anchor: for the PATH-B getters the CmnViewProcessDataMgr raw value EQUALS
// the stock menu "value" field (re/settings/settings_tree.txt `14=`/`15=`), confirmed
// against both the decompiled Wrapper raw->wrapper switch AND the real-device data point
// (focus MF = raw 6). So we decode the raw directly to a uicore catalog value -- no need
// for the WrapperXxx icon-remap layer.

// FOCUS MODE: eventId 13 raw == settings_tree FocusMode `14=`:
//   0=AF-S(Single) 1=AF-A(Auto) 2=AF-C(Cont) 3=AF-Z(Adv.Cont) 4=DMF 5=SemiManual(MF) 6=MF.
// Device-PROVEN: MF = raw 6 (and sub_21F666 would map it to wrapper 1 -- the old code then
// mislabelled wrapper 1 as "AF-S", the bug being fixed here). uicore split: SET_FOCUS_MODE
// {auto0,manual6,dmf10} + SET_AF_MODE {af-s1,af-c2}. (focusMode, afMode) out-params:
static void focusRawToUi(int raw, int *fmode, int *afmode) {
    switch (raw) {
        case 0: *fmode = 0;  *afmode = 1; break;   // AF-S                 PROVEN (settings 14=0)
        case 1: *fmode = 0;  *afmode = 1; break;   // AF-A -> af-s         [?] no AF-A in uicore catalog
        case 2: *fmode = 0;  *afmode = 2; break;   // AF-C                 PROVEN (settings 14=2)
        case 3: *fmode = 0;  *afmode = 2; break;   // AF-Z (adv.cont)->af-c [?]
        case 4: *fmode = 10; *afmode = 1; break;   // DMF                  PROVEN (settings 14=4)
        case 5: *fmode = 6;  *afmode = 1; break;   // SemiManual (MF)      PROVEN (settings 14=5)
        case 6: *fmode = 6;  *afmode = 1; break;   // MF                   PROVEN device (settings 14=6)
        default:*fmode = 0;  *afmode = 1; break;   // fallback AF-S
    }
}

// INVERSE of focusRawToUi: (uicore SET_FOCUS_MODE, SET_AF_MODE) -> engine eventId-13 raw.
// Used by the focus-mode SET path (setSetting below). The combined engine focus value is
// a single enum (settings_tree FocusMode `14=`): 0=AF-S 2=AF-C 4=DMF 6=MF. uicore splits
// it across SET_FOCUS_MODE {auto0,manual6,dmf10} + SET_AF_MODE {af-s1,af-c2}; recombine:
static int focusUiToRaw(int fmode, int afmode) {
    if (fmode == 6)  return 6;                    // MF
    if (fmode == 10) return 4;                    // DMF
    return (afmode == 2) ? 2 : 0;                 // auto AF: af-c -> raw2, else af-s -> raw0
}

// INVERSE of driveRawToUi (uicore SET_DRIVE value -> engine eventId-7 raw). The eventId-7
// raw IS the engine drive code = settings_tree DriveMode `15=` (single=0 continuous=1
// speed-priority-cont=4 cont-bracket=12), cross-checked against the WrapperDriveMode icon
// remap meaningful-input cases (sub_21DAD4). uicore SET_DRIVE {single0,burst1,bracket2,
// spd-prior-burst3}. Self-timer is a SEPARATE uicore setting (app-local, item 11) and not
// carried in eventId 7, so it is not combined here. Returns -1 to leave the write unmade on
// an unknown value (no fabrication). bracket raw12 = continuous-bracket [?] (vs single-
// bracket raw22); the read side folds both to bracket, so this round-trips to "bracket".
static int driveUiToRaw(int drive) {
    switch (drive) { case 0: return 0; case 1: return 1; case 2: return 12; case 3: return 4; default: return -1; }
}

// WRAPPER_ISO -> ISO integer (re/osd/shooting_osd_full.md §1-A full-stop + §1-B 1/3-stop
// ladders). raw->wrapper is decompile-PROVEN (isoRawToWrapper / WrapperIso 0x220140); the
// wrapper->number mapping is LIKELY (standard a5000 ISO ladder, anchored wrap2=ISO100 and
// the 1/3-stop resId 451..481 = 1:1 atlas sequence). Returns the ISO integer, or 0 for
// AUTO/unknown (wrapper 15 or 79 = AUTO; out-of-table = 0 -> caller leaves it AUTO/unset).
// DEVICE-PREFERRED refinement: SDCom isoGain (camera_param 269) gives the exact integer
// directly, but in-guest SDCom is blocked (ret -5), so this ladder is the in-process path.
static int isoWrapperToNumber(int wrap) {
    // §1-A full-stop branch: wrapper 2..14 (raw 0x3A..0x48).
    static const int kFull[13] = { 100, 200, 400, 800, 1600, 3200, 6400, 12800,
                                   16000, 25600, 32000, 40000, 51200 };   // index = wrap-2
    if (wrap >= 2 && wrap <= 14) return kFull[wrap - 2];
    // §1-B 1/3-stop branch: wrapper 48..78 (raw 8..38, wrapper = raw+40).
    static const int kThird[31] = { 100, 125, 160, 200, 250, 320, 400, 500, 640, 800,
                                    1000, 1250, 1600, 2000, 2500, 3200, 4000, 5000, 6400, 8000,
                                    10000, 12800, 16000, 20000, 25600, 32000, 40000, 51200, 64000, 80000,
                                    102400 };                             // index = wrap-48
    if (wrap >= 48 && wrap <= 78) return kThird[wrap - 48];
    return 0;                                                             // AUTO / unknown
}

// WHITE BALANCE: model/CAMERA(@M00B) sel 19 paramId 0x138 raw == settings_tree
// WhiteBalance `14=`, cross-checked against WrapperWhiteBalance::getWhiteBalance switch
// (viewUnified2 0x224F24: raw1=Daylight 2=Shade 3=Cloudy 4=Incandescent 5..8=Fluor(WW/CW/
// DW/DL) 9=Flash 10=UnderwaterAuto 11=ColorTemp 13..15=Custom; raw0=AWB no icon). Mapped to
// uicore kVals_WB A-side value. Returns -1 when unknown (leave unset). PROVEN (raw->label).
static int wbRawToUi(int raw) {
    switch (raw) {
        case 0:  return 0;    // auto (AWB)
        case 1:  return 4;    // daylight
        case 2:  return 7;    // shade
        case 3:  return 5;    // cloudy-daylight
        case 4:  return 1;    // incandescent
        case 5:  return 3;    // fluorescent warm-white
        case 6:  return 8;    // fluorescent cool-white
        case 7:  return 9;    // fluorescent day-white
        case 8:  return 10;   // fluorescent daylight
        case 9:  return 11;   // flash
        case 10: return 16;   // underwater-auto
        case 11: return 15;   // color-temp / filter
        case 18: return 15;   // color-temp (alt raw) [?]
        case 12: case 13: case 14: case 15: return 14; // custom (1/2/3)
        default: return -1;   // unknown -> leave unset, log raw
    }
}
// INVERSE of wbRawToUi (uicore kVals_WB A-side -> model/CAMERA WB mode raw). Used by the WB
// SET path (setSetting SET_WB -> route A requestModelExecute cmd18 {0x136=mode}, spec §3). The
// A-side set matches the catalog kVals_WB exactly; the inverse is injective (custom folds to the
// canonical raw 12). Returns -1 on an A-side value with no engine raw (reject, no fabrication).
static int wbUiToRaw(int ui) {
    switch (ui) {
        case 0:  return 0;    // auto (AWB)
        case 4:  return 1;    // daylight
        case 7:  return 2;    // shade
        case 5:  return 3;    // cloudy-daylight
        case 1:  return 4;    // incandescent
        case 3:  return 5;    // warm-fluorescent
        case 8:  return 6;    // fluorescent cool-white
        case 9:  return 7;    // fluorescent day-white
        case 10: return 8;    // fluorescent daylight
        case 11: return 9;    // flash
        case 16: return 10;   // underwater-auto
        case 15: return 11;   // color-temp / filter
        case 14: return 12;   // custom (canonical raw 12)
        default: return -1;
    }
}

// DRIVE / SELF-TIMER: CmnViewProcessDataMgr eventId 7 raw == settings_tree DriveMode `15=`:
//   0=single 1=continuous 4=spd-priority-cont 5=self-timer 10=self-portrait 12=cont-bracket
//   22=single-bracket 32=WB-bracket 34=DRO-bracket 36=remote. CROSS-CHECKED two ways:
//   (1) the raw==menu-value pattern is device-PROVEN on focus (MF=raw6=FocusMode 14=6); and
//   (2) these exact raws are precisely the "meaningful" input cases of the WrapperDriveMode
//   icon remap sub_21DAD4 (input 0,1,4,5,10,12,22,32,34,36 -> icon idx 1,2,6,8,14,17,32,47,
//   50,52), so the eventId-7 raw IS the engine drive code = the menu value. The wrapper
//   (sub_21DAD4 output) is not needed. uicore SET_DRIVE {single0,burst1,bracket2,spd-prior-burst3},
//   SET_SELF_TIMER {10,2,0}. Sub-variant raws are conservatively folded (marked [?]); the
//   exact self-timer DURATION (10s vs 2s) is a separate sub-setting not in eventId 7, so only
//   the presence of self-timer (raw 5) is set, with 10s as the [?] default.
static void driveRawToUi(int raw, int *drive, int *selftimer) {
    *drive = 0; *selftimer = 0;
    switch (raw) {
        case 0:  *drive = 0; break;                  // single              PROVEN
        case 1:  *drive = 1; break;                  // continuous/burst    PROVEN
        case 2: case 3: *drive = 1; break;           // continuous Hi/Lo    [?]
        case 4:  *drive = 3; break;                  // speed-priority cont PROVEN
        case 5:  *drive = 0; *selftimer = 10; break; // self-timer          PROVEN (10s default [?])
        case 10: *drive = 0; break;                  // self-portrait -> single
        case 12: case 22: *drive = 2; break;         // bracket (cont/single) PROVEN
        case 32: case 34: *drive = 2; break;         // WB / DRO bracket -> bracket PROVEN
        case 36: *drive = 0; break;                  // remote -> single
        default: *drive = 0; break;                  // [?] unknown (incl. self-timer sub-variants) -> single
    }
}

// ISO: model/CAMERA(@M00B) sel 72 paramId 0x1F4 raw -> WRAPPER_ISO. The raw is an
// ASCII-coded enum (NOT the stock menu `14=` value, which is a separate namespace --
// verified), decompile-PROVEN from WrapperIso::getIso (viewUnified2 0x220140):
//   ascii branch raw 0x3A..0x45 -> wrapper 2..13, raw 0x48 -> 14;
//   int   branch raw 8..36       -> wrapper 48..76 (wrapper = raw+40);
//   anything else -> wrapper 79 = AUTO (the getter's *a3 default).
// Returns 79 for AUTO/invalid, else the WRAPPER_ISO (>=2).
static int isoRawToWrapper(int raw) {
    if (raw >= 0x3A && raw <= 0x45) return raw - 0x38;   // 0x3A->2 .. 0x45->13
    if (raw == 0x48)                return 14;           // 'H' extended
    if (raw >= 8 && raw <= 36)      return raw + 40;     // 8->48 .. 36->76
    return 79;                                           // AUTO / invalid
}
// WRAPPER_ISO -> OSD numeric STRING resource id (dword_376348[wrapper-2], viewUnified2
// 0x16D21C); -1 when none. This is a *resource id*, NOT the ISO integer -- the
// resource-id -> "ISO nnn" string table is a separate OSD string namespace that needs
// an AE-active device dump to pin. For LOG ONLY, so a device capture of raw+wrapper+resId
// against a known ISO immediately reveals the ladder. (The main still group, wrapper
// 2,4..11 -> resId 168..176, is 9 consecutive ISOs; coordinator captures the numbers.)
static int isoWrapperToResId(int wrap) {
    static const int kIsoResId[68] = {   // index = wrapper-2 (wrapper 2..69)
        168, -1,169,170,171,172,173,174,175,176, -1, -1, -1, 25, -1, -1, -1, -1,
        254,255,256,257,258,259,260, -1, -1, -1, -1, -1, -1, -1, -1, -1,261,262,
        263,264,265,266,267, -1, -1, -1, -1, -1, -1, -1, 26, 27, 28, -1, 29, -1,
        -1, 30, -1, -1, 31, -1, -1, 32, -1, -1, 33, -1, -1, 34 };
    int i = wrap - 2;
    if (i < 0 || i >= 68) return -1;
    return kIsoResId[i];
}

// Catalog ids with a proven in-process SET executor in InprocCamera::setSetting (W1 engine
// route). applySetting routes these to setSetting even when the apply table marks the item
// AM_SDCOM, so the stock MENU reaches the engine (in-guest SDCom is blocked).
static bool inprocSetRoute(int catalogId) {
    switch (catalogId) {
        case SET_EXPOSURE_MODE: case SET_FOCUS_MODE: case SET_AF_MODE:
        case SET_METERING: case SET_STORAGE_FMT: case SET_DRIVE: case SET_EV:
        case SET_WB:          // route A: requestModelExecute cmd18 {0x136} (device-seal apply)
        case SET_FOCUS_AREA:  // route B: CmnViewProcessDataMgr group 187 (emulator round-trip)
            return true;
        default: return false;
    }
}

// ---- generalized W1 group-route apply for the 140 MENU items --------------------------
// settings_readwrite_full.md §3: every "general" setting with a Wrapper reaches the engine
// through the SAME CmnViewProcessDataMgr singleton the OSD quick-bar already drives (route B,
// vtable[14] setParameter(eventId, raw) / read-back vtable[3]), eventId per setting. The apply
// TABLE (apply_gen.cpp) only carries inprocVals for aspect/storage/metering/face (items 1/2/29/
// 56); the rest are AM_SDCOM. In-guest SDCom is blocked (ret -5), so WITHOUT a route these menu
// items silently no-op on device. This table bridges the remaining route-B settings straight to
// the group path, read-back guarded, so the MENU reaches the engine. RAM-only, reversible.
//
// VALUE SEMANTICS honesty: for WB/focus-area/etc. with a decompile-PROVEN raw<->A-side decode we
// invert it (semantic PROVEN). For settings with NO documented A-side<->raw table (dro/hdr/flash/
// picture-effect/steadyshot), we pass the resolved A-side value straight through as the provisional
// engine raw: the group WRITE+read-back then proves the ROUTE round-trips (emulator), but the
// A-side->raw mapping stays [device-seal] until a real-device toggle-dump pins it (the project's
// documented enum-verification step). A wrong provisional map that the engine refuses read-backs
// as a mismatch -> APPLY_REJECTED, never a fake OK.
enum { GCONV_IDENTITY = 0, GCONV_WB = 1, GCONV_FOCUSAREA = 2 };
struct GroupApply {
    int item;        // kApply index (MENU item)
    int eventId;     // CmnViewProcessDataMgr group / Wrapper eventId (settings_readwrite_full.md §3)
    int conv;        // GCONV_*: A-side (ApplyCmd.sdcomValue) -> engine group raw (conv path only)
    char semantic;   // 'P' = raw<->A-side decode PROVEN (invertible), 'S' = provisional [device-seal]
    const char *note;
    // OPTION-INDEX raw map (2026-11-19): when rawByOpt != 0 the engine raw is taken DIRECTLY
    // from the menu option index (applySetting's `value`), rawByOpt[value], bypassing the SDCom
    // A-side value entirely. The per-option raws come from re/settings/settings_tree.txt field
    // 15 (the engine internal value) for the node whose field 13 (eventId) == this eventId; the
    // node was matched to our catalog item by exact label (scratchpad propose.py). A -1 entry =
    // our option has no engine equivalent -> APPLY_REJECTED (never a wrong write). This gives the
    // previously-dead AM_UNMAPPED settings AND the SDCom-only settings a read-back-guarded
    // in-process route that needs no SDCom connection and round-trips on the emulator.
    const short *rawByOpt;   // 0 = use conv path (legacy rows)
    short nOpt;              // length of rawByOpt
};
// RE 2026-11-18 (re/scalar/settings_readwrite_full.md sec.2.1, arm-none-eabi-objdump of the stock
// viewUnified2.so Wrapper getters/setters): every eventId below is now DISASSEMBLY-PROVEN (the
// WrapperXxx getter loads `movs r1,#eventId` before mgr->vtable[3]; settings_tree.txt `13=` field
// matches it 1:1). The raw<->wrapper conversion helpers were disassembled too; the remaining open
// link is A-side(SDCom catalog value) <-> engine group raw:
//   * FLASH (ev8): engine raw == settings_tree FlashMode `15=` {off0,auto1,fill2,slow3,rear5,wl6};
//     uicore kS17 A-side {0=off,1=auto,2=fill} (settings_sdcom_map.tsv) == raw {0,1,2} EXACTLY, so
//     GCONV_IDENTITY is CONFIRMED for the 3 wired options -> semantic 'P'.
//   * STEADYSHOT (ev208): engine raw is a BOOLEAN {on=1,off=0} (setStillSteadyShot conv sub_224D84
//     proven: wrapper1->raw1, wrapper2->raw0; raw1=ON,raw0=OFF). uicore A-side is anti-blur
//     {off0,keep1,onetime2}; only off=0->raw0 aligns, the "on" cases do NOT map cleanly, so this
//     stays device-seal ('S'). The read-back guard makes a wrong on-value a safe APPLY_REJECTED.
//   * DRO/HDR (ev24, SHARED): engine raw is a COMBINED range {off0, DRO 1-6, AutoHDR 7-13}
//     (settings_tree DRO_Auto_HDR). uicore splits it into dro-mode {off0,on1,auto2} and hdr-mode
//     {off0,on1,auto2} on the SAME group -> identity is wrong past off (hdr "on"=1 would set DRO 1,
//     not Auto-HDR 7). Device-seal ('S'); engine encoding now documented for the eventual level map.
//   * PICTURE-EFFECT (ev26): engine raw = ArtFilter enum with per-effect sub-variants (toy/part-color
//     etc.), so the 14 flat A-side effects (kS46) do NOT equal raw past off -> device-seal ('S').
// OPTION-INDEX -> engine raw (settings_tree field 15) for items matched to a stock node by exact
// label (2026-11-19). Index == our menu option index; -1 = no engine equivalent (-> REJECTED).
static const short kRawRedEye[]   = { 0, 1 };          // #20 [on,off]        RedEyeReduction ev12
static const short kRawLongExpNR[]= { 1, 0 };          // #37 [off,on]        StillLongExposureNR ev27
static const short kRawColorSpace[]={ 0, 1 };          // #54 [standard,expanded] StillColorSpace ev41 {sRGB0,AdobeRGB1}
static const short kRawSoftSkin[] = { 0, 3, 2, 1 };    // #57 [off,low,middle,high] SoftSkin ev32 {Off0,High1,Mid2,Low3}
static const short kRawWindNoise[]= { 1, 0, -1 };      // #9  [off,on,auto]   MovieWindNoiseReduction ev46 (no 'auto' raw)
static const short kRawTracking[] = { 0, 1 };          // #55 [off,on]        ObjectTracking ev29 {Off0,On1}
static const GroupApply kGroupApply[] = {
    // item, eventId, conv, semantic, note, rawByOpt, nOpt   (eventId = DISASM/settings_tree PROVEN)
    { 17, 8,   GCONV_IDENTITY,  'P', "flash-mode ev8 (SET_STROBE_MODE_CMD): A-side{off0,auto1,fill2}==raw PROVEN", 0, 0 },
    { 19, 9,   GCONV_IDENTITY,  'S', "flash-compensation ev9 (SET_STROBE_EV_SHIFT_CMD, signed int; raw==EV int [?])", 0, 0 },
    { 30, 24,  GCONV_IDENTITY,  'S', "dro-mode ev24 PROVEN; raw=combined{off0,DRO1-6,AutoHDR7-13} (A-side!=raw past off)", 0, 0 },
    { 32, 24,  GCONV_IDENTITY,  'S', "hdr-mode ev24 PROVEN; shares grp24 combined range -> on/auto need level sub-map", 0, 0 },
    { 46, 26,  GCONV_IDENTITY,  'S', "picture-effect ev26 PROVEN; raw=ArtFilter enum w/ sub-variants (A-side!=raw past off)", 0, 0 },
    { 59, 208, GCONV_IDENTITY,  'S', "x-steadyshot ev208 PROVEN boolean{on1,off0}; off=0 OK, on mapping device-seal", 0, 0 },
    { 80, 208, GCONV_IDENTITY,  'S', "anti-blur ev208 PROVEN boolean{on1,off0}; off=0 OK, onetime=2 invalid->REJECTED (safe)", 0, 0 },
    // --- option-index raw map (settings_tree §15 label-matched; read-back guarded) --------------
    { 20, 12,  GCONV_IDENTITY,  'P', "red-eye-reduction ev12 {on0,off1} settings_tree RedEyeReduction", kRawRedEye,    2 },
    { 37, 27,  GCONV_IDENTITY,  'P', "long-exposure-NR ev27 {off1,on0} settings_tree StillLongExposureNR", kRawLongExpNR, 2 },
    { 54, 41,  GCONV_IDENTITY,  'P', "color-space ev41 {standard=sRGB0,expanded=AdobeRGB1} settings_tree StillColorSpace", kRawColorSpace, 2 },
    { 57, 32,  GCONV_IDENTITY,  'P', "soft-skin ev32 {off0,low3,middle2,high1} settings_tree SoftSkin (non-positional)", kRawSoftSkin, 4 },
    {  9, 46,  GCONV_IDENTITY,  'P', "wind-noise ev46 {off1,on0,auto=reject} settings_tree MovieWindNoiseReduction", kRawWindNoise, 3 },
    { 55, 29,  GCONV_IDENTITY,  'S', "tracking-focus(Lock-on AF) ev29 {off0,on1} settings_tree ObjectTracking (ev30 alt)", kRawTracking, 2 },
};
static const GroupApply *groupApplyFor(int item) {
    for (unsigned i = 0; i < sizeof kGroupApply / sizeof kGroupApply[0]; i++)
        if (kGroupApply[i].item == item) return &kGroupApply[i];
    return 0;
}
static int groupConv(int conv, int aSide) {
    switch (conv) {
        case GCONV_WB:        return wbUiToRaw(aSide);
        case GCONV_FOCUSAREA: return focusAreaUiToRaw(aSide);
        default:              return aSide;   // GCONV_IDENTITY: provisional A-side==raw [device-seal]
    }
}

class InprocCamera : public CameraControl {
public:
    explicit InprocCamera(a5kinproc::Engine *eng) : eng_(eng), connected_(false), reads_(0) {}
    ~InprocCamera() { sdcom_.disconnect(); }   // brick-0: never leave the osal queue open

    // Non-setting menu action (app launch / stock action view). Forwards to actions_inproc.cpp.
    // Brick-0: RAM/mount only, never Backup/flash/NAND.
    int systemAction(int kind, const char *arg) { return a5kinproc::runSystemAction(eng_, kind, arg); }

    bool connect()        { connected_ = eng_ && eng_->ok(); return connected_; }
    void disconnect()     { sdcom_.disconnect(); connected_ = false; }
    bool connected() const { return connected_; }
    bool dryrun() const   { return false; }

    // M0 device-info probe (test screen only): surface a couple of live params.
    int readInfo(int *ids, int *vals, int max) {
        if (!eng_ || !eng_->ok() || max <= 0) return 0;
        int n = 0, v = 0;
        if (n < max && eng_->readModelParam("model/CAMERA", 15, 0x12C, &v)) { ids[n] = 0x12C; vals[n] = v; n++; }
        return n;
    }

    bool readState(CameraState *out) {
        if (!out || !eng_ || !eng_->ok()) return false;
        out->reset();
        out->valid = true;
        bool logNow = (reads_++ % 15) == 0;

        // ================= LIVE reads (real engine state) ====================
        // Each value below is read from the live engine through the stock
        // in-process path; the decode of each is backed by runtime evidence
        // (toggle-diff / boot dump, emulator/tools/mdlscan.log) and/or the
        // decompile map re/scalar/osd_model_params.md. Values with no reliable
        // in-process source/decode are left as honest placeholders (marked [?]).

        // --- exposure MODE: PATH A model/CAMERA 0x12C (PROVEN read) ----------
        int modeD = -1, aspect = -1, raw;
        bool gotMode = eng_->readModelParam("model/CAMERA", SEL_MODE, 0x12C, &modeD);
        bool gotAsp  = eng_->cvpdmRead(&aspect);
        int uiMode = gotMode ? modeDtoUicore(modeD) : 16;
        setV(out, SET_EXPOSURE_MODE, uiMode);                                  // LIVE (enum re-derivation still open [?])

        // --- focus MODE / AF-mode: PATH B eventId 13 (LIVE; labels RE-fixed) --
        // The eventId-13 raw is the engine focus value and EQUALS the stock menu
        // value (settings_tree FocusMode `14=`): 0=AF-S 1=AF-A 2=AF-C 3=AF-Z 4=DMF
        // 5=SemiManual(MF) 6=MF. Device-PROVEN: MF = raw 6. The OLD code converted the
        // raw through sub_21F666 to a WRAPPER value and then mislabelled wrapper 1 as
        // "AF-S" -- but wrapper 1 is MF (raw 6), so MF showed as AF-S. Fixed by decoding
        // the raw directly (focusRawToUi). fmWrap kept only for the cross-check log.
        int fmRaw = -1, fmWrap = 7, fmFM = 0, fmAF = 1;
        if (eng_->cvpdmGroupRead(EV_FOCUSMODE, &fmRaw)) {
            fmWrap = wrapFocus(fmRaw);
            focusRawToUi(fmRaw, &fmFM, &fmAF);
        }
        setV(out, SET_FOCUS_MODE, fmFM);
        setV(out, SET_AF_MODE, fmAF);

        // --- metering: PATH B eventId 22 (PROVEN; raw2->multi confirmed) -----
        if (eng_->cvpdmGroupRead(EV_METERING, &raw)) {
            int w = wrapMeter(raw);
            int m = (w == 1) ? 0 : (w == 2) ? 2 : (w == 3) ? 3 : -1;           // uicore METERING: multi0,cw2,cs3
            if (m >= 0) setV(out, SET_METERING, m);
        }

        // --- focus AREA: PATH B eventId 187 (LIVE; read slot + conv DISASM-PROVEN) --
        // cvpdmGroupRead(187) == the stock WrapperFocusArea::getFocusArea read. raw is
        // the engine area code (0=Wide 1=Zone 2=Center 3=FlexSpot ...). Mapped to uicore
        // best-effort (focusAreaRawToUi); unknown -> left unset (no fabrication). faWrap
        // kept for the cross-check log only.
        int faRaw = -1, faWrap = 15, faUi = -1;
        if (eng_->cvpdmGroupRead(EV_FOCUSAREA, &faRaw)) {
            faWrap = wrapFocusArea(faRaw);
            faUi   = focusAreaRawToUi(faRaw);
            if (faUi >= 0) setV(out, SET_FOCUS_AREA, faUi);
        }

        // --- storage-fmt (image quality): PATH B eventId 2 (OSD-PROVEN) ------
        // raw decode verified by stock OSD screendump (TRACKB §23.6): 0=RAW, 1=RAW+J, 2=JPEG.
        if (eng_->cvpdmGroupRead(EV_IMAGEQUAL, &raw)) {
            int sf = (raw == 0) ? 1 : (raw == 1) ? 2 : (raw == 2) ? 0 : -1;    // uicore STORAGE_FMT: jpeg0,raw1,rawjpeg2
            if (sf >= 0) setV(out, SET_STORAGE_FMT, sf);
        }

        // --- exposure compensation: PATH B eventId 20 (PROVEN numeric) ------
        // raw 15 = 0 EV, 1/3-EV steps; uicore SET_EV is a signed 1/3-EV count.
        int evRaw = -1;
        if (eng_->cvpdmGroupRead(EV_EXPCOMP, &evRaw) && evRaw >= 0 && evRaw <= 30)
            setV(out, SET_EV, 15 - evRaw);
        else setV(out, SET_EV, 0);

        // --- AE lock: PATH A sel 63 / 0x1DC (LIKELY bool) --------------------
        int ael = 0;
        if (eng_->readModelParam("model/CAMERA", SEL_AELOCK, 0x1DC, &ael))
            setV(out, SET_AE_LOCK, ael == 1 ? 1 : 0);
        else setV(out, SET_AE_LOCK, 0);

        // --- aperture: PATH A sel 52 / 0x1B5 F*100, guarded by 0x27E valid --
        // Format is decompile-PROVEN (osd_model_params §2.7); only trusted when the
        // engine flags it valid (AE-active). Emulator has no lens -> usually invalid.
        int apVal = -1, apValid = 0;
        bool apOk = eng_->readModelParam("model/CAMERA", SEL_APERTURE, 0x1B5, &apVal);
        eng_->readModelParam("model/CAMERA", SEL_APERTURE, 0x27E, &apValid);
        // DEVICE FINDING (2026-11-17): the 0x27E "valid" flag reads 0 on the real device
        // even when 0x1B5 carries a live, sane aperture (ap=180=F1.8 with the f/1.8 lens).
        // The valid selector/paramId is unconfirmed, so it is NOT trusted as the gate;
        // instead the sane F-number range F1.0..F63.9 (100..6399, x100) is the gate. apValid
        // is still read and logged for the eventual valid-paramId RE. Out of range -> placeholder.
        if (apOk && apVal >= 100 && apVal < 6400) out->apertureX100 = apVal;   // LIVE (sane-range gated)
        else out->apertureX100 = 560;                                         // placeholder F5.6 [?]

        // --- shutter: PATH A sel 53 / 0x1B8 num + 0x1B9 den, guarded by 0x27F -
        // Decode-PROVEN (WrapperShutterSpeed 0x2243E8 + formatter 0x194BCC): display
        // time = num/den seconds (num==0xFFFF -> BULB). uicore fmtShutter is ratio-based
        // (scale-invariant), so we reduce a fast shutter to 1/x and pass a slow one
        // through. Only trusted when 0x27F flags it valid (AE-active) -> emulator (no
        // lens) stays on the representative 1/160 placeholder. Marked [?]: needs device.
        int ssNum = -1, ssDen = -1, ssValid = 0;
        bool ssOk = eng_->readModelParam("model/CAMERA", SEL_SHUTTER, 0x1B8, &ssNum)
                 && eng_->readModelParam("model/CAMERA", SEL_SHUTTER, 0x1B9, &ssDen);
        eng_->readModelParam("model/CAMERA", SEL_SHUTTER, 0x27F, &ssValid);
        // DEVICE FINDING (2026-11-17): like 0x27E, the 0x27F shutter "valid" flag reads 0
        // on device even with a live sane shutter (ss=1/2500). So gate on a sane IMPLIED
        // time instead of the unconfirmed flag: fast num<den requires den/num <= 16000
        // (<=1/16000 s floor), slow num>=den requires num/den <= 120 (<=120 s). ssValid
        // still read+logged for the valid-paramId RE. num==0xFFFF = BULB.
        bool ssSane = ssOk && ssNum > 0 && ssDen > 0 &&
                      (ssNum < ssDen ? (ssDen / ssNum) <= 16000 : (ssNum / ssDen) <= 120);
        if (ssOk && ssNum == 0xFFFF) {                           // BULB: uicore has no bulb glyph
            out->shutterNum = 0; out->shutterDen = 0;            // blank; raw logged
        } else if (ssSane) {
            if (ssNum < ssDen) {                                  // fast: reduce to 1/x
                out->shutterNum = 1;
                out->shutterDen = (ssDen + ssNum / 2) / ssNum;
            } else { out->shutterNum = ssNum; out->shutterDen = ssDen; }   // slow: ratio
        } else { out->shutterNum = 1; out->shutterDen = 160; }   // placeholder 1/160 [?]

        // --- white balance: PATH A sel 19 / 0x138 (LIVE; raw==menu value PROVEN) --
        // wbRawToUi: 0x138 raw -> uicore kVals_WB A-side (settings_tree cross-checked).
        int wbRaw = -1;
        if (eng_->readModelParam("model/CAMERA", SEL_WB, 0x138, &wbRaw)) {
            int wbUi = wbRawToUi(wbRaw);
            if (wbUi >= 0) setV(out, SET_WB, wbUi); else setV(out, SET_WB, 0);
        } else setV(out, SET_WB, 0);                              // default auto

        // --- drive / self-timer: PATH B eventId 7 (LIVE; raw==menu value PROVEN) --
        int drvRaw = -1, drvUi = 0, stUi = 0;
        if (eng_->cvpdmGroupRead(EV_DRIVE, &drvRaw)) driveRawToUi(drvRaw, &drvUi, &stUi);
        setV(out, SET_DRIVE,      drvUi);
        setV(out, SET_SELF_TIMER, stUi);

        // --- ISO: PATH A sel 72 / 0x1F4 cur, 0x1F5 autoMin, 0x1F6 autoMax -------
        // raw->WRAPPER_ISO is decompile-PROVEN (isoRawToWrapper; WrapperIso 0x220140);
        // WRAPPER_ISO->OSD-resource-id is PROVEN too (isoWrapperToResId; 0x16D21C table).
        // The ONLY open link is resource-id -> ISO integer (a separate OSD string table
        // that needs an AE-active device dump; the raw 0x1F4 is ascii-coded and is NOT the
        // stock menu `14=` namespace -- verified). So the AUTO boundary (wrapper 79) IS
        // decoded and rendered, but a NUMBERED ISO is logged only (raw+wrapper+resId) and
        // left UNSET so the OSD shows the ISO icon with no (wrong) number until the device
        // capture pins the ladder. Emulator has no lens -> reads AUTO/invalid anyway.
        int isoRaw = -1, isoAutoMin = -1, isoAutoMax = -1, isoWrap = 79, isoRes = -1, isoNum = 0;
        bool isoOk = eng_->readModelParam("model/CAMERA", SEL_ISO, 0x1F4, &isoRaw);
        eng_->readModelParam("model/CAMERA", SEL_ISO, 0x1F5, &isoAutoMin);
        eng_->readModelParam("model/CAMERA", SEL_ISO, 0x1F6, &isoAutoMax);
        if (isoOk) { isoWrap = isoRawToWrapper(isoRaw); isoRes = isoWrapperToResId(isoWrap); isoNum = isoWrapperToNumber(isoWrap); }
        // SET_ISO carries the ACTUAL ISO INTEGER (shooting_osd.cpp:822 prints it directly;
        // 0 == AUTO). The old code left numbered ISO UNSET because it had no wrapper->number
        // table (OSD showed ISO icon with no number). Now mapped via isoWrapperToNumber
        // (shooting_osd_full.md §1 ladder). wrapper 15/79 = AUTO -> 0. An unknown wrapper
        // (isoNum 0 but not AUTO) still falls back to AUTO rather than a wrong number.
        if (!isoOk || isoWrap == 79 || isoWrap == 15) setV(out, SET_ISO, 0);   // AUTO
        else setV(out, SET_ISO, isoNum > 0 ? isoNum : 0);                      // numbered ISO integer (0->AUTO fallback)

        // --- battery %: model/POWER 2-STEP read (NOT model/CAMERA) -------------
        // RE-PROVEN (ACT_IconBattery sub_27D934 @viewUnified2 0x27D934): the fine percent is
        // model/POWER sel 1 paramId 0x10C, read with an INPUT ParamList {0x107 = (model/POWER
        // sel 0 paramId 0x105)}. Wired via the new Engine::readModelParamIn. (The stock code
        // buckets 0x10C >80/>50/>20/>0 for the 4 battery glyphs -> it is a 0..100 percent.)
        int batLvl = -1, batP = -1;
        bool batOk = false;
        if (eng_->readModelParam("model/POWER", 0, 0x105, &batLvl))     // step 1: sel-0 0x105 input value
            batOk = eng_->readModelParamIn("model/POWER", 1, 0x107, batLvl, 0x10C, &batP);  // step 2
        if (batOk && batP >= 0 && batP <= 100) out->batteryPct = batP;  // LIVE
        else out->batteryPct = 85;                                      // representative placeholder [?]

        // --- shots remaining: model/MEDIA sel 0 paramId 0x103 (NOT model/CAMERA) ----
        // RE-PROVEN path (shooting_osd_full.md §3; sub_17158C @0x17158C / sub_171E48):
        //   getModelInfo("model/MEDIA", 0) -> PrmNumberList::GET(list, 0x103) -> getNumber(0).
        // Validity gate: getModelInfo("model/MEDIA", 64) -> getValid(0x40) (sub_19D758 @0x19D758)
        // tells whether media is inserted/formatted. Our readModelParam reads the param's +12
        // int (the same accessor the 0x12C path proved); for a PrmNumberList the stock getter
        // uses getNumber(0), whose object layout vs +12 is NOT separately verified here (the
        // library offsets for PrmNumberList::GET/getNumber are not pinned) -> treated as a
        // best-effort read, gated by a sane range. [needs-device: card inserted] to seal the
        // getNumber(0) semantics. No card in the emulator -> validity 0 -> hidden (shots<0),
        // which is correct stock parity (stock hides the count when no media), NOT a fake 120.
        int mediaValid = 0, shotsN = -1;
        bool mvOk = eng_->readModelParam("model/MEDIA", 64, 0x40, &mediaValid);
        if (mvOk && mediaValid &&
            eng_->readModelParam("model/MEDIA", 0, 0x103, &shotsN) && shotsN >= 0 && shotsN < 1000000)
            out->shotsRemaining = shotsN;                              // LIVE (media valid)
        else
            out->shotsRemaining = -1;                                 // no valid media / unread -> hide (no fabrication)
        out->wifiStatus = WIFI_NONE;

        if (logNow) {
            logf("cam: LIVE mode0x12C=%s%d->ui%d asp=%d | focus raw%d->wrap%d ->FM%d/AF%d | area raw%d->wrap%d->ui%d | meter=%d qual=%d | ev raw%d | aelock=%d",
                 gotMode ? "" : "(x)", modeD, uiMode, gotAsp ? aspect : -1,
                 fmRaw, fmWrap, fmFM, fmAF,
                 faRaw, faWrap, faUi,
                 out->has[SET_METERING] ? out->v[SET_METERING] : -1,
                 out->has[SET_STORAGE_FMT] ? out->v[SET_STORAGE_FMT] : -1,
                 evRaw, out->v[SET_AE_LOCK]);
            logf("cam: LIVE drive raw%d->drv%d/st%d | wb raw%d->ui%d | ap=%d(valid%d) | ss %d/%d(valid%d)->%d/%d"
                 " | bat sel0-0x105=%d ->0x10C=%d(ok%d)->%d%% || iso raw%d->wrap%d resId%d num%d(autoMin%d,Max%d)"
                 " iso-set=%d media-valid%d shots=%d",
                 drvRaw, drvUi, stUi, wbRaw, out->has[SET_WB] ? out->v[SET_WB] : -1,
                 apVal, apValid, ssNum, ssDen, ssValid, out->shutterNum, out->shutterDen,
                 batLvl, batP, batOk ? 1 : 0, out->batteryPct,
                 isoRaw, isoWrap, isoRes, isoNum, isoAutoMin, isoAutoMax,
                 out->has[SET_ISO] ? out->v[SET_ISO] : -1, mediaValid, out->shotsRemaining);
            // Issue 2/3 instrument: capture the stock view state so the shooting-only OSD gating and
            // the "READING CAMERA" (model/CAMERA readable only when shooting is current-view) can be
            // pinned on device across modes (shooting / playback / stock-menu / app). camModelReadable
            // = did model/CAMERA mode read succeed this pass.
            logf("cam: VIEW native=%d funcMode=%d camModelReadable=%d",
                 eng_->viewIsNative(), eng_->currentFuncEventMode(), gotMode ? 1 : 0);
        }
        return true;
    }

    // SET (catalog): the exposure MODE through the stock enqueue path
    // requestModelExecute("model/CAMERA", 0x10, {0x12A = enum}) (RE_MODE_DIAL.md: the selector's real
    // engine apply; Backup writes are NOT done here). Accepted only when read-back (0x12C = engine D value)
    // shows the new mode. Everything else has no in-process route -> false (honest).
    bool setSetting(int catalogId, int val) {
        // Group-mapped catalog settings also reachable through the OSD quick bar
        // (shooting_osd.cpp kQbSlots) drive the CmnViewProcessDataMgr group route, not SDCom.
        // In-guest SDCom is blocked, so without this the metering quick bar silently no-ops in
        // the emulator even though a read-back-verified in-process route exists (TRACKB §23.3).
        // catalog value -> group internal maps are the confirmed apply_gen tables (items 29 / 2).
        // RAM-only + read-back guarded: a wrong map or a mode-inhibited write just returns false.
        // FOCUS MODE / AF MODE: the engine focus value is a single enum read via PATH B
        // eventId 13 (0=AF-S 2=AF-C 4=DMF 6=MF). uicore drives it as two catalog settings
        // (SET_FOCUS_MODE auto/manual/dmf + SET_AF_MODE af-s/af-c); set() carries only one,
        // so read the CURRENT focus raw to preserve the other half, recombine, and write the
        // SAME CmnViewProcessDataMgr singleton via group 13 (the write path metering/storage
        // already use) + read-back. PROOF CASE for the SET->engine route: toggling AF<->MF
        // here must make the group-13 read-back change. RAM-only, reversible, read-back guarded.
        if (catalogId == SET_FOCUS_MODE || catalogId == SET_AF_MODE) {
            if (!eng_ || !eng_->ok()) return false;
            int cur = -1, curFM = 0, curAF = 1;
            if (eng_->cvpdmGroupRead(EV_FOCUSMODE, &cur) && cur >= 0) focusRawToUi(cur, &curFM, &curAF);
            int fm = (catalogId == SET_FOCUS_MODE) ? val : curFM;
            int af = (catalogId == SET_AF_MODE)    ? val : curAF;
            int raw = focusUiToRaw(fm, af);
            int rb = groupSetChecked(EV_FOCUSMODE, raw);
            logf("cam: set focus cat%d val%d (fm%d af%d) -> group13 raw%d read-back %d",
                 catalogId, val, fm, af, raw, rb);
            return rb == raw;
        }
        // DRIVE: eventId-7 raw is the engine drive code (settings_tree DriveMode `15=`),
        // read via PATH B. Write the inverse (driveUiToRaw) through CmnViewProcessDataMgr
        // group 7 + read-back. Same W1 route-B path as metering/focus (RE_SETTINGS §2 grp7
        // -> SET_DRIVE_MODE_CMD). RAM-only, reversible, read-back guarded.
        if (catalogId == SET_DRIVE) {
            if (!eng_ || !eng_->ok()) return false;
            int raw = driveUiToRaw(val);
            if (raw < 0) return false;
            int rb = groupSetChecked(7, raw);
            logf("cam: set drive val%d -> group7 raw%d read-back %d", val, raw, rb);
            return rb == raw;
        }
        // EXPOSURE COMPENSATION: eventId-20 raw, 0 EV = raw 15, 1/3-EV steps (PROVEN numeric).
        // readState decodes uiEv = 15 - raw, so the write is raw = 15 - uiEv. Group 20 +
        // read-back. (SET_EV is also dial-driven; the menu item routes here too.)
        if (catalogId == SET_EV) {
            if (!eng_ || !eng_->ok()) return false;
            int raw = 15 - val;
            if (raw < 0 || raw > 30) return false;
            int rb = groupSetChecked(20, raw);
            logf("cam: set EV val%d -> group20 raw%d read-back %d", val, raw, rb);
            return rb == raw;
        }
        // FOCUS AREA: eventId-187 raw is the engine area code, read via PATH B (EV_FOCUSAREA,
        // the exact read slot WrapperFocusArea::getFocusArea uses). Write the inverse
        // (focusAreaUiToRaw) through CmnViewProcessDataMgr group 187 + read-back -- the SAME
        // W1 route-B path metering/drive use. Emulator round-trip provable; label semantic [?].
        if (catalogId == SET_FOCUS_AREA) {
            if (!eng_ || !eng_->ok()) return false;
            int raw = focusAreaUiToRaw(val);
            if (raw < 0) { logf("cam: set focus-area val%d -> no engine raw (reject)", val); return false; }
            int rb = groupSetChecked(EV_FOCUSAREA, raw);
            logf("cam: set focus-area val%d -> group187 raw%d read-back %d", val, raw, rb);
            return rb == raw;
        }
        // WHITE BALANCE: route A (requestModelExecute cmd18 {0x136=mode}, settings_readwrite_full.md
        // §3 PROVEN cmd). wbUiToRaw inverts the PROVEN read decode. Enqueue via the same in-proc
        // path exposure-mode uses (modelSet1), then poll the PATH-A read (sel19 0x138) for the raw
        // to confirm the async apply. [device-seal]: the emulator (no AE/lens) may not reflect the
        // WB apply, in which case this read-back stays unchanged and returns false (honest) rather
        // than a fake OK. On the real device the read-back seals it. cmd 18 = 0x12.
        if (catalogId == SET_WB) {
            if (!eng_ || !eng_->ok()) return false;
            int raw = wbUiToRaw(val);
            if (raw < 0) { logf("cam: set WB val%d -> no engine raw (reject)", val); return false; }
            int r = eng_->modelSet1("model/CAMERA", 18, 0x136, raw);
            logf("cam: set WB val%d -> model/CAMERA cmd18 {0x136=%d} ret=%d", val, raw, r);
            if (r < 0) return false;
            for (int i = 0; i < 20; i++) {
                usleep(100 * 1000);
                int rb = -1;
                if (eng_->readModelParam("model/CAMERA", SEL_WB, 0x138, &rb) && rb == raw) {
                    logf("cam: WB read-back ok raw=%d", rb); return true;
                }
            }
            logf("cam: WB read-back did not reflect (engine did not apply / device-seal)");
            return false;
        }
        if (catalogId == SET_METERING || catalogId == SET_STORAGE_FMT) {
            int group, internal = -1;
            if (catalogId == SET_METERING) {                 // group 22: multi0->2, cw2->1, spot3->4
                group = 22;
                if (val == 0) internal = 2; else if (val == 2) internal = 1; else if (val == 3) internal = 4;
            } else {                                         // group 2: jpeg0->2, raw1->0, rawjpeg2->1
                group = 2;
                if (val == 0) internal = 2; else if (val == 1) internal = 0; else if (val == 2) internal = 1;
            }
            if (internal < 0 || !eng_ || !eng_->ok()) return false;
            int rb = groupSetChecked(group, internal);
            logf("cam: set catalog %d group %d = %d -> read-back %d", catalogId, group, internal, rb);
            return rb == internal;
        }
        if (catalogId != SET_EXPOSURE_MODE) return false;
        int eng = modeUicoreToEnum(val);
        if (eng < 0 || !eng_ || !eng_->ok()) return false;
        int r = eng_->modelSet1("model/CAMERA", 0x10, 0x12A, eng);
        logf("cam: mode set model/CAMERA cmd 0x10 {0x12A=%d} ret=%d", eng, r);
        if (r < 0) return false;
        for (int i = 0; i < 20; i++) {                       // pump applies asynchronously
            usleep(100 * 1000);
            int d = -1;
            if (eng_->readModelParam("model/CAMERA", SEL_MODE, 0x12C, &d) && modeDtoUicore(d) == val) {
                logf("cam: mode read-back ok D=%d", d); return true;
            }
        }
        logf("cam: mode read-back did not change (engine did not apply)");
        return false;
    }

    // Generic apply for the 140 menu items (conversion table, uicore/apply.h). RAM/runtime only.
    // In-process routes that exist: image aspect (§18 vtable, guarded, read-back verified).
    // SDCom-mapped items cannot be executed in-process (SDCom 0x1002/0x1003 is blocked in-guest, ret -5):
    // reported as APPLY_NO_ROUTE rather than faked.  Backup ids are recorded, never written.
    int applySetting(int item, int value) {
        ApplyCmd c;
        if (!applyResolve(item, value, &c)) return APPLY_UNMAPPED;
        // ---- option-index group-route (settings_tree label-matched) -------------------
        // Consulted FIRST so it also catches items the apply TABLE marks AM_UNMAPPED (which the
        // method switch below would short-circuit) and AM_SDCOM items we can drive in-process.
        // raw is read straight from the menu option index via the curated settings_tree §15 map
        // (eventId = field 13). Write through the CmnViewProcessDataMgr group + read-back guard:
        // APPLY_OK only on a matching read-back, so a wrong/absent raw (-1) is a safe REJECT, never
        // a fake OK or a wrong write. No SDCom needed; round-trips on the emulator. RAM-only.
        {
            const GroupApply *g = groupApplyFor(item);
            if (g && g->rawByOpt && eng_ && eng_->ok()) {
                int raw = (value >= 0 && value < g->nOpt) ? g->rawByOpt[value] : -1;
                if (raw < 0) {
                    logf("cam: apply %s -> group%d: option %d has no engine raw (reject)",
                         kApply[item].key, g->eventId, value);
                    return APPLY_REJECTED;
                }
                int rb = groupSetChecked(g->eventId, raw);
                logf("cam: apply %s -> W1 group%d opt%d raw%d read-back %d [%s %s]",
                     kApply[item].key, g->eventId, value, raw, rb,
                     g->semantic == 'P' ? "semantic-PROVEN" : "semantic-device-seal", g->note);
                return rb == raw ? APPLY_OK : APPLY_REJECTED;
            }
        }
        switch (c.method) {
            case AM_LOCAL: case AM_ACTION: return APPLY_LOCAL;
            case AM_BACKUP:   logf("cam: apply %s -> backup 0x%08x recorded only", kApply[item].key, c.backupProp); return APPLY_RECORDED;
            case AM_UNMAPPED: return APPLY_UNMAPPED;
        }
        // In-process engine route via the global CmnViewProcessDataMgr singleton
        // (TRACKB §18/§23). IK_ASPECT = group 1; IK_GROUP = c.inprocGroup. Write the
        // internal value (vtable[14]) then read it back (vtable[3]); APPLY_OK only on a
        // matching read-back. A write the engine refuses in the current shooting mode
        // (ret 0 -> read-back unchanged) is APPLY_REJECTED, which is stock parity (the
        // stock menu greys the same items out via WrapperXxx::isInhOn), not a fake OK.
        if ((c.routes & AR_INPROC) && c.inprocOk &&
            (c.inprocKind == IK_ASPECT || c.inprocKind == IK_GROUP)) {
            int group = (c.inprocKind == IK_ASPECT) ? 1 : c.inprocGroup;
            int rb = groupSetChecked(group, c.inprocValue);
            logf("cam: apply %s inproc group %d = %d -> read-back %d", kApply[item].key, group, c.inprocValue, rb);
            return rb == c.inprocValue ? APPLY_OK : APPLY_REJECTED;
        }
        // ---- In-process catalog executor bridge (the user's MENU-apply bug fix) -------
        // Several catalog settings have a PROVEN in-process engine SET route via setSetting
        // (W1, settings_readwrite_full.md §1): focus-mode/af-mode -> CmnViewProcessDataMgr
        // group 13, drive -> group 7, exposure-comp -> group 20, metering -> 22, storage -> 2,
        // exposure-mode -> requestModelExecute cmd 0x10. The stock MENU drives these through
        // exactly this path. Our apply table, however, marks focus/af/drive/exp-comp as
        // AM_SDCOM (only SDCom A-side data was auto-generated for them), so WITHOUT this bridge
        // a MENU focus change resolved to AR_SDCOM, found no in-guest SDCom (blocked, ret -5),
        // and returned NO_ROUTE -- the engine never saw it. THIS is why "changing focus AF/MF
        // in the MENU did not apply" on device, even though the quick-bar setSetting worked.
        // For these catalog ids the SDCom A-side value == the uicore catalog value == what
        // setSetting expects (verified: focus kS21{0,6,10}, af kS22{1,2}, drive kS10{0,1,2,3},
        // EV int), so route c.catalogValue to setSetting, which writes the engine + read-back
        // guards. (AM_INPROC IK_GROUP items above keep their path; this only catches catalog
        // settings whose TABLE route is SDCom but that we can drive in-process.)
        if (c.sdcomOk && inprocSetRoute(c.catalogId)) {
            bool ok = setSetting(c.catalogId, c.catalogValue);
            logf("cam: apply %s -> in-proc setSetting(cat %d = %d) [W1, SDCom table route bypassed] = %s",
                 kApply[item].key, c.catalogId, c.catalogValue, ok ? "OK" : "REJECTED");
            return ok ? APPLY_OK : APPLY_REJECTED;
        }
        // ---- generalized W1 group-route bridge (kGroupApply) --------------------------
        // Remaining route-B settings (flash/dro/hdr/picture-effect/steadyshot...) whose apply
        // TABLE route is SDCom but that reach the engine in-process via the CmnViewProcessDataMgr
        // group path (settings_readwrite_full.md §3). Convert the resolved A-side value to the
        // engine group raw (PROVEN inverse where documented, else provisional [device-seal]),
        // write + read-back guard. Round-trip proves the ROUTE in the emulator; the A-side->raw
        // semantic for 'S' rows is device-sealed. RAM-only, reversible, brick-safe (no Backup).
        const GroupApply *ga = groupApplyFor(item);
        if (ga && c.sdcomOk && eng_ && eng_->ok()) {
            int raw = groupConv(ga->conv, c.sdcomValue);
            if (raw == APPLY_UNMAPPED_VAL || raw < 0) {
                logf("cam: apply %s -> group%d: A-side %d has no engine raw (reject)",
                     kApply[item].key, ga->eventId, c.sdcomValue);
                return APPLY_REJECTED;
            }
            int rb = groupSetChecked(ga->eventId, raw);
            logf("cam: apply %s -> W1 group%d raw%d read-back %d [%s %s]",
                 kApply[item].key, ga->eventId, raw, rb,
                 ga->semantic == 'P' ? "semantic-PROVEN" : "semantic-device-seal", ga->note);
            return rb == raw ? APPLY_OK : APPLY_REJECTED;
        }
        // ---- SDCom-only (W2) in-process executor --------------------------------------
        // Last resort for the settings with NO in-process engine route above (peaking/zebra/
        // AF-assist etc. and any SDCom-table item not bridged to W1). Build 0x1003
        // CameraSetParameters { 52=0, sdcomParam=sdcomValue } via libsdcom resolved PURELY BY
        // OFFSET (engine.h SdcomClient -- 0 undefined dynamic symbols, LD_PRELOAD-safe). The
        // send is honest: SR_OK only if the daemon acked; SR_REJECT -> REJECTED; and when SDCom
        // is blocked in-guest (emulator/stock screen, connect ret -5) or libsdcom is not mapped,
        // NO_ROUTE -- never faked. SD_DRYRUN assembles + logs the message but does not send.
        if (c.sdcomOk && c.sdcomMsg == a5kinproc::SD_MSG_SETPARAMS) {
            int sr = sdcom_.setParam(c.sdcomParam, c.sdcomValue);
            logf("cam: apply %s -> SDCom 0x%x param %d = %d: sr=%d%s",
                 kApply[item].key, c.sdcomMsg, c.sdcomParam, c.sdcomValue, sr,
                 sdcom_.dryrun() ? " (SD_DRYRUN)" : "");
            switch (sr) {
                case a5kinproc::SR_OK:     return APPLY_OK;
                case a5kinproc::SR_REJECT: return APPLY_REJECTED;
                default:                   return APPLY_NO_ROUTE;   // NOCONN / NOLIB / DRYRUN
            }
        }
        if (c.sdcomOk) {
            logf("cam: apply %s -> SDCom msg 0x%x param %d = %d: no in-process executor (non-0x1003 msg)",
                 kApply[item].key, c.sdcomMsg, c.sdcomParam, c.sdcomValue);
            return APPLY_NO_ROUTE;
        }
        return APPLY_UNMAPPED;
    }

    // Relative exposure shift (dial in shooting mode). Native in-process path, RE-PROVEN
    // (2026-11-17, WrapperShutterSpeed::setShutterSpeed @0x224588 / WrapperAperture::
    // setAperture @0x21C7CC): requestModelExecute("@M00B", cmd, ParamList{ shiftParamId =
    // +1|-1 }) -- the SAME proven in-proc enqueue path exposure-mode uses, one relative
    // step per call. shutter cmd 0x2C paramId 0x1A8, aperture cmd 0x2D paramId 0x1A7. The
    // model polls the read-back (now real, issue-3 fix) to confirm the async apply. Program
    // shift has no RE'd native paramId yet -> honest false. RAM-only, reversible, brick-safe.
    bool exposureStep(int axis, int steps) {
        if (!eng_ || !eng_->ok() || steps == 0) return false;
        unsigned long cmd; unsigned int pid;
        switch (axis) {
            case AXIS_SHUTTER:  cmd = 0x2C; pid = 0x1A8; break;
            case AXIS_APERTURE: cmd = 0x2D; pid = 0x1A7; break;
            default: logf("cam: exposureStep axis=%d -> no native route (program shift unmapped)", axis); return false;
        }
        int dir = steps > 0 ? 1 : -1;
        int n = steps > 0 ? steps : -steps;
        if (n > 16) n = 16;                                   // clamp a queued burst
        int r = 0;
        for (int i = 0; i < n && r >= 0; i++) r = eng_->modelSet1("@M00B", cmd, pid, dir);
        logf("cam: exposureStep axis=%d steps=%d -> @M00B cmd0x%lx {0x%x=%d} x%d ret=%d",
             axis, steps, cmd, pid, dir, n, r);
        return r >= 0;
    }

    // Bonus: visible + reversible engine SET via the global vtable. Returns the
    // value actually read back, or -1 on a guarded failure (no forced pointers).
    int setAspect(int internalVal) {
        if (!eng_ || !eng_->ok()) return -1;
        if (!eng_->cvpdmWrite(internalVal)) { logf("cam: setAspect(%d) write refused/guarded", internalVal); return -1; }
        int rb = -1; eng_->cvpdmRead(&rb);
        logf("cam: setAspect(%d) -> read-back %d", internalVal, rb);
        return rb;
    }
    int readAspect() { int v = -1; eng_->cvpdmRead(&v); return v; }
    // Generic CmnViewProcessDataMgr group SET + read-back (TRACKB §23). Writes the
    // internal value, waits for the pump, returns the value actually read back (or -1
    // on a guarded failure). Reverts nothing -- the caller's apply IS the change.
    int groupSetChecked(int group, int internalVal) {
        if (!eng_ || !eng_->ok()) return -1;
        eng_->cvpdmGroupWrite(group, internalVal);   // ret 0 = engine refused (inhibited); read-back tells
        usleep(200 * 1000);
        int rb = -1; return eng_->cvpdmGroupRead(group, &rb) ? rb : -1;
    }
    // Write + read back; -1 if the guarded write/read refused. Only values the table proved (1 = 16:9, 2 = 4:3).
    int setAspectChecked(int internalVal) {
        if (!eng_ || !eng_->ok() || !eng_->cvpdmWrite(internalVal)) return -1;
        usleep(200 * 1000);
        int rb = -1; return eng_->cvpdmRead(&rb) ? rb : -1;
    }

private:
    static void setV(CameraState *s, int id, int v) {
        if (id > SET_NONE && id < SET_COUNT) { s->has[id] = true; s->v[id] = v; }
    }
    a5kinproc::Engine *eng_;
    bool connected_;
    unsigned reads_;
    a5kinproc::SdcomClient sdcom_;   // lazy, offset-resolved; inert until an SDCom-only apply
};

} // namespace

namespace a5kinproc {

// ---- apply-path self-test (A5K_UI_APPLYTEST=1; emulator only) -------------------------------
// Drives CameraControl::applySetting() for a representative set of table rows and logs the result
// plus the engine read-back where one exists. Everything is reverted; nothing touches Backup/flash.
namespace {
struct TestArgs { CameraControl *cam; Engine *eng; int probe; };
const char *rn(int r) {
    switch (r) {
        case APPLY_OK: return "OK"; case APPLY_REJECTED: return "REJECTED"; case APPLY_NO_ROUTE: return "NO_ROUTE";
        case APPLY_UNMAPPED: return "UNMAPPED"; case APPLY_LOCAL: return "LOCAL"; case APPLY_RECORDED: return "RECORDED";
    }
    return "?";
}
void nap(int ms) { usleep(ms * 1000); }
void *testThread(void *vp) {
    TestArgs *a = (TestArgs *)vp; CameraControl *cam = a->cam; Engine *eng = a->eng;
    int d = -1, asp = -1;
    for (int i = 0; i < 3000; i++) {                       // engine answers getModelInfo only when fully up
        if (eng->readModelParam("model/CAMERA", SEL_MODE, 0x12C, &d) && eng->cvpdmRead(&asp)) break;
        nap(100);
    }
    nap(2000);
    eng->readModelParam("model/CAMERA", SEL_MODE, 0x12C, &d); eng->cvpdmRead(&asp);
    logf("applytest: BEFORE mode D=%d aspect=%d", d, asp);
    struct Row { int item, val; const char *what; } rows[] = {
        { 1, 1, "image-aspect 16:9 (inproc group 1, table value 1)" },
        { 1, 0, "image-aspect 3:2 (inproc, 3:2 internal unmapped)" },
        { 2, 1, "storage-fmt RAW (inproc group 2 -> internal 0)" },
        { 2, 2, "storage-fmt RAW+JPEG (inproc group 2 -> internal 1)" },
        { 2, 0, "storage-fmt JPEG (inproc group 2 -> internal 2)" },
        { 27, 3, "exposure-compensation +3 (SDCom route only)" },
        { 28, 5, "iso 250 (SDCom route only)" },
        { 24, 0, "flex-spot size (unmapped)" },
        { 11, 2, "self-timer (app-local pref)" },
        { 107, 1, "beep (backup propId recorded)" },
        { 111, 0, "format (gated action)" },
    };
    for (unsigned i = 0; i < sizeof rows / sizeof rows[0]; i++) {
        int r = cam->applySetting(rows[i].item, rows[i].val);
        int a2 = -1; eng->cvpdmRead(&a2);
        logf("applytest: apply #%d v=%d [%s] -> %s  (aspect read-back %d)", rows[i].item, rows[i].val, rows[i].what, rn(r), a2);
        nap(1500);
    }
    if (a->probe > 0) {                                   // raw probe of an internal aspect value (never shipped as a mapping)
        logf("applytest: ASPECT PROBE write %d", a->probe);
        eng->cvpdmWrite(a->probe); nap(1000);
        int rb = -1; eng->cvpdmRead(&rb);
        logf("applytest: ASPECT PROBE read-back %d (screendump window 40s)", rb);
        nap(40000);
        eng->cvpdmWrite(asp); nap(1000); rb = -1; eng->cvpdmRead(&rb);
        logf("applytest: ASPECT PROBE reverted -> %d", rb);
    }
    // exposure MODE via requestModelExecute(model/CAMERA, 0x10, {0x12A}) + 0x12C read-back, then revert to SCN(6).
    int r = cam->setSetting(SET_EXPOSURE_MODE, 16);       // program-auto
    int d2 = -1; eng->readModelParam("model/CAMERA", SEL_MODE, 0x12C, &d2);
    logf("applytest: setSetting(EXPOSURE_MODE=P) -> %d, 0x12C D %d -> %d", r, d, d2);
    nap(1500);
    // Settings inhibited in Scene mode but settable in P (metering, face) -- apply
    // via the inproc group route now that the camera is in P, with read-back.
    struct PRow { int item, val; const char *what; } prows[] = {
        { 29, 2, "metering center-weighted (inproc group 22 -> internal 1)" },
        { 29, 0, "metering multi (inproc group 22 -> internal 2)" },
        { 56, 1, "face-detection auto (inproc group 31 -> internal 1)" },
        { 56, 0, "face-detection off (inproc group 31 -> internal 0)" },
        // MENU-apply bridge fix: these items are AM_SDCOM in the table but now reach the
        // engine in-process. APPLY_OK below == the group read-back matched (setSetting guard),
        // i.e. the MENU change really hit the engine -- the user's AF/MF bug, proven fixed.
        { 21, 1, "MENU focus-mode MANUAL (bridge -> in-proc group 13)" },
        { 22, 1, "MENU af-mode AF-S (bridge -> in-proc group 13)" },
        { 21, 0, "MENU focus-mode AUTO (bridge -> in-proc group 13)" },
        { 10, 1, "MENU drive BURST (bridge -> in-proc group 7)" },
        { 10, 0, "MENU drive SINGLE (bridge -> in-proc group 7)" },
        // Generalized W1 route (part-of-everything-works). catalog-bridge settings:
        { 23, 5, "MENU focus-area WIDE (bridge -> in-proc group 187) [V route]" },
        { 23, 0, "MENU focus-area MULTI (bridge -> in-proc group 187) [V route]" },
        { 38, 0, "MENU white-balance AUTO (bridge -> route A cmd18 {0x136}) [device-seal]" },
        { 38, 3, "MENU white-balance DAYLIGHT (bridge -> route A cmd18) [device-seal]" },
        // kGroupApply route-B settings (semantic device-seal, route round-trip verifiable):
        { 17, 1, "MENU flash-mode (W1 group 8) [device-seal]" },
        { 30, 1, "MENU dro-mode (W1 group 24) [device-seal]" },
        { 46, 1, "MENU picture-effect (W1 group 26) [device-seal]" },
        { 59, 0, "MENU steadyshot (W1 group 208) [device-seal]" },
        // option-index raw map (settings_tree §15 label-matched) -- previously dead/SDCom-only,
        // now in-process + read-back guarded. OK == the menu change really hit the engine.
        { 20, 1, "MENU red-eye OFF (W1 grp12 opt1->raw1)" },
        { 20, 0, "MENU red-eye ON  (W1 grp12 opt0->raw0)" },
        { 37, 1, "MENU long-exp-NR ON (W1 grp27 opt1->raw0)" },
        { 54, 1, "MENU color-space EXPANDED/AdobeRGB (W1 grp41 opt1->raw1)" },
        { 54, 0, "MENU color-space STANDARD/sRGB (W1 grp41 opt0->raw0)" },
        { 57, 3, "MENU soft-skin HIGH (W1 grp32 opt3->raw1, non-positional)" },
        { 57, 0, "MENU soft-skin OFF (W1 grp32 opt0->raw0)" },
        { 55, 1, "MENU tracking-focus ON (W1 grp29 opt1->raw1)" },
        { 55, 0, "MENU tracking-focus OFF (W1 grp29 opt0->raw0)" },
        {  9, 2, "MENU wind-noise AUTO -> expect REJECTED (no engine raw, opt2->-1)" },
        {  9, 1, "MENU wind-noise ON (W1 grp46 opt1->raw0)" },
    };
    for (unsigned i = 0; i < sizeof prows / sizeof prows[0]; i++) {
        int pr = cam->applySetting(prows[i].item, prows[i].val);
        logf("applytest: [P] apply #%d v=%d [%s] -> %s", prows[i].item, prows[i].val, prows[i].what, rn(pr));
        nap(1200);
    }
    // ---- W1 group-route ROUND-TRIP probe (mechanism proof, semantic-independent) ----------
    // For each generalized route-B eventId (settings_readwrite_full.md §3), read current, write a
    // candidate raw, read it back (== route proven), then REVERT to the original. This proves the
    // CmnViewProcessDataMgr group path reaches the engine for the new settings INDEPENDENT of the
    // A-side->raw semantic (which is device-sealed for the 'S' rows). RAM-only, reversible.
    struct GRow { int ev; int cand; const char *what; } grows[] = {
        {   8, 1, "flash-mode grp8" },
        {   9, 0, "flash-comp grp9" },
        {  24, 1, "dro/hdr grp24" },
        {  26, 1, "picture-effect grp26" },
        { 187, 0, "focus-area grp187" },
        { 208, 1, "steadyshot grp208" },
    };
    for (unsigned i = 0; i < sizeof grows / sizeof grows[0]; i++) {
        int before = -1; bool rok = eng->cvpdmGroupRead(grows[i].ev, &before);
        int cand = (rok && before == grows[i].cand) ? (grows[i].cand ^ 1) : grows[i].cand;  // ensure a change
        eng->cvpdmGroupWrite(grows[i].ev, cand); nap(200);
        int after = -1; eng->cvpdmGroupRead(grows[i].ev, &after);
        const char *verdict = (after == cand) ? "ROUTE_OK" : (rok ? "refused/inhibited" : "no-read");
        logf("applytest: W1 round-trip %s ev%d before=%d(rd%d) write=%d read-back=%d -> %s",
             grows[i].what, grows[i].ev, before, rok ? 1 : 0, cand, after, verdict);
        if (rok) { eng->cvpdmGroupWrite(grows[i].ev, before); nap(150); }   // revert
        nap(400);
    }
    int rr = eng->modelSet1("model/CAMERA", 0x10, 0x12A, 6);   // restore scene mode (RE_MODE_DIAL enum 6)
    nap(3000); int d3 = -1; eng->readModelParam("model/CAMERA", SEL_MODE, 0x12C, &d3);
    logf("applytest: revert to SCN ret=%d, 0x12C D now %d", rr, d3);
    logf("applytest: DONE");
    return 0;
}
} // namespace

void runApplyTest(CameraControl *cam, Engine *eng, int probe) {
    static TestArgs args; args.cam = cam; args.eng = eng; args.probe = probe;
    pthread_t t; if (pthread_create(&t, 0, testThread, &args) == 0) pthread_detach(t);
}

// ---- focus-mode SET self-test (marker /android/a5k/focustest) -------------------------
// Proves the issue-(1) SET->engine route on the real device WITHOUT physical input: read the
// live focus (eventId 13), toggle AF<->MF via CameraControl::setSetting (-> group-13 write +
// read-back), log the read-back, hold, then REVERT to the original. RAM-only, reversible.
namespace {
struct FocusArgs { CameraControl *cam; Engine *eng; };
void *focusThread(void *vp) {
    FocusArgs *a = (FocusArgs *)vp; CameraControl *cam = a->cam; Engine *eng = a->eng;
    int raw = -1;
    for (int i = 0; i < 3000; i++) {                 // wait until the engine answers eventId 13
        if (eng->cvpdmGroupRead(13, &raw)) break;
        usleep(100 * 1000);
    }
    int fm = 0, af = 1; focusRawToUi(raw, &fm, &af);
    logf("focustest: BEFORE focus raw=%d (fm%d af%d)", raw, fm, af);
    bool wasMF = (fm == 6);
    // toggle: MF -> AF-S (SET_FOCUS_MODE=0), else -> MF (SET_FOCUS_MODE=6)
    int target = wasMF ? 0 : 6;
    bool ok = cam->setSetting(SET_FOCUS_MODE, target);
    int rb = -1; eng->cvpdmGroupRead(13, &rb);
    logf("focustest: setSetting(FOCUS_MODE=%d) -> %s ; group13 read-back raw=%d (expect %s)",
         target, ok ? "applied" : "not-applied", rb, wasMF ? "0" : "6");
    usleep(3000 * 1000);                             // hold 3s so an AF/MF change is observable
    // revert to the original focus mode
    int origFm = wasMF ? 6 : fm;
    bool ok2 = cam->setSetting(SET_FOCUS_MODE, origFm);
    int rb2 = -1; eng->cvpdmGroupRead(13, &rb2);
    logf("focustest: REVERT setSetting(FOCUS_MODE=%d) -> %s ; group13 read-back raw=%d",
         origFm, ok2 ? "applied" : "not-applied", rb2);

    // Exposure-SHIFT self-test (dial native path, issue-2/dial-exposure): only meaningful
    // in M/S/A (shutter/aperture adjustable). Read shutter, +1 step, read-back, revert -1.
    int sn0 = -1, sd0 = -1;
    eng->readModelParam("model/CAMERA", 53, 0x1B8, &sn0);
    eng->readModelParam("model/CAMERA", 53, 0x1B9, &sd0);
    logf("focustest: shutter BEFORE %d/%d", sn0, sd0);
    bool sok = cam->exposureStep(AXIS_SHUTTER, 1);
    usleep(2000 * 1000);
    int sn1 = -1, sd1 = -1;
    eng->readModelParam("model/CAMERA", 53, 0x1B8, &sn1);
    eng->readModelParam("model/CAMERA", 53, 0x1B9, &sd1);
    logf("focustest: shutter +1 sent=%d -> %d/%d (changed=%d)", sok, sn1, sd1, (sn1 != sn0 || sd1 != sd0) ? 1 : 0);
    cam->exposureStep(AXIS_SHUTTER, -1);             // revert
    usleep(1500 * 1000);
    int sn2 = -1, sd2 = -1;
    eng->readModelParam("model/CAMERA", 53, 0x1B8, &sn2);
    eng->readModelParam("model/CAMERA", 53, 0x1B9, &sd2);
    logf("focustest: shutter revert -> %d/%d", sn2, sd2);
    logf("focustest: DONE");
    return 0;
}
} // namespace
void runFocusTest(CameraControl *cam, Engine *eng) {
    static FocusArgs args; args.cam = cam; args.eng = eng;
    pthread_t t; if (pthread_create(&t, 0, focusThread, &args) == 0) pthread_detach(t);
}

// The image-aspect self-test (§18) is driven directly through the Engine from
// inproc.cpp, so the adapter only needs the factory here.
uicore::CameraControl *createInprocCamera(Engine *eng) { return new InprocCamera(eng); }
}
