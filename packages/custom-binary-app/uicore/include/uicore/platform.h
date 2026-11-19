// uicore <-> platform boundary. The core (libuicore) talks to the outside world
// ONLY through these interfaces. A platform (a5000 / host / future a6400) provides
// concrete implementations and wires them into the App.
//
// uicore has ZERO platform includes (no libsdcom, rgl*/ws_hal, evdev, Android, osal).
// All of that lives behind these pure-virtual interfaces in the platform adapters.
#pragma once
#include "uicore/types.h"
#include "uicore/action.h"
#include "uicore/camera_state.h"
#include "uicore/apply.h"

namespace uicore {

class Canvas;
class ShootKeymapStore;   // uicore/keymap_shoot.h: persistence for the shooting Custom Key map

// ---- Input ---------------------------------------------------------------
// Adapters emit RAW platform key/touch events; the core maps raw->Action with the
// platform's binding table (KeyMap) under the active screen's context. Control
// events (SHOT/QUIT) come from the ctl channel so one loop drives device and host.
enum EventKind { EV_NONE, EV_KEY, EV_TOUCH, EV_SHOT, EV_QUIT, EV_NFC_TAP };   // EV_NFC_TAP: ctl-simulated phone tap (host/emulator test only)
struct InputEvent {
    EventKind kind;
    int       raw;     // EV_KEY: platform raw scan code
    int       value;   // EV_KEY: 1 down, 0 up, 2 repeat
    int       x, y;    // EV_TOUCH
    char      arg[128];// EV_SHOT: output path
};

class Input {
public:
    virtual ~Input() {}
    virtual bool init() = 0;
    // Block up to timeoutMs, fill out[] with up to max events, return count.
    virtual int  poll(InputEvent *out, int max, int timeoutMs) = 0;
    virtual void shutdown() = 0;
};

// ---- Display -------------------------------------------------------------
// The core owns the RGBA8888 back buffer (Canvas). The adapter copies it to the
// real surface in present(); dirty is a hint (full-frame if 0,0,0,0).
// Rect moved to uicore/types.h (so Canvas can use it without a platform include).
class Display {
public:
    virtual ~Display() {}
    virtual bool init() = 0;
    virtual void present(const Canvas &c, const Rect &dirty) = 0;
    virtual void shutdown() = 0;
    virtual const char *name() const = 0;
};

// ---- CameraControl -------------------------------------------------------
// a5000 implements this with SDCom; a6400 later with im.elf hooks. Settings are
// addressed by catalog id (uicore/catalog.h) with A-side values. All calls are
// blocking and return quickly (the a5000 adapter uses short SDCom syncs); events are
// queued by the adapter and drained with pollEvents() from the UI thread.
// Non-setting menu actions (menu_cascade app-launch / action items). The UI dispatches
// these through CameraControl::systemAction; the platform adapter maps them to the stock
// native path (openView reuse / requestModelExecute / Android ABT world hand-off). Brick-0
// CONTRACT: RAM/mount only -- an executor MUST NEVER write Backup.bin / flash / NAND.
enum SysAction {
    SYS_OPEN_VIEW = 0,   // openView("view/<arg>") reuse: arg = stock view id ("FORMAT","SELECT_FOLDER",...)
    SYS_MODEL_EXEC,      // requestModelExecute toggle (airplane): arg = stock model name
    SYS_LAUNCH_APP       // Android world hand-off + launch: arg = "pkg/.Class" (0 = Sony launcher)
};
enum SysActionResult {
    SA_DONE        =  0, // performed on this platform
    SA_DEVICE_SEAL =  1, // path staged, but the on-device hand-off (ABT seq / openView offset) is unverified
    SA_UNSUPPORTED = -1  // no route on this platform
};

class CameraControl {
public:
    virtual ~CameraControl() {}
    virtual bool connect() = 0;
    virtual void disconnect() = 0;
    virtual bool connected() const = 0;
    virtual bool dryrun() const = 0;
    // M0 read-only device info: fill id/val pairs, return count.
    virtual int  readInfo(int *ids, int *vals, int max) = 0;

    // ---- M1 ----
    // Full snapshot (a5000: OpenCamera-once + 0x1002 + extras). Returns false on error.
    virtual bool readState(CameraState * /*out*/) { return false; }
    // Set one catalog setting. Returns true only if the new value was read back
    // (settings can be silently ignored depending on the exposure mode).
    virtual bool setSetting(int /*catalogId*/, int /*val*/) { return false; }
    // Generic apply for ANY of the 140 menu items (conversion table: uicore/apply.h). `item` is the
    // kMenuItems index, `value` the picked option index (MT_RANGE: the number). Returns an ApplyResult.
    // RAM/runtime only -- never writes Backup/flash. Default (apply.cpp) = catalog bridge only.
    virtual int  applySetting(int item, int value);
    // Relative exposure step command (shutter / aperture / program shift are camera-side
    // shifts and apply asynchronously; steps = signed detent count, +1 = faster shutter /
    // smaller aperture / higher value). EV / ISO are done by the model via setSetting.
    // Returns true if the command was accepted.
    virtual bool exposureStep(int /*axis*/, int /*steps*/) { return false; }
    virtual bool autofocus(bool /*on*/) { return false; }   // half press / release
    virtual bool shutterDown() { return false; }            // full press (direct shutter)
    virtual bool shutterUp()   { return false; }            // full release
    virtual bool takePicture() { return false; }            // single still
    virtual void zoom(int /*dir*/) {}                       // +1 tele / -1 wide, one step
    virtual bool movieRec(bool /*on*/) { return false; }    // start / stop movie recording
    // Drain queued camera events (non-blocking); returns the count written.
    virtual int  pollEvents(CameraEvent * /*out*/, int /*max*/) { return 0; }
    // Run a non-setting menu action (SysAction). Brick-0: RAM/mount only, never flash/Backup/NAND.
    // Default = no route (device-seal). arg may be 0.
    virtual int  systemAction(int /*kind*/, const char * /*arg*/) { return SA_UNSUPPORTED; }
};

// ---- Storage -------------------------------------------------------------
// Preference key/value persistence (and later file enumeration). M0: optional.
class Storage {
public:
    virtual ~Storage() {}
    virtual bool getInt(const char *key, int *out) { (void)key; (void)out; return false; }
    virtual bool setInt(const char *key, int val)  { (void)key; (void)val; return false; }
};

// ---- Media (M3 playback / gallery) ---------------------------------------
// Read access to the card's DCIM tree. Entries are indexed 0..count()-1 in CHRONOLOGICAL order (oldest
// first); the playback screen shows them newest-first. Decoding is the provider's job: load() returns a
// RGBA8888 bitmap no larger than maxW x maxH (aspect preserved) -- a real provider uses a scaled JPEG
// decode (libjpeg scale_denom / EXIF thumbnail) so a 6000x4000 still never fully decodes into RAM.
// The returned pixels are owned by the provider until release(). Everything but the listing/loading
// calls is optional (defaults = unsupported). Host: platform/common/fake_media.cpp (generated patterns).
// a5000 / a5000-inproc: not wired yet (platform.media == 0 -> the screen shows the empty state).
struct MediaEntry {
    char     name[16];      // "DSC00012.JPG"
    char     folder[12];    // "100MSDCF"
    int      folderNo, fileNo;      // DCF 100-0012
    int      w, h;          // original pixel size (0 = unknown)
    unsigned size;          // bytes
    int      year, mon, day, hour, min, sec;
    bool     movie, prot, raw;
    int      durationSec;   // movies
};
struct MediaImage { int w, h; u8 *px; };    // RGBA8888, w*h*4 bytes

class Media {
public:
    virtual ~Media() {}
    virtual int  count() = 0;
    virtual bool info(int i, MediaEntry *out) = 0;
    // thumb = true: grid thumbnail (EXIF/micro thumb is fine); false: full-screen preview.
    virtual bool load(int i, int maxW, int maxH, bool thumb, MediaImage *out) = 0;
    virtual void release(MediaImage *img) = 0;
    // Write ops (gated by Options.allowWrite in the UI). Default: unsupported.
    virtual bool remove(int /*i*/) { return false; }
};

// ---- Wi-Fi ---------------------------------------------------------------
// Own Wi-Fi design (memory wifi-own-design): STA (join a network) and host-AP (smartphone transfer).
// The core only drives this interface; the real a5000 stack (ar6003 / wpa_supplicant-like) is a
// follow-up behind the same calls. Host-sim: platform/common/fake_wifi.cpp. Everything is non-blocking:
// scan / connect / AP start return at once and progress is observed through status().
enum WifiMode  { WM_STA = 0, WM_AP = 1 };
enum WifiState { WS_OFF = 0, WS_IDLE, WS_CONNECTING, WS_CONNECTED, WS_AP_STARTING, WS_AP_WAITING, WS_AP_CLIENT };
enum WifiError { WE_NONE = 0, WE_AUTH, WE_NOT_FOUND, WE_TIMEOUT, WE_FAIL };
struct WifiNetwork { char ssid[33]; int signal; bool secured; bool saved; };    // signal 0..100
struct WifiStatusInfo {
    int  state;          // WifiState
    int  mode;           // WifiMode
    char ssid[33];       // CONNECTING/CONNECTED: target network; AP_*: our AP name
    int  signal;         // CONNECTED: 0..100
    char ip[16];         // CONNECTED: our address; AP_*: the AP gateway the phone talks to
    int  clients;        // AP_CLIENT: connected phones
    int  lastError;      // WifiError of the last failed connect (sticky until the next connect)
    int  errSeq;         // bumped on every failure (UI toasts once per change)
};
class Wifi {
public:
    virtual ~Wifi() {}
    virtual bool enabled() = 0;
    virtual void setEnabled(bool on) = 0;           // off = radio down (drops link / AP)
    virtual int  mode() = 0;
    virtual void setMode(int mode) = 0;             // WifiMode (drops the current link / AP)
    // STA
    virtual bool beginScan() = 0;
    virtual bool scanning() = 0;
    virtual int  scanResults(WifiNetwork *out, int max) = 0;     // strongest first; `saved` filled in
    virtual bool connect(const char *ssid, const char *password) = 0;   // password 0 = use the saved one; "" = open
    virtual void disconnect() = 0;
    virtual int  savedCount() = 0;
    virtual bool savedAt(int i, char ssid[33], bool *secured) = 0;
    virtual bool forget(const char *ssid) = 0;
    // AP (smartphone)
    virtual bool startAp(const char *ssid, const char *password) = 0;
    virtual void stopAp() = 0;
    virtual void status(WifiStatusInfo *out) = 0;
};

// ---- NFC (one-touch connect) ---------------------------------------------
// The a5000 NFC chip is a FeliCa-Plug-class dynamic tag (emulator/docs/RE_NFC.md): the camera answers the
// phone's reads with an NDEF image {application/x-sony-pmm: SSID + passphrase, AAR PlayMemories Mobile}.
// So a "tap" = the phone read OUR Wi-Fi AP credentials and wants to join: the core's job is to bring the
// AP up with exactly those credentials (setPayload keeps tag content == AP creds). Non-blocking.
// Host-sim: platform/common/fake_nfc.cpp. Real chip (nfc.ko / libInfraNfc handover event) = follow-up.
enum NfcState { NFS_OFF = 0, NFS_IDLE, NFS_TAG, NFS_PAIRING };   // off / waiting for a phone / phone on the antenna (reading) / handover done, waiting for the app
enum NfcError { NE_NONE = 0, NE_READ };                           // tag read aborted (phone pulled away)
struct NfcStatusInfo {
    int state;       // NfcState
    int tapSeq;      // bumped when a phone reaches the antenna (UI opens the one-touch screen once per change)
    int lastError;   // NfcError of the last aborted tap (sticky until the next tap)
    int errSeq;      // bumped on every aborted tap
};
struct NfcTap { char ssid[33]; char password[64]; };   // what the phone read (= the AP the camera must run)
class Nfc {
public:
    virtual ~Nfc() {}
    virtual bool enabled() = 0;
    virtual void setEnabled(bool on) = 0;
    virtual void status(NfcStatusInfo *out) = 0;
    // Tag content: the AP credentials the phone will read. Call before a tap (and when they change).
    virtual void setPayload(const char *ssid, const char *password) = 0;
    // A completed read (handover). Returns true once per tap; state is NFS_PAIRING until finishPairing().
    virtual bool pollTap(NfcTap *out) = 0;
    virtual void finishPairing(bool ok) = 0;
    // Test hook (host fake / emulator ctl): behave as if a phone was tapped. Real hardware: no-op.
    virtual bool injectTap() { return false; }
};

// ---- Clock ---------------------------------------------------------------
class Clock {
public:
    virtual ~Clock() {}
    virtual long nowMs() = 0;
};

// ---- Log -----------------------------------------------------------------
class Log {
public:
    virtual ~Log() {}
    virtual void line(const char *s) = 0;
};

// Bundle handed to App::init. Any pointer except display/input/clock may be 0.
// bindings/bindingCount are the platform's physical-key -> action table (data).
struct Platform {
    Display        *display;
    Input          *input;
    CameraControl  *camera;
    Storage        *storage;
    Clock          *clock;
    Log            *log;
    const Binding  *bindings;
    int             bindingCount;
    Media          *media;          // M3: card media (0 = none -> empty gallery)
    Wifi           *wifi;           // Wi-Fi (0 = none -> the Wi-Fi screen shows 'unavailable')
    Nfc            *nfc;            // NFC one-touch (0 = none -> the NFC screen shows 'unavailable')
    ShootKeymapStore *keymapStore;  // shooting Custom Key persistence (0 = none -> in-memory defaults)
    // Stock-shooting passthrough hook (route-b): on(true) = hand the panel + input to the
    // STOCK camera (live-view/OSD is Sony's own; our overlay steps back), on(false) = engage
    // OUR UI (menu). The app calls it when the screen stack enters/leaves the shooting root,
    // so the shooting screen is Sony's stock OSD and only our MENU is custom. 0 = host-sim
    // (no passthrough; our UI draws everything, as before).
    void (*setStockShooting)(bool on);
    Platform() : display(0), input(0), camera(0), storage(0), clock(0), log(0), bindings(0), bindingCount(0), media(0), wifi(0), nfc(0), keymapStore(0), setStockShooting(0) {}
};

} // namespace uicore
