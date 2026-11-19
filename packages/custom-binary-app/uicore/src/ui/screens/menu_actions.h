// Menu APP-LAUNCH + ACTION items (route-b). The cascade menu mirrors the stock 6-tab menu;
// its non-setting rows (Android app launch + stock actions like format / folder / device name)
// are matched here by catalog (key,label) -- exactly like the nfc/wifi rows -- and routed to the
// stock native path through CameraControl::systemAction (openView reuse / requestModelExecute /
// Android ABT hand-off). Spec + the 43-item map: re/menu/app_launch_and_actions.md.
//
// Brick-0 (CLAUDE.md s1): destructive actions (format, reset/initialize, image-DB recover,
// playback delete) are EXPERIMENTAL-gated -- never auto-run; they need both an explicit confirm
// AND menuActionGateEnabled(). Safe actions trigger directly. NOTHING here writes Backup/flash.
#pragma once

namespace uicore {

class App;

enum MenuActKind {
    MAK_APP_LAUNCH = 0,   // Android app / Sony launcher (ABT world hand-off)
    MAK_OPEN_VIEW,        // safe stock view reuse (openView)
    MAK_MODEL_TOGGLE,     // safe stock requestModelExecute toggle (airplane)
    MAK_DESTRUCTIVE       // gated: confirm modal + EXPERIMENTAL gate required
};

struct MenuActionDesc {
    int         kind;        // MenuActKind
    int         sysAction;   // SysAction (platform.h) to dispatch
    const char *arg;         // openView id / android component / model name (may be 0)
    const char *title;       // modal / toast title (UTF-8)
    const char *confirm;     // MAK_DESTRUCTIVE: confirm-body line (UTF-8)
};

// Menu hook: does this (key,label) row map to an app-launch / action? Fills *out. (key,label)
// because several catalog rows share a key (info-na-recfolder, pb-view, ...), as nfc/wifi do.
bool menuActionEntry(const char *key, const char *label, MenuActionDesc *out);

// EXPERIMENTAL gate (mirror of Track A's EXPERIMENTAL_ENABLED, inert when empty): true only when
// env A5K_EXPERIMENTAL is set non-empty OR the marker /android/a5k/experimental exists. Destructive
// actions run ONLY when this is true AND the user confirmed. Safe actions ignore it.
bool menuActionGateEnabled();

// Dispatch a matched action through app.camera()->systemAction. Writes a short UTF-8 result line
// into toastOut (<=n). Returns the SysActionResult (SA_DONE / SA_DEVICE_SEAL / SA_UNSUPPORTED).
// For MAK_DESTRUCTIVE the caller MUST have shown the confirm modal and checked the gate first.
int runMenuAction(App &app, const MenuActionDesc &d, char *toastOut, int n);

} // namespace uicore
