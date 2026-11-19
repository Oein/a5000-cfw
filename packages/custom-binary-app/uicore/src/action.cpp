#include "uicore/action.h"

namespace uicore {

const char *action_name(Action a) {
    switch (a) {
        case ACT_NAV_UP:       return "NAV_UP";
        case ACT_NAV_DOWN:     return "NAV_DOWN";
        case ACT_NAV_LEFT:     return "NAV_LEFT";
        case ACT_NAV_RIGHT:    return "NAV_RIGHT";
        case ACT_CONFIRM:      return "CONFIRM";
        case ACT_BACK:         return "BACK";
        case ACT_MENU:         return "MENU";
        case ACT_PLAYBACK:     return "PLAYBACK";
        case ACT_SHUTTER_HALF: return "SHUTTER_HALF";
        case ACT_SHUTTER_FULL: return "SHUTTER_FULL";
        case ACT_MOVIE_REC:    return "MOVIE_REC";
        case ACT_DELETE:       return "DELETE";
        case ACT_DIAL_MAIN_CW: return "DIAL_MAIN_CW";
        case ACT_DIAL_MAIN_CCW:return "DIAL_MAIN_CCW";
        case ACT_DIAL_SUB_CW:  return "DIAL_SUB_CW";
        case ACT_DIAL_SUB_CCW: return "DIAL_SUB_CCW";
        case ACT_ZOOM_T:       return "ZOOM_T";
        case ACT_ZOOM_W:       return "ZOOM_W";
        case ACT_FN:           return "FN";
        case ACT_CUSTOM_1:     return "CUSTOM_1";
        case ACT_CUSTOM_2:     return "CUSTOM_2";
        case ACT_CUSTOM_3:     return "CUSTOM_3";
        case ACT_CUSTOM_4:     return "CUSTOM_4";
        case ACT_CUSTOM_5:     return "CUSTOM_5";
        case ACT_TOUCH:        return "TOUCH";
        default:               return "?";
    }
}

} // namespace uicore
