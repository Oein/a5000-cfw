// a5000 physical-key -> action binding table (DATA).
// Raw codes = event0 ISV scan codes (CLAUDE.md §5; trash=595 per TRACKB_RUNTIME §2.1).
// The control wheel + 4-way, MENU/PLAY/MOVIE/trash, zoom lever, control dial.
//
// Context rules: in a MENU the wheel/4-way navigate; on the SHOOTING screen the
// control dial changes exposure (DIAL_MAIN). CTX_GLOBAL entries are the fallback.
#include "uicore/action.h"

namespace {
using namespace uicore;

// event0 ISV scan codes
enum {
    ISV_UP = 103, ISV_DOWN = 108, ISV_LEFT = 105, ISV_RIGHT = 106, ISV_ENTER = 232,
    ISV_DIAL_CW = 525, ISV_DIAL_CCW = 526, ISV_MENU = 514,
    ISV_S1A = 516, ISV_S1B = 517, ISV_MOVIE = 515, ISV_PLAY = 207,
    ISV_ZOOM_T = 610, ISV_ZOOM_W = 611, ISV_TRASH = 595, ISV_SK1 = 229
};

const Binding kBindings[] = {
    // --- global fallbacks (apply in every context) ---
    { ISV_ENTER,    CTX_GLOBAL, ACT_CONFIRM },
    { ISV_MENU,     CTX_GLOBAL, ACT_MENU },
    { ISV_PLAY,     CTX_GLOBAL, ACT_PLAYBACK },
    { ISV_MOVIE,    CTX_GLOBAL, ACT_MOVIE_REC },
    { ISV_TRASH,    CTX_GLOBAL, ACT_DELETE },
    { ISV_SK1,      CTX_GLOBAL, ACT_FN },
    { ISV_S1A,      CTX_GLOBAL, ACT_SHUTTER_HALF },
    { ISV_S1B,      CTX_GLOBAL, ACT_SHUTTER_HALF },
    { ISV_ZOOM_T,   CTX_GLOBAL, ACT_ZOOM_T },
    { ISV_ZOOM_W,   CTX_GLOBAL, ACT_ZOOM_W },
    { ISV_UP,       CTX_GLOBAL, ACT_NAV_UP },
    { ISV_DOWN,     CTX_GLOBAL, ACT_NAV_DOWN },
    { ISV_LEFT,     CTX_GLOBAL, ACT_NAV_LEFT },
    { ISV_RIGHT,    CTX_GLOBAL, ACT_NAV_RIGHT },
    { ISV_DIAL_CW,  CTX_GLOBAL, ACT_DIAL_MAIN_CW },
    { ISV_DIAL_CCW, CTX_GLOBAL, ACT_DIAL_MAIN_CCW },

    // --- MENU context: the control dial scrolls the list ---
    { ISV_DIAL_CW,  CTX_MENU,   ACT_NAV_DOWN },
    { ISV_DIAL_CCW, CTX_MENU,   ACT_NAV_UP },

    // --- SHOOTING context: dial = exposure (kept distinct from menu nav) ---
    { ISV_DIAL_CW,  CTX_SHOOTING, ACT_DIAL_MAIN_CW },
    { ISV_DIAL_CCW, CTX_SHOOTING, ACT_DIAL_MAIN_CCW },
};

} // namespace

namespace a5000 {
const uicore::Binding *bindings(int *count) {
    *count = (int)(sizeof(kBindings) / sizeof(kBindings[0]));
    return kBindings;
}
}
