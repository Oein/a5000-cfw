// a5000 in-process Input adapter — ENGINE input path (route (b), TRACKB_INPROCESS §21).
//
// In Diadem (the native shooting UI) event0 (the uinput "D keyboard") is never fed:
// snd_InfraInputEvent_to_InputServant (libObj 0x37F2D4) forwards to InputServant only
// in app mode. Native keys arrive as view Events via the single confluence point
//     InputService::createEvent(this, id, type)   (libObj 0x37F854)
// We REIMPLEMENT createEvent in my_createEvent (record the id + do the same new+ctor)
// and patch its entry with a non-destructive absolute Thumb jump (LDR.W PC,[PC,#0]).
// Stock input keeps working; we observe every input id and feed it to uicore.
//
// id encoding: id = 0x15010000 | (code<<4) | edge. code: CENTER 0x06, UP 0x07,
// DOWN 0x08, RIGHT 0x09, LEFT 0x0a, DELETE 0x10, MENU 0x11. We translate each code to
// the a5000 ISV raw scan code so the existing a5000 keymap (platform/a5000/keymap.cpp)
// maps raw -> abstract action unchanged.
//
// EDGE polarity (re-measured in the emulator manual-inject repro, 2026-11-16): the
// PHYSICAL key-down carries edge==2 and the key-UP edge==1 -- the OPPOSITE of the §21.2
// note. Proof: with `sendkey <k> 400` the edge==2 id is recorded at the START of the
// 400 ms hold (press) and the edge==1 id at its END (release). uicore expects value=1 on
// key-DOWN (press) and value=0 on key-up, so every screen does `if (value==0) return` to
// act once per press. The old decode `value=(edge==1)?1:0` therefore delivered our action
// on the RELEASE edge and DROPPED the press. That is why a normal device tap never drove
// our UI while the stock path (which acts on press) did -- "keys go to stock". The §22
// demo only worked because a deliberate 400 ms HOLD also delivered the release edge.
// Fixed below: key-DOWN (edge!=1, i.e. 2) -> value 1.
#include "uicore/platform.h"
#include "uicore/log.h"
#include "engine.h"
#include <string.h>
#include <sys/mman.h>
#include <time.h>
#include <unistd.h>
#include <fcntl.h>

using namespace uicore;

// HAND-TO-STOCK (display_rigel.cpp): while a stock view owns the panel we release
// input to the stock engine. handToStock_active() gates our key consume; _poll()
// (called every frame below) decides when the stock view closed and we resume.
namespace a5kinproc { bool handToStock_active(); void handToStock_poll(); bool stockShooting_active(); }

// evdev (/dev/input/event0) record. We define it locally rather than include
// <linux/input.h> because that header's EV_KEY macro clashes with uicore's
// EV_KEY enum. Layout on the 32-bit ARM guest: timeval(2*long) + u16 type +
// u16 code + s32 value = 16 bytes (matches struct input_event).
namespace { struct Ev0Rec { long sec, usec; unsigned short type, code; int value; };
            enum { EVDEV_EV_KEY = 1 }; }

namespace {

// libObj offsets (Thumb FUNCs)
enum { OFF_createEvent = 0x37F854, OFF_Event_ctor = 0x7042D4 };

// Native-mode CONTROL-WHEEL ROTATION hook. In native Diadem mode the dial reaches
// neither evdev nor createEvent (real-device confirmed: dial-only -> 0 events on both).
// The dial driver callback that IS invoked, view-mode-independent, is
//   InfraDial::eventReceiveCallback = sub_710370 @ libObj 0x710370 (OSAL queue-cb for
//   the dial queue 0x11B02AA). a1 = InfraDialEventDataRecord {local_key_code,
//   degree_per_sec(SIGNED), ex_info}; direction = sign of a1[1] (>=0 CW -> 525, <0 CCW
//   -> 526). Its own epilogue is `return osal_free_msg(18547370)`, so our detour MUST
//   free that msg (else the OSAL pool leaks and the engine starves). We CONSUME (do not
//   run the original), which is safe: at entry no semaphore is held and no InfraDial
//   state is mutated yet, and the av-cam side owns the delta math. (RE: subagent trace
//   + bytes; sub_710370 prologue e92d4ff0/b08f/4bb1 confirmed.)
enum { OFF_InfraDialRecv  = 0x710370,    // InfraDial::eventReceiveCallback (Thumb)
       OFF_osal_free_msg  = 0x0E12D4,    // j_osal_free_msg veneer (BX pc -> ARM plt); Thumb-callable
       DIAL_FREE_MSG_ID   = 18547370 };  // 0x11B02AA dial queue id, freed in the epilogue

// --- SPSC ring: producer = my_createEvent (engine thread), consumer = poll() -----
volatile unsigned g_ring[512];
volatile unsigned g_head;        // producer cursor
unsigned          g_tail;        // consumer cursor (poll thread only)

// resolved addresses / patch state
unsigned long g_evctor;          // (libObj + OFF_Event_ctor) | 1
unsigned long g_target;          // createEvent entry
unsigned char g_save[8];
int           g_patched;

// --- native dial hook state (sub_710370) ----------------------------------------
unsigned long g_dial_libobj;             // libObj base (for osal_free_msg)
unsigned long g_dial_target;             // sub_710370 entry
unsigned char g_dial_save[8];
int           g_dial_patched;
volatile unsigned g_dial_cw, g_dial_ccw; // producer (hook) counters (one per DETENT)
unsigned      g_dial_cw_seen, g_dial_ccw_seen;   // consumer (poll) cursors
volatile long g_dial_emit_ms;            // producer: time of the last counted detent
volatile int  g_dial_last_cw = -1;       // producer: sign of the last emitted detent (-1 = none)
int           g_dial_invert = 0;         // A5K_DIAL_INVERT: swap CW/CCW (differently-wired body)
// DEVICE-PROVEN direction decode (2026-11-18, a5000 @172.16.1.151, raw a1[] capture; see
// re/input/dial_quickmenu_full.md). The InfraDial msg is {local_key_code, degree_per_sec
// (SIGNED), ex_info}. a1[0] is CONSTANT (always 22) -- it does NOT encode direction, so the
// earlier a1[0]-binding theory was wrong (it bound 22->CW once and then read every turn,
// including CCW, as CW). The SIGN of a1[1] IS the direction and never flips spuriously: CW
// emits only {+n,0} samples, CCW only {-n,0}. The lone noise is a1[1]==0 (detent zero-
// crossing, no direction) -- the old `deg>=0?CW:CCW` miscounted zeros as CW and leaked a
// phantom CW out of every CCW click (the "CCW read as CW" reversal bug). my_dialRecv now
// ignores a1[1]==0 and takes direction = sign(a1[1]). Debounce: dedup SAME-direction repeats
// within DIAL_DETENT_MS (collapses the same-ms double samples on a fast spin while keeping
// ~1 step per physical detent); a reversal registers immediately (the sign is trustworthy).
// Tunable: raise DIAL_DETENT_MS if a single detent double-steps / lower for faster scroll.
static const long DIAL_DETENT_MS = 50;

// --- input OWNERSHIP + fail-safe (TRACKB §21, input-consume) ---------------------
// When our UI is alive it OWNS the key events: my_createEvent returns NULL for the
// keys we handle, and every stock createEvent caller null-checks the return and then
// simply drops the event (verified in libObj.so.c: `if (Event != nullptr) { addParameter;
// post; }`), so the stock views never see the key -> our UI, not stock, acts on it.
// Fail-safe: poll() (our worker loop) refreshes g_hb_ms every frame. If the worker
// hangs or dies, g_hb_ms goes stale and consume AUTO-DISABLES, so keys flow back to
// the stock camera -> the device never goes input-dead (brick-safe). Non-key events
// (touch/dial/internal 0x1700xxxx/0x1101xxxx) ALWAYS pass through untouched.
volatile int  g_own   = 0;       // our UI claims key input (set by poll())
volatile long g_hb_ms = 0;       // worker heartbeat (monotonic ms), refreshed each frame
static const long HB_STALE_MS = 2000;   // fail-safe: consume only while heartbeat fresh

static long mono_ms() {
    struct timespec t; clock_gettime(CLOCK_MONOTONIC, &t);
    return (long)t.tv_sec * 1000 + t.tv_nsec / 1000000;
}

static int codeToIsv(unsigned code);     // fwd: "do we handle this key?" gate for consume

typedef void (*evctor_fn)(void *, unsigned int, unsigned char, unsigned char);

// Our replacement for InputService::createEvent(this, id, type). For a key event
// (0x1501xxxx) we (1) record the id for our poll(), and (2) if our UI owns input and
// it is a key we handle, return NULL to CONSUME it (stock drops it). Everything else
// (and keys while our UI is not alive -- fail-safe) is reconstructed exactly like the
// stock function (new 0x10 + Event ctor; the glibc heap is shared, §21 proven) so the
// engine/touch/dial path is never disturbed.
__attribute__((used))
static void *my_createEvent(void *self, unsigned int id, unsigned char type) {
    (void)self;
    if ((id & 0xFFFF0000u) == 0x15010000u) {        // key namespace
        g_ring[g_head++ & 511] = id;                // observe (lock-free SPSC)
        unsigned code = (id >> 4) & 0xFFFu;
        // Consume only the buttons we handle (the control-wheel rotation does not come
        // through createEvent -- it is read from event0, which we do not need to gate).
        if (g_own && codeToIsv(code) >= 0 && (mono_ms() - g_hb_ms) < HB_STALE_MS)
            return 0;                               // CONSUME: caller null-checks -> stock drops it
        // Stock-shooting passthrough: input belongs to the stock camera (g_own=0), BUT we still
        // peek the MENU button so our UI can open. Consume only MENU (so the stock menu does not
        // also open); the id was recorded to the ring above, so poll() delivers it to our app.
        if (!g_own && code == 0x11u && a5kinproc::stockShooting_active())
            return 0;
    }
    void *e = malloc(0x10);                   // pass-through: reimplement the stock function
    ((evctor_fn)g_evctor)(e, id, type, 1);    // Event::Event(e, id, type, 1)
    return e;
}

static bool patchCreateEvent(unsigned long libObj) {
    g_evctor = (libObj + OFF_Event_ctor) | 1UL;
    g_target = libObj + OFF_createEvent;
    unsigned long page = g_target & ~0xfffUL;
    if (mprotect((void *)page, 0x2000, PROT_READ | PROT_WRITE | PROT_EXEC)) return false;
    memcpy(g_save, (void *)g_target, 8);
    unsigned char *t = (unsigned char *)g_target;
    t[0]=0xDF; t[1]=0xF8; t[2]=0x00; t[3]=0xF0;          // LDR.W PC,[PC,#0]
    unsigned long tgt = ((unsigned long)&my_createEvent) | 1UL;
    memcpy(t + 4, &tgt, 4);
    __builtin___clear_cache((char *)g_target, (char *)g_target + 8);
    mprotect((void *)page, 0x2000, PROT_READ | PROT_EXEC);
    g_patched = 1;
    return true;
}

static void unpatchCreateEvent() {
    if (!g_patched) return;
    unsigned long page = g_target & ~0xfffUL;
    if (mprotect((void *)page, 0x2000, PROT_READ | PROT_WRITE | PROT_EXEC) == 0) {
        memcpy((void *)g_target, g_save, 8);
        __builtin___clear_cache((char *)g_target, (char *)g_target + 8);
        mprotect((void *)page, 0x2000, PROT_READ | PROT_EXEC);
        g_patched = 0;
    }
}

// ---- native dial hook (sub_710370) ----------------------------------------------
// Fail-safe teardown is reentrant: unpatchDialHook() can be called CONCURRENTLY by the
// worker thread (CreateEventInput::shutdown) AND the OSAL dispatcher thread (my_dialRecv
// self-heal when the heartbeat goes stale). Two un-serialized callers racing the
// mprotect(RWX)->memcpy->mprotect(RX) sequence can let one thread flip the page back to
// R-X while the other is mid-memcpy -> SIGSEGV -> engine watchdog reboot (a teardown
// crash). CAS the patched flag so exactly ONE thread performs the restore; the loser
// returns immediately. (Mirrors the g_vh.lock serialization the VDF hook already uses.)
static void unpatchDialHook() {
    if (!__sync_bool_compare_and_swap(&g_dial_patched, 1, 0)) return;   // single executor
    unsigned long page = g_dial_target & ~0xfffUL;
    if (mprotect((void *)page, 0x2000, PROT_READ | PROT_WRITE | PROT_EXEC) == 0) {
        memcpy((void *)g_dial_target, g_dial_save, 8);
        __builtin___clear_cache((char *)g_dial_target, (char *)g_dial_target + 8);
        mprotect((void *)page, 0x2000, PROT_READ | PROT_EXEC);
    } else {
        g_dial_patched = 1;   // mprotect failed (unreachable in practice): allow a retry
    }
}

// Our detour for InfraDial::eventReceiveCallback. CONSUME: record the direction, free
// the dial msg (the stock epilogue), return 0 -- the stock engine never sees the dial,
// so there is no double-step in shooting mode. Fail-safe: if our UI is not alive, we
// self-heal (restore the stock callback so the dial works normally again) and still
// free this msg. Runs on the OSAL dispatcher thread -> minimal work, no logging here.
__attribute__((used))
static int my_dialRecv(int *a1, int a2) {
    (void)a2;
    typedef int (*free_fn)(unsigned long);
    free_fn freemsg = (free_fn)((g_dial_libobj + OFF_osal_free_msg) | 1UL);
    if (!g_own || (mono_ms() - g_hb_ms) >= HB_STALE_MS) {
        unpatchDialHook();                       // worker gone -> hand the dial back to stock
        return freemsg(DIAL_FREE_MSG_ID);
    }
    if (a1) {
        long now = mono_ms();
        // DEVICE-PROVEN (2026-11-18, a5000 @172.16.1.151 raw a1[] capture): a1[0] is a
        // CONSTANT dial key code (always 22) -- it is NOT the direction. The signed velocity
        // a1[1] (degree/sec) IS the direction and its sign NEVER flips spuriously: a CW turn
        // emits only {+n, 0} samples, a CCW turn only {-n, 0}. The sole noise is a1[1]==0 (the
        // detent zero-crossing, no direction). The old `deg>=0?CW:CCW` counted those zeros as
        // CW, leaking a phantom CW out of every CCW click (the "CCW read as CW" reversal bug);
        // the later a1[0]-binding made it total (a1[0] is constant -> everything bound to CW).
        // Fix: ignore a1[1]==0; direction = sign(a1[1]); dedup same-direction repeats within
        // DIAL_DETENT_MS; a reversal registers immediately (the sign is fully trustworthy).
        // env A5K_DIAL_INVERT=1 swaps CW/CCW for a differently-wired body (a6400 portability).
        int deg = a1[1];
        if (deg != 0) {
            int cw = (deg > 0) ? 1 : 0;
            if (g_dial_invert) cw = !cw;
            bool emit = (cw != g_dial_last_cw) || (now - g_dial_emit_ms) >= DIAL_DETENT_MS;
            if (emit) {
                if (cw) g_dial_cw++; else g_dial_ccw++;
                g_dial_emit_ms = now;
                g_dial_last_cw = cw;
            }
        }
    }
    return freemsg(DIAL_FREE_MSG_ID);            // CONSUME: stock never sees the dial
}

static bool patchDialHook(unsigned long libObj) {
    const char *inv = getenv("A5K_DIAL_INVERT");
    if (inv && *inv) g_dial_invert = (int)strtol(inv, 0, 0);
    g_dial_libobj = libObj;
    g_dial_target = libObj + OFF_InfraDialRecv;
    unsigned long page = g_dial_target & ~0xfffUL;
    if (mprotect((void *)page, 0x2000, PROT_READ | PROT_WRITE | PROT_EXEC)) return false;
    memcpy(g_dial_save, (void *)g_dial_target, 8);
    unsigned char *t = (unsigned char *)g_dial_target;
    t[0]=0xDF; t[1]=0xF8; t[2]=0x00; t[3]=0xF0;          // LDR.W PC,[PC,#0]
    unsigned long tgt = ((unsigned long)&my_dialRecv) | 1UL;
    memcpy(t + 4, &tgt, 4);
    __builtin___clear_cache((char *)g_dial_target, (char *)g_dial_target + 8);
    mprotect((void *)page, 0x2000, PROT_READ | PROT_EXEC);
    g_dial_patched = 1;
    return true;
}

// createEvent code -> a5000 ISV raw scan code (platform/a5000/keymap.cpp bindings).
//
// code = the createEvent id code field (id = 0x15010000|(code<<4)|edge). Ground truth
// is the firmware's own key->event table (viewUnified2.so @0x46A2F4, {hw_key, event_id})
// cross-decoded with libObj's key-name table (@0xEE1078). HW-confirmed by the real-device
// log: center=0x06, MENU=0x11. Directions are 0x07/0x08/0x09/0x0a (KEY_TACT_UP/DOWN/
// RIGHT/LEFT) -- the firmware table, matching this map's original values.
//
// IMPORTANT (real-device log interpretation): the "burst" of 0x22/0x23/0x25/0x2b/0x02
// that looked like unmapped directions is NOT directions -- 0x22=TELE(zoom), 0x23=WIDE
// (zoom), and 0x02/0x25/0x2b are the SAME physical key, shutter half-press S1 (hw 0x1F1),
// fanned out to three view categories. The camera boots with the shutter electrically
// held (RE_ENGINE_TICK §12), so S1 is sprayed. We deliberately DO NOT map those: they
// return -1 so they are neither fed to our UI nor consumed -- they pass through to the
// stock engine (which needs S1/zoom for AF/capture). Only the primary code per UI action
// is mapped (no fan-out double-dispatch).
// Control-wheel ROTATION reuses the a5000 keymap's dial scan codes (keymap.cpp:
// CTX_MENU CW->NAV_DOWN / CCW->NAV_UP, CTX_SHOOTING -> exposure step).
enum { ISV_DIAL_CW = 525, ISV_DIAL_CCW = 526 };

static int codeToIsv(unsigned code) {
    switch (code) {
        case 0x06: return 232;   // ENTER / center  -> ACT_CONFIRM   [HW-confirmed]
        case 0x07: return 103;   // UP              -> ACT_NAV_UP     (KEY_TACT_UP)
        case 0x08: return 108;   // DOWN            -> ACT_NAV_DOWN   (KEY_TACT_DOWN; alt 0x3d NOT mapped -> no double)
        case 0x09: return 106;   // RIGHT           -> ACT_NAV_RIGHT  (KEY_TACT_RIGHT)
        case 0x0a: return 105;   // LEFT            -> ACT_NAV_LEFT   (KEY_TACT_LEFT)
        case 0x0f: return 207;   // LANC playback   -> ACT_PLAYBACK
        case 0x10: return 595;   // DELETE / trash  -> ACT_DELETE
        case 0x11: return 514;   // MENU            -> ACT_MENU       [HW-confirmed]
        default:   return -1;    // shutter 0x02/0x25/0x2b, zoom 0x22/0x23, idle etc. -> pass to stock
    }
}

class CreateEventInput : public Input {
public:
    explicit CreateEventInput(a5kinproc::Engine *eng) : eng_(eng), ev0fd_(-1) {}

    bool init() {
        if (!eng_ || !eng_->ok()) { logf("input: engine not resolved -> keys inert"); return true; }
        bool ok = patchCreateEvent(eng_->libObj());
        g_tail = g_head;                 // drop any pre-hook backlog
        logf("input: createEvent hook @libObj+0x37F854 %s", ok ? "installed" : "FAILED");
        // Control-wheel ROTATION does NOT come through createEvent on the real
        // device (confirmed: dial-only rotation -> 0 createEvent events). It arrives
        // on the virtual evdev /dev/input/event0 as EV_KEY 525 (CW) / 526 (CCW)
        // (CLAUDE.md §5). event0 is the uinput "D keyboard" im.elf creates; it is not
        // grabbed, so an extra in-process reader is safe and needs no ABT (we are
        // already inside im.elf, a front-view owner). Keys stay on the createEvent
        // hook; event0 is read ONLY for the dial (so buttons are not double-counted).
        ev0fd_ = ::open("/dev/input/event0", O_RDONLY | O_NONBLOCK);
        logf("input: /dev/input/event0 %s (app-mode dial reader)", ev0fd_ >= 0 ? "opened" : "FAILED");
        // Native-mode control-wheel ROTATION: hook InfraDial::eventReceiveCallback
        // (sub_710370), the view-mode-independent dial driver callback. This is THE
        // native dial path (evdev/createEvent get nothing in native mode).
        bool dok = patchDialHook(eng_->libObj());
        logf("input: InfraDial hook @libObj+0x710370 %s (native dial)", dok ? "installed" : "FAILED");
        return true;
    }

    int poll(InputEvent *out, int max, int timeoutMs) {
        // Heartbeat + ownership: this runs once per app.run() frame on the worker.
        // Refreshing g_hb_ms here is the fail-safe -- if the loop stops, consume
        // self-disables and keys return to the stock camera.
        g_hb_ms = mono_ms();
        // Hand-to-stock: while a stock view (openView) owns the panel, RELEASE input
        // so createEvent + InfraDial pass keys to stock; else own it (hook live).
        if (a5kinproc::handToStock_active()) g_own = 0;
        else if (g_patched) g_own = 1;            // own input only once the hook is live
        // No-reboot teardown: `touch /android/a5k/stop` makes the worker quit
        // cleanly (app.run breaks -> worker restores the stock gate + unpatches),
        // so a new a5kinproc.so can be injected without a reboot. Release ownership
        // immediately so keys reach the stock camera during teardown.
        if (max > 0 && access("/android/a5k/stop", F_OK) == 0) {
            g_own = 0;
            out[0].kind = EV_QUIT; out[0].value = 0; out[0].arg[0] = 0;
            return 1;
        }
        // Hand-to-stock: our overlay is stepped back and keys belong to the stock
        // view. Run the resume poll (view-closed / timeout -> re-own next frame), and
        // DISCARD any key/dial ids we still observed (my_createEvent records the id
        // into the ring even when it passes the event through) so our UI does NOT
        // double-act on a key the stock view is handling. Sleep out the frame so the
        // App loop keeps cadence without busy-spinning.
        if (a5kinproc::handToStock_active()) {
            a5kinproc::handToStock_poll();         // may clear active (resume)
            int n = 0;
            // Stock-shooting passthrough: deliver the peeked MENU press (our wake key) to the app
            // so it can open our custom menu; drop everything else (the stock camera owns it).
            if (a5kinproc::stockShooting_active() && max > 0) {
                unsigned h = g_head;
                while (g_tail != h) {
                    unsigned id = g_ring[g_tail & 511]; g_tail++;
                    if ((id & 0xFFFF0000u) != 0x15010000u) continue;
                    unsigned code = (id >> 4) & 0xFFFu, edge = id & 0xFu;
                    if (code == 0x11u && edge != 1u) {            // MENU press (edge 2); ignore release (1)
                        out[n].kind = EV_KEY; out[n].raw = 514; out[n].value = 1; out[n].arg[0] = 0;
                        if (++n >= max) break;
                    }
                }
            }
            g_tail = g_head;                       // drop remaining observed keys (stock owns them)
            g_dial_cw_seen = g_dial_cw;            // drop native-dial detents
            g_dial_ccw_seen = g_dial_ccw;
            if (ev0fd_ >= 0) { Ev0Rec ev; while (::read(ev0fd_, &ev, sizeof ev) == (ssize_t)sizeof ev) {} }  // flush dial
            if (n > 0) return n;
            long s = timeoutMs > 20 ? 20 : timeoutMs;
            if (s > 0) { struct timespec t = { 0, s * 1000000L }; nanosleep(&t, 0); }
            return 0;
        }
        // Keys arrive asynchronously on the engine thread; wait up to timeoutMs in
        // small slices, returning as soon as any are queued (keeps the frame cadence).
        long waited = 0;
        for (;;) {
            int n = drain(out, max);                              // createEvent hook: buttons
            if (n < max) n += drainDial(out + n, max - n);        // InfraDial hook: native dial rotation
            if (n < max) n += drainEvent0(out + n, max - n);      // event0: app-mode dial (compat)
            if (n > 0) return n;
            if (waited >= timeoutMs) return 0;
            long slice = timeoutMs - waited; if (slice > 8) slice = 8;
            struct timespec t = { 0, slice * 1000000L }; nanosleep(&t, 0);
            waited += slice;
        }
    }

    void shutdown() {
        g_own = 0;
        unpatchCreateEvent();                     // release input back to stock
        unpatchDialHook();                        // restore the stock dial callback
        if (ev0fd_ >= 0) { ::close(ev0fd_); ev0fd_ = -1; }
    }

private:
    int drain(InputEvent *out, int max) {
        int n = 0;
        unsigned h = g_head;
        while (g_tail != h && n < max) {
            unsigned id = g_ring[g_tail & 511]; g_tail++;
            if ((id & 0xFFFF0000u) != 0x15010000u) continue;   // our key namespace (skip 0x17005006 idle)
            unsigned code = (id >> 4) & 0xFFFu;
            unsigned edge = id & 0xFu;                          // 2 = key-down (press), 1 = key-up (release)
            // NOTE: the control-wheel ROTATION does NOT arrive here. On the real
            // device a dial-only rotation produces ZERO createEvent events; rotation
            // comes on /dev/input/event0 (525/526) and is handled by drainEvent0().
            int raw = codeToIsv(code);
            if (raw < 0) {
                // Unmapped. The boot-pressed shutter S1 (0x02/0x25/0x2b fan-out) and
                // zoom (0x22/0x23) fire CONTINUOUSLY; logging that flood to the ui.log
                // file saturates the single-core engine and makes ALL input crawl.
                // Drop those silently; log only *other* unmapped codes, sparsely, so the
                // real-device dial / unknown keys can still be reverse-engineered.
                if (code != 0x02u && code != 0x22u && code != 0x23u && code != 0x25u && code != 0x2bu)
                    logf("input: UNMAPPED id=0x%08x code=0x%x edge=%u", id, code, edge);
                continue;
            }
            logf("input: id=0x%08x code=0x%x edge=%u -> raw=%d", id, code, edge, raw);
            out[n].kind = EV_KEY; out[n].raw = raw;
            out[n].value = (edge == 1) ? 0 : 1;                 // key-DOWN (press, edge!=1) -> 1 (uicore acts on press)
            out[n].arg[0] = 0;
            n++;
        }
        return n;
    }

    // Read the control-wheel ROTATION from /dev/input/event0 (the real-device path,
    // CLAUDE.md §5). We take ONLY the two dial scan codes 525 (CW) / 526 (CCW) and
    // reuse the a5000 keymap (CTX_MENU CW->NAV_DOWN / CCW->NAV_UP, CTX_SHOOTING ->
    // exposure step); buttons are left to the createEvent hook so nothing is doubled.
    // Non-blocking; a detent is a {525,1}{525,0} pair, uicore acts on the down edge.
    // Consume the native-dial counters the InfraDial hook (my_dialRecv) produces, and
    // emit them as the a5000 dial scan codes 525/526 (keymap: CTX_MENU CW->NAV_DOWN /
    // CCW->NAV_UP, CTX_SHOOTING -> exposure step). Lock-free SPSC: hook increments,
    // poll() consumes. One step per detent event.
    int drainDial(InputEvent *out, int max) {
        int n = 0;
        unsigned cw = g_dial_cw, ccw = g_dial_ccw;        // snapshot producer cursors
        while (g_dial_cw_seen != cw && n < max) {
            out[n].kind = EV_KEY; out[n].raw = ISV_DIAL_CW;  out[n].value = 1; out[n].arg[0] = 0;
            g_dial_cw_seen++; n++;
            logf("input: engine-dial CW -> raw=%d", ISV_DIAL_CW);
        }
        while (g_dial_ccw_seen != ccw && n < max) {
            out[n].kind = EV_KEY; out[n].raw = ISV_DIAL_CCW; out[n].value = 1; out[n].arg[0] = 0;
            g_dial_ccw_seen++; n++;
            logf("input: engine-dial CCW -> raw=%d", ISV_DIAL_CCW);
        }
        return n;
    }

    int drainEvent0(InputEvent *out, int max) {
        if (ev0fd_ < 0) return 0;
        int n = 0;
        Ev0Rec ev;
        while (n < max && ::read(ev0fd_, &ev, sizeof ev) == (ssize_t)sizeof ev) {
            if (ev.type != EVDEV_EV_KEY) continue;
            if (ev.code != ISV_DIAL_CW && ev.code != ISV_DIAL_CCW) continue;  // dial only
            out[n].kind = EV_KEY; out[n].raw = ev.code; out[n].value = ev.value; out[n].arg[0] = 0;
            if (ev.value) logf("input: event0 DIAL raw=%d (%s)", ev.code, ev.code == ISV_DIAL_CW ? "CW" : "CCW");
            n++;
        }
        return n;
    }

    a5kinproc::Engine *eng_;
    int ev0fd_;        // /dev/input/event0, for the control-wheel rotation (525/526)
};

} // namespace

namespace a5kinproc {
uicore::Input *createCreateEventInput(Engine *eng) { return new CreateEventInput(eng); }

// HAND-TO-STOCK: drop input ownership immediately so createEvent + InfraDial pass
// keys to the stock view NOW (called by handToStock_begin, before poll() next runs).
// poll() keeps g_own at 0 while handToStock_active(); it re-owns on resume.
void handToStock_forceReleaseInput() { g_own = 0; }
}
