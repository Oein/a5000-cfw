// Abstract UI actions and contexts. Screens consume ACTIONS, never physical keys.
// Each platform maps its physical buttons/events to these via a binding table
// (platform/<model>/keymap.*). This lets a5000 (control wheel + MENU/PLAY/MOVIE/
// trash/zoom lever) and a6400 (front/rear dials, C1-C3, AEL, AF-ON, Fn, touch,
// 4-way) drive the same screens. See uicore/README.md.
#pragma once

namespace uicore {

enum Action {
    ACT_NONE = 0,
    ACT_NAV_UP, ACT_NAV_DOWN, ACT_NAV_LEFT, ACT_NAV_RIGHT,
    ACT_CONFIRM, ACT_BACK, ACT_MENU, ACT_PLAYBACK,
    ACT_SHUTTER_HALF, ACT_SHUTTER_FULL, ACT_MOVIE_REC, ACT_DELETE,
    ACT_DIAL_MAIN_CW, ACT_DIAL_MAIN_CCW, ACT_DIAL_SUB_CW, ACT_DIAL_SUB_CCW,
    ACT_ZOOM_T, ACT_ZOOM_W, ACT_FN,
    ACT_CUSTOM_1, ACT_CUSTOM_2, ACT_CUSTOM_3, ACT_CUSTOM_4, ACT_CUSTOM_5,
    ACT_TOUCH,                 // value unused; x,y carried in the event
    ACT_COUNT
};

// UI context selects which binding applies (e.g. wheel = NAV in a menu, exposure
// change on the shooting screen). CTX_GLOBAL is the fallback for every context.
enum Context {
    CTX_GLOBAL = 0,
    CTX_SHOOTING,
    CTX_MENU,
    CTX_PLAYBACK,
    CTX_TEXT,              // text-input widget (wheel scrubs the character strip)
    CTX_COUNT
};

// A single physical-key -> action rule. `raw` is the platform scan/key code.
// Tables are plain data (static arrays in platform/<model>/keymap.*).
struct Binding {
    int raw;       // platform raw code (a5000 ISV scan code, host ASCII, ...)
    int context;   // Context; CTX_GLOBAL matches any context as fallback
    int action;    // Action
};

const char *action_name(Action a);

} // namespace uicore
