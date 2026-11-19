// a6400 physical-key -> action binding table — STUB / TODO.
//
// The a6400 (ILCE-6400) has a different button layout than the a5000: front + rear
// control dials, a control wheel, C1/C2/C3 custom buttons, AEL, AF-ON, Fn, a 4-way,
// and a touch screen. It has NO SDCom and NO Rigel; per cfw/docs/A6400_PORTABILITY.md
// the UI runs as a RAM mod loaded into im.elf and input comes from im.elf-internal
// hooks (raw codes TBD) rather than event0 scan codes.
//
// To bring the a6400 up:
//   1. Capture the a6400 raw key/touch codes (ivica00 USB Debugger + input hook).
//   2. Fill kBindings below, reusing the SAME uicore Action/Context vocabulary.
//      Put front dial -> ACT_DIAL_MAIN_*, rear dial -> ACT_DIAL_SUB_*, wheel -> NAV
//      in menus / exposure in shooting, C1..C3 -> ACT_CUSTOM_1..3, AEL/AF-ON/Fn/touch.
//   3. Add platform/a6400/{display_*, input_*, camera_* (im.elf hooks), main}.
// The screens and libuicore do NOT change — only this table and the adapters.
#include "uicore/action.h"

namespace {
using namespace uicore;

// TODO(a6400): raw codes are placeholders (0) until captured on hardware/emulator.
const Binding kBindings[] = {
    // { A6400_WHEEL_UP,   CTX_MENU,     ACT_NAV_UP },
    // { A6400_FRONT_DIAL_CW, CTX_SHOOTING, ACT_DIAL_MAIN_CW },
    // { A6400_REAR_DIAL_CW,  CTX_SHOOTING, ACT_DIAL_SUB_CW },
    // { A6400_C1, CTX_GLOBAL, ACT_CUSTOM_1 }, ...
    { 0, CTX_GLOBAL, ACT_NONE },   // placeholder so the table is non-empty
};
}

namespace a6400 {
const uicore::Binding *bindings(int *count) {
    *count = (int)(sizeof(kBindings) / sizeof(kBindings[0]));
    return kBindings;
}
}
