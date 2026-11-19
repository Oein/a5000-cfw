// Shooting-screen key assignments (see uicore/keymap_shoot.h). Pure libc; no platform includes.
#include "uicore/keymap_shoot.h"
#include <stdio.h>
#include <string.h>

namespace uicore {

namespace {

// Parallel tables: index = enum value. Tokens are the stable conf-file spelling; labels are the
// Korean UI strings (UTF-8 byte escapes so the source stays ASCII, like the rest of the tree).
struct FuncRow { const char *token; const char *label; };
const FuncRow kFunc[SF_COUNT] = {
    { "none",         "\xec\x97\x86\xec\x9d\x8c" },                         // 없음
    { "quick-bar",    "\xed\x80\xb5 \xeb\xb0\x94" },                         // 퀵 바
    { "expo-target",  "\xeb\x85\xb8\xec\xb6\x9c \xeb\x8c\x80\xec\x83\x81(\xec\x85\x94\xed\x84\xb0/\xec\xa1\xb0\xeb\xa6\xac\xea\xb0\x9c)" }, // 노출 대상(셔터/조리개)
    { "iso-ev",       "ISO / EV \xec\xa0\x84\xed\x99\x98" },                 // ISO / EV 전환
    { "iso",          "ISO \xec\xa7\x81\xec\xa0\x91" },                      // ISO 직접
    { "shutter",      "\xec\x85\x94\xed\x84\xb0 \xec\x86\x8d\xeb\x8f\x84" },  // 셔터 속도
    { "mode-picker",  "\xec\xb4\xac\xec\x98\x81 \xeb\xaa\xa8\xeb\x93\x9c" },  // 촬영 모드
    { "drive-picker", "\xeb\x93\x9c\xeb\x9d\xbc\xec\x9d\xb4\xeb\xb8\x8c \xeb\xaa\xa8\xeb\x93\x9c" }, // 드라이브 모드
    { "playback",     "\xec\x9e\xac\xec\x83\x9d" },                          // 재생
    { "menu",         "\xeb\xa9\x94\xeb\x89\xb4" },                          // 메뉴
    { "disp",         "\xed\x99\x94\xeb\xa9\xb4 \xed\x91\x9c\xec\x8b\x9c(DISP)" }, // 화면 표시(DISP)
    { "ae-lock",      "AE \xec\x9e\xa0\xea\xb8\x88" },                       // AE 잠금
};

struct KeyRow { const char *token; const char *label; };
const KeyRow kKey[SK_COUNT] = {
    { "up",     "\xec\x9c\x84\xec\xaa\xbd(UP)" },        // 위쪽(UP)
    { "down",   "\xec\x95\x84\xeb\x9e\x98\xec\xaa\xbd(DOWN)" }, // 아래쪽(DOWN)
    { "left",   "\xec\x99\xbc\xec\xaa\xbd(LEFT)" },      // 왼쪽(LEFT)
    { "right",  "\xec\x98\xa4\xeb\xa5\xb8\xec\xaa\xbd(RIGHT)" }, // 오른쪽(RIGHT)
    { "center", "\xec\xa4\x91\xec\x95\x99(\xed\x99\x95\xec\x9d\xb8)" }, // 중앙(확인)
    { "c1",     "C \xeb\xb2\x84\xed\x8a\xbc(C1)" },      // C 버튼(C1)
};

} // namespace

const char *shootFuncLabel(ShootFunc fn) { return (fn >= 0 && fn < SF_COUNT) ? kFunc[fn].label : kFunc[SF_NONE].label; }
const char *shootKeyLabel (ShootKey k)   { return (k  >= 0 && k  < SK_COUNT) ? kKey[k].label   : "?"; }
const char *shootFuncToken(ShootFunc fn) { return (fn >= 0 && fn < SF_COUNT) ? kFunc[fn].token : kFunc[SF_NONE].token; }
const char *shootKeyToken (ShootKey k)   { return (k  >= 0 && k  < SK_COUNT) ? kKey[k].token   : "?"; }

ShootFunc shootFuncFromToken(const char *s) {
    if (s) for (int i = 0; i < SF_COUNT; i++) if (!strcmp(s, kFunc[i].token)) return (ShootFunc)i;
    return SF_COUNT;
}
ShootKey shootKeyFromToken(const char *s, bool *ok) {
    if (s) for (int i = 0; i < SK_COUNT; i++) if (!strcmp(s, kKey[i].token)) { if (ok) *ok = true; return (ShootKey)i; }
    if (ok) *ok = false;
    return SK_UP;
}

void defaultsInto(ShootKeymap *km) {
    if (!km) return;
    // The shipped scheme: UP = quick bar, DOWN = exposure target, LEFT = drive picker,
    // RIGHT = ISO/EV, CENTER = mode picker, C1 = DISP. (Every exposure target stays reachable:
    // DOWN cycles shutter<->aperture, RIGHT toggles ISO<->EV; pickers/menu/playback unchanged.)
    km->f[SK_UP]     = SF_QUICK_BAR;
    km->f[SK_DOWN]   = SF_EXPO_TARGET;
    km->f[SK_LEFT]   = SF_DRIVE_PICKER;
    km->f[SK_RIGHT]  = SF_ISO_EV;
    km->f[SK_CENTER] = SF_MODE_PICKER;
    km->f[SK_C1]     = SF_DISP;
}

void formatConf(const ShootKeymap &km, char *out, int n) {
    if (!out || n <= 0) return;
    int w = snprintf(out, n, "# a5k shooting custom-key map v1\n# <key> <function>\n");
    for (int i = 0; i < SK_COUNT && w > 0 && w < n; i++)
        w += snprintf(out + w, n - w, "%s %s\n", kKey[i].token, shootFuncToken((ShootFunc)km.f[i]));
}

void parseConf(ShootKeymap *km, const char *text) {
    if (!km || !text) return;
    const char *p = text;
    char line[128];
    while (*p) {
        int i = 0;
        while (*p && *p != '\n' && i < (int)sizeof line - 1) line[i++] = *p++;
        line[i] = 0;
        if (*p == '\n') p++;
        // strip a trailing \r and skip comments / blank lines
        if (i > 0 && line[i - 1] == '\r') line[--i] = 0;
        char ktok[64] = {0}, ftok[64] = {0};
        if (sscanf(line, " %63s %63s", ktok, ftok) != 2) continue;
        if (ktok[0] == '#' || ktok[0] == 0) continue;
        bool ok = false;
        ShootKey  k  = shootKeyFromToken(ktok, &ok);
        ShootFunc fn = shootFuncFromToken(ftok);
        if (ok && fn != SF_COUNT) km->set(k, fn);     // unknown tokens: ignored (keep the default)
    }
}

// ---- singleton + persistence hook ----------------------------------------------------------
namespace {
ShootKeymap       g_km;
bool              g_init = false;
ShootKeymapStore *g_store = 0;
}

ShootKeymap &shootKeymap() {
    if (!g_init) { defaultsInto(&g_km); g_init = true; }
    return g_km;
}

void shootKeymapBindStore(ShootKeymapStore *store) { g_store = store; }

void shootKeymapLoad() {
    ShootKeymap &km = shootKeymap();
    defaultsInto(&km);                         // always start from a known-good default
    if (!g_store) return;
    char buf[1024];
    buf[0] = 0;
    if (g_store->read(buf, sizeof buf)) { buf[sizeof buf - 1] = 0; parseConf(&km, buf); }
}

void shootKeymapSave() {
    if (!g_store) return;
    char buf[1024];
    formatConf(shootKeymap(), buf, sizeof buf);
    g_store->write(buf);
}

} // namespace uicore
