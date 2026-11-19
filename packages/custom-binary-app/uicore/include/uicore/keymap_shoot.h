// Shooting-screen key assignments ("Custom Key"). Maps each customizable shooting KEY
// (4-way + CENTER, C1) to an assignable FUNCTION (quick bar, pickers, exposure targets,
// playback / menu, DISP, AE-lock). This is the user-facing remap layer that sits ABOVE the
// physical raw->Action keymap (uicore/keymap.h): the platform still turns a button into an
// abstract Action (ACT_NAV_UP ...), and the shooting OSD then routes that Action through this
// table to decide what it DOES.
//
// The table is a process-wide singleton shared by the shooting OSD and the Custom Key settings
// screen. Persistence is through a platform-supplied store (a /setting text conf file on the
// a5000, in-memory on host) so uicore stays platform-free: this header never touches files.
#pragma once

namespace uicore {

// Assignable functions a shooting key can trigger. Order is stable (serialized by token, not
// by number), so new values may be appended but existing ones must keep their token.
enum ShootFunc {
    SF_NONE = 0,      // nothing (dead key)
    SF_QUICK_BAR,     // open the quick-access bar
    SF_EXPO_TARGET,   // cycle the main exposure target (shutter <-> aperture)
    SF_ISO_EV,        // toggle the ISO <-> EV wheel target
    SF_ISO,           // set the wheel target directly to ISO
    SF_SHUTTER,       // set the wheel target directly to shutter speed (main axis)
    SF_MODE_PICKER,   // open the exposure / scene mode picker
    SF_DRIVE_PICKER,  // open the drive / self-timer picker
    SF_PLAYBACK,      // open playback / gallery
    SF_MENU,          // open the main menu
    SF_DISP,          // toggle the on-screen info rows (DISP)
    SF_AE_LOCK,       // toggle AE lock
    SF_COUNT
};

// Customizable shooting keys. (MOVIE / zoom lever / shutter stay fixed dedicated buttons.)
enum ShootKey {
    SK_UP = 0, SK_DOWN, SK_LEFT, SK_RIGHT, SK_CENTER, SK_C1,
    SK_COUNT
};

struct ShootKeymap {
    unsigned char f[SK_COUNT];                 // ShootFunc per key
    ShootFunc func(ShootKey k) const { return (ShootFunc)f[k]; }
    void      set(ShootKey k, ShootFunc fn) { f[k] = (unsigned char)fn; }
};

// Labels (Korean, UTF-8) for the settings UI, and stable ASCII tokens for the conf file.
const char *shootFuncLabel(ShootFunc fn);
const char *shootKeyLabel(ShootKey k);
const char *shootFuncToken(ShootFunc fn);
const char *shootKeyToken(ShootKey k);
ShootFunc   shootFuncFromToken(const char *s);     // SF_COUNT if unknown
ShootKey    shootKeyFromToken(const char *s, bool *ok);

void defaultsInto(ShootKeymap *km);                // factory default scheme

// Text conf round-trip. parseConf starts from *km and only overwrites recognised lines, so a
// missing / truncated / garbage file simply leaves the defaults (or whatever was there) intact.
void formatConf(const ShootKeymap &km, char *out, int n);
void parseConf(ShootKeymap *km, const char *text);

// ---- process-wide singleton (shared by the shooting OSD and the Custom Key screen) ----
ShootKeymap &shootKeymap();

// ---- persistence hook (platform-supplied; uicore never opens files) ----
// read() fills buf with the raw conf text (false = no file / unreadable); write() stores it.
class ShootKeymapStore {
public:
    virtual ~ShootKeymapStore() {}
    virtual bool read(char *buf, int n) = 0;
    virtual bool write(const char *buf) = 0;
};

void shootKeymapBindStore(ShootKeymapStore *store);   // 0 = none (host: in-memory default only)
void shootKeymapLoad();    // (re)load the singleton from the bound store; defaults if none / bad
void shootKeymapSave();    // persist the singleton to the bound store; no-op if none

} // namespace uicore
