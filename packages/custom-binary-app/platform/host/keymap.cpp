// host simulator physical-key -> action binding table (DATA). Raw codes = ASCII of
// the host keyboard keys you inject with `a5kctl key <ascii>` (or SDL keys later).
// WASD + IJKL navigate, Enter confirms, etc. This proves the core is model-agnostic:
// a different table, same screens.
#include "uicore/action.h"

namespace {
using namespace uicore;

const Binding kBindings[] = {
    { 'i', CTX_GLOBAL, ACT_NAV_UP },    { 'w', CTX_GLOBAL, ACT_NAV_UP },
    { 'k', CTX_GLOBAL, ACT_NAV_DOWN },  { 's', CTX_GLOBAL, ACT_NAV_DOWN },
    { 'j', CTX_GLOBAL, ACT_NAV_LEFT },  { 'a', CTX_GLOBAL, ACT_NAV_LEFT },
    { 'l', CTX_GLOBAL, ACT_NAV_RIGHT }, { 'd', CTX_GLOBAL, ACT_NAV_RIGHT },
    { 13,  CTX_GLOBAL, ACT_CONFIRM },   { ' ', CTX_GLOBAL, ACT_CONFIRM },
    { 'm', CTX_GLOBAL, ACT_MENU },
    { 'p', CTX_GLOBAL, ACT_PLAYBACK },
    { 'r', CTX_GLOBAL, ACT_MOVIE_REC },
    { 'x', CTX_GLOBAL, ACT_DELETE },
    { 'f', CTX_GLOBAL, ACT_FN },
    { 'h', CTX_GLOBAL, ACT_SHUTTER_HALF },   // half press (a5kctl key sends down+up)
    { 'c', CTX_GLOBAL, ACT_SHUTTER_FULL },   // capture
    { 't', CTX_GLOBAL, ACT_ZOOM_T },    { 'g', CTX_GLOBAL, ACT_ZOOM_W },
    { '.', CTX_GLOBAL, ACT_DIAL_MAIN_CW }, { ',', CTX_GLOBAL, ACT_DIAL_MAIN_CCW },
    // menu context: wheel/dial scroll
    { '.', CTX_MENU,   ACT_NAV_DOWN },  { ',', CTX_MENU,   ACT_NAV_UP },
};
}

namespace host {
const uicore::Binding *bindings(int *count) {
    *count = (int)(sizeof(kBindings) / sizeof(kBindings[0]));
    return kBindings;
}
}
