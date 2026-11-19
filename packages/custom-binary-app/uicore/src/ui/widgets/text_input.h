// Text-input widget (on-screen keyboard replacement) -- NATIVE_PORT_PLAN.md section 9.
// Design: NOT a copy of the stock 2D grid keyboard. The a5000 has a control wheel (1D), so the
// default backend is a 1D character strip scrubbed by the wheel + an assist row (presets / recent /
// autocomplete / generated passwords) + live validation + password masking.
//
// Caller-facing interface is backend-neutral:  editText(app, field, constraints, callback, user).
// A future a6400 touch-QWERTY backend (TB_TOUCH_QWERTY) plugs in behind editText() without changing
// any caller. Pure uicore core: no platform headers; actions only.
//
// Lifetime rules: all `const char *` inside TextConstraints/TextField must be static (the widget
// keeps a copy of the structs, not of the strings it points to).
#pragma once
#include "uicore/action.h"

namespace uicore {

class App;
class Screen;

enum TextGroups    { TG_LOWER = 1, TG_UPPER = 2, TG_DIGIT = 4, TG_SYMBOL = 8, TG_ALL = 15 };
enum TextValidator { TV_NONE = 0, TV_NAME, TV_SSID, TV_WIFI_PSK };
enum TextBackend   { TB_AUTO = 0, TB_WHEEL_STRIP, TB_TOUCH_QWERTY };   // TOUCH = a6400 (stub: falls back)

struct TextConstraints {
    int  minLen, maxLen;              // bytes (ASCII only for now; Hangul composer = future)
    unsigned groups;                  // TextGroups mask of selectable character groups
    bool secret;                      // mask with bullets + show/hide toggle
    bool allowSpace;
    bool autoCap;                     // auto-shift to ABC at word start
    bool suggestGenerated;            // offer generated strong passwords when the field is empty
    int  validator;                   // TextValidator
    const char *prompt;               // one-line description under the title (may be 0)
    const char *const *presets; int nPresets;   // quick picks / autocomplete source
    const char *const *recent;  int nRecent;    // recent values (never for secrets)
    int  backend;                     // TextBackend
    unsigned seed;                    // password generator seed (platform supplies entropy; 0 = fixed)
};

struct TextField { const char *id; const char *title; const char *initial; };

struct TextEditRequest;
// Called on commit (committed=true, text valid) or cancel. Return true to chain: fill `next` with the
// following request and the widget re-targets itself (Wi-Fi SSID -> password); false closes it.
typedef bool (*TextEditDone)(void *user, bool committed, const char *text, TextEditRequest *next);

struct TextEditRequest {
    TextField field; TextConstraints c;
    TextEditDone cb; void *user;
};

// Open the editor on top of the screen stack (backend picked from c.backend / platform capability).
void editText(App &app, const TextField &f, const TextConstraints &c, TextEditDone cb, void *user);

// ---- validation (shared with the future touch backend) ----
struct TextValidity { int level; char msg[72]; int strength; };   // level 0 ok, 1 warn, 2 invalid; strength 0..3 (secrets)
void textValidate(const TextConstraints &c, const char *text, TextValidity *out);

// ---- TEXT catalog fields (data-driven; the menu looks items up by key+label) ----
struct TextFieldDef {
    const char *id;            // store id
    const char *menuKey;       // MenuItem.key that opens it (0 = chained only)
    const char *menuLabel;     // MenuItem.label ("" matches any)
    TextField field;
    TextConstraints c;
    const char *defVal;        // host-sim default
    const char *route;         // apply route note (no device write in this track)
    int next;                  // chained field index or -1
    int summaryOf;             // field index whose value the menu row shows (self = -1)
};
const TextFieldDef *findTextField(const char *menuKey, const char *menuLabel);
void openTextField(App &app, const TextFieldDef &d);            // edit + store + log apply route
void textFieldSummary(const TextFieldDef &d, char *out, int n); // row value (secrets masked)
const char *textFieldValue(const char *id);                     // current RAM value or 0

} // namespace uicore
