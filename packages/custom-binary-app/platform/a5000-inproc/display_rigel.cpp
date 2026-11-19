// a5000 in-process Display adapter -- Rigel WindowClient / GPU-composite backend
// (route (b), ALTERNATIVE to display_osd.cpp's software /dev/mem blit).
//
// Goal: present uicore's RGBA8888 Canvas as a Rigel window that the im.elf
// WindowServer composites on the DMP GPU (GLES: ws_hal_lock -> glTexImage2D ->
// EGL compose -> VDF OSD plane), instead of the CPU writing the uncached OSD
// scanout at 0x8f758000 word-by-word. Transparent (alpha 0) where uicore did
// not draw, so the VDF live-view plane shows beneath. Reuses the stock Rigel
// client that is STATICALLY linked inside libObj.so (the server binary we are
// injected into), resolved per-boot by offset (same technique as engine.h).
//
// ---------------------------------------------------------------------------
// IMPORTANT in-process feasibility finding (static RE of re/ida/libObj.so.c,
// 2026-11-17 -- CORRECTS re/rigel/windowclient.md, which was reversed from the
// *bionic* Android client gralloc.diadem.so, a DIFFERENT and more capable build
// than the one compiled into libObj.so):
//
//  * The buffer HAL IS resolvable in im.elf: ws_hal_alloc/lock/unlock/free and
//    hal_create are libObj-local functions (di_hal.cpp). windowclient.md open
//    item [?]#3 ("is ws_hal mapped in im.elf?") -> RESOLVED: yes. gralloc/
//    libdihal/libma are nflasha16 *bionic* libs, NOT in the glibc im.elf and
//    NOT dlopen-able from it; we use libObj's own copies instead.
//
//  * The server composites via GLES, NOT GRM_bitmapCreateComposite: the window
//    upload path sub_6789E8 does ws_hal_lock -> glTexImage2D(...) -> GL compose
//    (confirms docs/RE_UI_BRINGUP.md; corrects windowclient.md claim #2).
//
//  * BLOCKER [confirmed]: libObj's compiled WindowClient CANNOT create a
//    server-registered overlay window with caller-supplied buffers.
//      - WindowClient::createNativeWindow (sub_678358) -> CreateNativeWindow
//        (sub_6771D4) only succeeds for a6 == 0x1000 (WS_USAGE_HW_FB): it then
//        allocates an INTERNAL framebuffer via sub_5DB696 and does NOT consult
//        any caller buffer. For a6 != 0x1000 it logs "usage is not
//        WS_USAGE_HW_FB!!" and returns 0 -> createNativeWindow fails (returns
//        NULL, safely -- the assert is a log, not an abort).
//      - There is NO registerBuffers() anywhere in libObj (the gralloc global
//        dword_F530 + copy-to-message+104 path does not exist here).
//      - a6 == 0x1000 is ALSO the message-suppress mode (createNativeWindow
//        sends NO type-0 create to the server), i.e. the Android-SF main-surface
//        mechanism, not a composited overlay.
//    => An in-proc UX_DEBUG_CLIENT overlay window fed by our ws_hal_alloc buffer
//    is not reachable through the stock libObj client. See the final report /
//    windowclient.md addendum for the two unblock paths (reproduce the SF
//    main-surface attach/flush protocol, or the exported GRM_* GPU path).
//
// Therefore this backend is SAFE-BY-DESIGN: init() attempts the documented
// Rigel create sequence and, on the expected NULL from rglCreateWindow, logs the
// exact failure and returns false so inproc.cpp falls back to the software OSD
// backend (zero regression, brick risk 0, RAM-only, reversible). Every API call
// is backed by the RE above; [?] marks what still needs real-device/emulator
// confirmation. All parameters (a5/a6/fmt/usage/nbuf) are env-overridable so the
// remaining empirical questions can be probed in the emulator WITHOUT a rebuild,
// all under the A5K_UI_RIGEL gate.
#include "uicore/platform.h"
#include "uicore/canvas.h"
#include "uicore/types.h"
#include "uicore/log.h"
#include "engine.h"
#include <stdlib.h>
#include <string.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/mman.h>
#include <stdarg.h>
#include <stdio.h>
#include <sys/time.h>

using namespace uicore;

namespace {

// ---- persistent probe log (survives a crash+reboot, unlike the tmpfs ui.log) --
// Writes to /setting/rprobe.log (NAND rw-FAT, persistent) with fsync per line.
// Used ONLY by the RE probe path; emulator disposable NAND, no brick risk.
void plog(const char *fmt, ...) {
    int fd = ::open("/setting/rprobe.log", O_WRONLY | O_CREAT | O_APPEND, 0644);
    if (fd < 0) fd = ::open("/android/a5k/rprobe.log", O_WRONLY | O_CREAT | O_APPEND, 0644);
    if (fd < 0) return;
    char buf[512];
    va_list ap; va_start(ap, fmt);
    int n = vsnprintf(buf, sizeof buf - 1, fmt, ap);
    va_end(ap);
    if (n < 0) n = 0; if (n > (int)sizeof buf - 1) n = sizeof buf - 1;
    buf[n++] = '\n';
    ssize_t w = ::write(fd, buf, n); (void)w;
    ::fsync(fd);
    ::close(fd);
}

// WSImageBuffer = 20 bytes = 5 x u32 (di_hal.cpp; [4] = global MA slot index).
struct WSImageBuffer { unsigned int w[5]; };

// ===========================================================================
// DETOUR-AND-SWAP hook on vdf_if_port_send_update (sub_5DC3D4) -- the in-band VDF
// OSD submit. Instead of calling the submit ourselves out-of-band (crashes, gate
// B/C), we hook the STOCK submit so it runs in its own thread / OSD-pin context
// and SWAP the panel-OSD record's buffer phys to our own pre-filled buffer. Then
// the stock VDF carries OUR pixels with zero context violation.
//
// Hook = the project's proven inline patch (LDR.W PC,[PC,#0] -> my_vdfSend), with
// call-through via unpatch-call-repatch (serialized) so we run the real submit.
// Instrument-first: A5K_VDF_FORCE logs live records (to learn the real format /
// phys offset / whether the panel OSD actually fires); A5K_VDF_SWAP additionally
// rewrites the phys field. Everything is RAM-only, reversible, brick risk 0.
typedef int (*vh_fn_raw)(const void *, int);
struct VhState {
    unsigned long target;   // libObj + OFF_vdf_send_update
    unsigned char save[8];
    int   patched;
    volatile unsigned ourPhys;  // PUBLISHED submit-buffer phys (atomic handoff).
                            // Single-producer (render thread, present()) / single-
                            // consumer (stock submit thread, my_vdfSend). An aligned
                            // 32-bit load/store is atomic on ARMv7; __sync_synchronize
                            // around it gives release/acquire so the consumer never
                            // sees a phys whose buffer writes are not yet visible.
                            // 0 = nothing published yet (hook passes through).
    int   swap;             // A5K_VDF_SWAP: actually rewrite phys
    int   physOff;          // phys field byte offset within the 32B record
    int   logn;             // instrument: log the first few submits
    volatile int lock;
};
VhState g_vh = { 0, {0}, 0, 0, 0, 20, 0, 0 };

// Count of real VDF submits (detour my_vdfSend call-throughs + Path A direct sends),
// used only for the "vdf: present rate N fps" logging so the full-rate Path A can be
// compared against the stock-cadence detour-swap. Single writer per site; a racy read
// for a log line needs no barrier.
volatile unsigned g_submitCount = 0;

// ===========================================================================
// HAND-TO-STOCK: make an opened stock view (viewManagerIf::openView) VISIBLE and
// interactive by STEPPING BACK our route-b overlay. When an action opens a stock
// view (format / device-name / USB / area / folder / app-launch) we must stop
// covering it (suspend OUR present) and stop eating its keys (release input), then
// RESUME when that stock view closes.
//
//  * detour-swap backend (default): my_vdfSend stops swapping while active -> the
//    stock submit carries the STOCK view's own buffer (nothing else to restore).
//  * Path A backend: VdfDisplay::present restores the stock UpdateManager present
//    (mgr+100 = saved word) so stock composites its view; re-disables on resume.
//  * Rigel/Fbc backends: present() early-returns (stops our submit / stops writing
//    the stock element buffer).
//  * input: g_own forced 0 (input_createevent.cpp) so createEvent + InfraDial pass
//    keys to stock; our poll() drops any key ids it still observes.
//
// RESUME TRIGGER: poll the stock ViewManager current-view global (dword_EEDDE0,
// libObj+0xEEDDE0, identified by the openView RE, app_launch_and_actions.md §3.5).
// We snapshot it at begin (preOpen); once it CHANGES (the stock view actually came
// up) and later reverts to preOpen (the user backed out), we resume. A hard
// fail-safe TIMEOUT resumes no matter what, so the panel/input can NEVER lock
// (brick-0). [device-seal] the EXACT current-view id values are not known
// statically -- the change-then-revert heuristic + timeout is the honest
// static-safe trigger; a device session can tighten it to an exact view-id compare.
struct HandToStock {
    volatile int       active;      // 1 = our present suspended, keys handed to stock
    a5kinproc::Engine *eng;
    unsigned long      curViewAddr; // libObj + OFF_g_currentView
    unsigned           preOpen;     // current-view snapshot taken at begin()
    int                sawChange;   // current view changed away from preOpen (view came up)
    long               startMs;     // begin() time (for the fail-safe timeout)
    int                passthrough; // 1 = INDEFINITE stock-shooting passthrough (no view-close/timeout resume)
};
HandToStock g_hts = { 0, 0, 0, 0, 0, 0, 0 };
static const unsigned long OFF_g_currentView = 0xEEDDE0;  // ViewManager current-view (dword_EEDDE0)
static const long HTS_TIMEOUT_MS = 60000;                 // fail-safe: never hand off forever
static long hts_mono_ms() {
    struct timeval tv; gettimeofday(&tv, 0);
    return (long)tv.tv_sec * 1000 + tv.tv_usec / 1000;
}

int my_vdfSend(const void *recs, int count);

void vh_write_patch() {
    unsigned char *t = (unsigned char *)g_vh.target;
    t[0]=0xDF; t[1]=0xF8; t[2]=0x00; t[3]=0xF0;            // LDR.W PC,[PC,#0]
    unsigned long tgt = ((unsigned long)&my_vdfSend) | 1UL;
    memcpy(t + 4, &tgt, 4);
    __builtin___clear_cache((char *)g_vh.target, (char *)g_vh.target + 8);
}
void vh_restore_bytes() {
    memcpy((void *)g_vh.target, g_vh.save, 8);
    __builtin___clear_cache((char *)g_vh.target, (char *)g_vh.target + 8);
}

// The detour. Runs on the STOCK submit thread (correct VDF/OSD-pin context).
int my_vdfSend(const void *recs, int count) {
    // Instrument: log the first few live submits (full 32B record -> phys offset).
    if (g_vh.logn < 6 && recs) {
        const unsigned *r = (const unsigned *)recs;
        int n = count > 2 ? 2 : count;
        for (int i = 0; i < n; i++)
            plog("vdfhook: n=%d rec%d: %08x %08x %08x %08x %08x %08x %08x %08x",
                 count, i, r[i*8+0], r[i*8+1], r[i*8+2], r[i*8+3],
                 r[i*8+4], r[i*8+5], r[i*8+6], r[i*8+7]);
        g_vh.logn++;
    }
    // Swap: for the panel OSD record (full-screen 640x480), overwrite the phys
    // field so the stock submit carries our pre-filled buffer. ACQUIRE-load the
    // currently published buffer phys ONCE (a single aligned 32-bit read) so every
    // record in this submit gets the same, fully-rendered buffer -- never one the
    // render thread is mid-write (see present(): it only ever writes the OTHER buffer).
    // While handed to stock (g_hts.active) we DO NOT swap: the stock submit then
    // carries the stock view's OWN buffer, so the opened view is visible/interactive.
    if (g_vh.swap && !g_hts.active && recs && g_vh.physOff >= 0) {
        unsigned pub = g_vh.ourPhys;    // atomic 32-bit load of the published phys
        __sync_synchronize();           // acquire: pair with present()'s release store
        if (pub) {
            unsigned char *rr = (unsigned char *)recs;
            int n = count > 2 ? 2 : count;
            for (int i = 0; i < n; i++) {
                unsigned char *rec = rr + 32 * i;
                unsigned short w2 = *(unsigned short *)(rec + 4), w3 = *(unsigned short *)(rec + 6);
                unsigned short w4 = *(unsigned short *)(rec + 8), w5 = *(unsigned short *)(rec + 10);
                bool isPanel = (w2 == 640 || w3 == 640 || w4 == 640 || w3 == 480 || w5 == 480);
                if (isPanel) memcpy(rec + g_vh.physOff, &pub, 4);
            }
        }
    }
    // Call through to the real submit (unpatch-call-repatch, serialized).
    while (__sync_lock_test_and_set(&g_vh.lock, 1)) { }
    vh_restore_bytes();
    vh_fn_raw orig = (vh_fn_raw)((g_vh.target) | 1UL);
    int rr = orig(recs, count);
    vh_write_patch();
    __sync_lock_release(&g_vh.lock);
    g_submitCount++;   // a real panel submit went out (stock cadence) -- for rate log
    return rr;
}

bool vh_install(unsigned long libObj, unsigned ourPhys, int doSwap, int physOff) {
    if (g_vh.patched) return true;
    g_vh.target  = libObj + a5kinproc::OFF_vdf_send_update;
    g_vh.ourPhys = ourPhys;
    g_vh.swap    = doSwap;
    g_vh.physOff = physOff;
    g_vh.logn    = 0;
    unsigned long page = g_vh.target & ~0xfffUL;
    // Keep the page RWX for the lifetime of the hook: the call-through rewrites
    // these 8 bytes on EVERY submit (unpatch-call-repatch), so it must stay
    // writable (R-X here would SIGSEGV on the first restore). RAM-only, reversible.
    if (mprotect((void *)page, 0x2000, PROT_READ | PROT_WRITE | PROT_EXEC)) { plog("vdfhook: mprotect failed"); return false; }
    memcpy(g_vh.save, (void *)g_vh.target, 8);
    vh_write_patch();
    g_vh.patched = 1;
    plog("vdfhook: installed @0x%lx (swap=%d physOff=%d ourPhys=0x%08x)", g_vh.target, doSwap, physOff, ourPhys);
    return true;
}
void vh_uninstall() {
    if (!g_vh.patched) return;
    while (__sync_lock_test_and_set(&g_vh.lock, 1)) { }
    vh_restore_bytes();                 // page is already RWX (kept so since install)
    g_vh.patched = 0;
    __sync_lock_release(&g_vh.lock);
}

int env_int(const char *name, int def) {
    const char *v = getenv(name);
    if (!v || !*v) return def;
    return (int)strtol(v, 0, 0);
}

class RigelDisplay : public Display {
public:
    explicit RigelDisplay(a5kinproc::Engine *eng)
        : eng_(eng), client_(0), nbuf_(2), back_(0), ok_(false),
          shadow_(0), shadowW_(0), shadowH_(0), firstFrame_(true), htsWas_(false) {
        memset(bufs_, 0, sizeof bufs_);
        memset(stride_, 0, sizeof stride_);
        memset(allocd_, 0, sizeof allocd_);
    }

    // ---- resolved trampolines (all libObj, Thumb -> | 1) ------------------
    typedef void *(*fn_create)(void *, const char *, int, int, int, int, unsigned char);
    typedef int   (*fn_show)(void *, int);
    typedef void *(*fn_post)(void *, int);
    typedef int   (*fn_destroy)(void *);
    typedef int   (*fn_alloc)(int, int, int, int, void *, void *);
    typedef int   (*fn_lock)(void *, int, int, int, int, int, void **);
    typedef int   (*fn_unlock)(void *);
    typedef int   (*fn_free)(void *);

    void *T(unsigned off) { return (void *)((eng_->libObj() + off) | 1); }

    bool init() {
        if (!eng_ || !eng_->ok()) { logf("rigel: engine unresolved -> fallback"); return false; }
        unsigned long lo = eng_->libObj();

        // SAFETY GATE. Emulator verification (2026-11-17) CONFIRMED that merely
        // attempting the in-proc create sequence (ws_hal_alloc + rglCreateWindow
        // on the stock libObj client) CRASHES im.elf -> camera-engine watchdog
        // reboot. Root cause is the BLOCKER documented in this file's header: the
        // libObj WindowClient has no caller-buffer overlay path, and creating a
        // second client in the server process touches shared Rigel state that is
        // not safe to re-enter from an injected thread. So by DEFAULT this backend
        // does NOT make those calls: it logs the finding and returns false ->
        // inproc.cpp falls back to the software OSD blit (zero regression, no
        // crash, brick risk 0). The actual (known-unsafe) attempt is behind the
        // explicit A5K_RIGEL_FORCE=1 gate, for controlled RE in a DISPOSABLE
        // emulator ONLY -- never on the real device.
        if (!env_int("A5K_RIGEL_FORCE", 0)) {
            logf("rigel: in-proc UX_DEBUG_CLIENT overlay path is BLOCKED on stock libObj");
            logf("rigel:   (no registerBuffers; createNativeWindow needs a6=0x1000 HW-FB, which");
            logf("rigel:   also suppresses the server create msg). Attempting it crashes im.elf.");
            logf("rigel: -> software OSD fallback (set A5K_RIGEL_FORCE=1 to attempt in emulator RE).");
            return false;
        }
        logf("rigel: A5K_RIGEL_FORCE=1 -> attempting the known-unsafe create sequence");

        nbuf_ = env_int("A5K_RIGEL_NBUF", 2);
        if (nbuf_ < 1) nbuf_ = 1; if (nbuf_ > MAXBUF) nbuf_ = MAXBUF;
        const int W = PANEL_W, H = PANEL_H;
        const int fmt   = env_int("A5K_RIGEL_FMT", 7);    // [?] GraphicsServer cfg uses fmt=7; RGBA8888 layout TBC
        const int usage = env_int("A5K_RIGEL_USAGE", 0);  // ws_hal_alloc usage (not WS_USAGE_HW_FB)
        const int a5    = env_int("A5K_RIGEL_A5", 0);     // SF uses a5=0
        const int a6    = env_int("A5K_RIGEL_A6", 0);     // SF uses a6=0; 0x1000 = HW-FB/main-surface (msg-suppress)

        fn_alloc  alloc = (fn_alloc) T(a5kinproc::OFF_ws_hal_alloc);
        fn_create create= (fn_create)T(a5kinproc::OFF_rglCreateWindow);
        fn_show   show  = (fn_show)  T(a5kinproc::OFF_WC_showWindow);

        logf("rigel: libObj=0x%lx create=0x%lx alloc=0x%lx (W=%d H=%d fmt=%d usage=%d a5=%d a6=0x%x nbuf=%d)",
             lo, lo + a5kinproc::OFF_rglCreateWindow, lo + a5kinproc::OFF_ws_hal_alloc,
             W, H, fmt, usage, a5, a6, nbuf_);

        // 1) Allocate nbuf shared RGBA buffers via libObj's own ws_hal (hal_create
        //    was already run by GraphicsServer::Init; we do not re-create it).
        for (int i = 0; i < nbuf_; i++) {
            unsigned int out2 = 0;
            int r = alloc(W, H, fmt, usage, &bufs_[i], &out2);
            if (r != 0) {
                logf("rigel: ws_hal_alloc[%d] FAILED r=%d -> fallback", i, r);
                teardownBuffers();
                return false;
            }
            allocd_[i] = true;
            stride_[i] = out2 ? out2 : (unsigned)W;   // out2 = aligned row pixels
            logf("rigel: ws_hal_alloc[%d] ok slot=%u stridePx=%u", i, bufs_[i].w[4], stride_[i]);
        }

        // 2) Create the UX_DEBUG_CLIENT window. factory MUST be NULL for the
        //    in-proc client (sub_6771D4: "native_display is not valid" if != 0).
        client_ = create(0, "UX_DEBUG_CLIENT", W, H, a5, a6, (unsigned char)nbuf_);
        if (!client_) {
            logf("rigel: rglCreateWindow(UX_DEBUG_CLIENT) returned NULL.");
            logf("rigel: CONFIRMED BLOCKER -- libObj WindowClient::createNativeWindow rejects");
            logf("rigel: a6=0x%x (non WS_USAGE_HW_FB); no registerBuffers path exists in-proc.", a6);
            logf("rigel: our ws_hal buffers cannot be attached to a server overlay window ->");
            logf("rigel: falling back to the software OSD blit (no regression).");
            teardownBuffers();
            return false;
        }
        logf("rigel: rglCreateWindow -> client=%p (UNEXPECTED success; proceeding)", client_);

        // 3) Show it (top of z-order = newest window).
        int sr = show(client_, 1);
        logf("rigel: showWindow -> %d", sr);

        ok_ = true;
        return true;
    }

    // Present uicore's RGBA8888 canvas into the back buffer (dirty-row skip like
    // display_osd) then post it. Canvas byte order is already R,G,B,A == ws RGBA,
    // so a drawn-rows memcpy is enough; unused pixels keep alpha 0 (live-view
    // shows through). Only reached if a window exists (see init()).
    void present(const Canvas &c, const Rect &) {
        if (!ok_ || !client_) return;
        if (g_hts.active) { htsWas_ = true; return; }   // hand-to-stock: stop posting
        if (htsWas_) { htsWas_ = false; firstFrame_ = true; }
        const int W = c.width(), H = c.height();
        const u8 *src = c.pixels();                 // RGBA8888, W*H*4
        const size_t rowBytes = (size_t)W * 4;

        if (!shadow_ || shadowW_ != W || shadowH_ != H) {
            free(shadow_);
            shadow_ = (u8 *)malloc((size_t)W * H * 4);
            shadowW_ = W; shadowH_ = H; firstFrame_ = true;
        }

        void *lp = 0;
        fn_lock   lock   = (fn_lock)  T(a5kinproc::OFF_ws_hal_lock);
        fn_unlock unlock = (fn_unlock)T(a5kinproc::OFF_ws_hal_unlock);
        fn_post   post   = (fn_post)  T(a5kinproc::OFF_WC_post);

        if (lock(&bufs_[back_], 256, 0, 0, W, H, &lp) != 0 || !lp) {
            logf("rigel: ws_hal_lock[%d] FAILED", back_);
            return;
        }
        const unsigned dstStrideB = stride_[back_] * 4;   // bytes per dst row
        u8 *dst = (u8 *)lp;
        int dirtyTop = -1, dirtyBot = -1;
        for (int y = 0; y < H; y++) {
            const u8 *sp = src + (size_t)y * rowBytes;
            u8 *shrow = shadow_ + (size_t)y * rowBytes;
            if (!firstFrame_ && memcmp(shrow, sp, rowBytes) == 0) continue;
            memcpy(shrow, sp, rowBytes);
            memcpy(dst + (size_t)y * dstStrideB, sp, rowBytes);  // RGBA8888 -> RGBA8888
            if (dirtyTop < 0) dirtyTop = y;
            dirtyBot = y;
        }
        firstFrame_ = false;
        unlock(&bufs_[back_]);

        // post(client, frontIdx): WindowClient::post reads the DirtyRegion object
        // (we posted whole-window here) and sends type-6. Toggle buffers.
        (void)dirtyTop; (void)dirtyBot;
        post(client_, back_);
        if (nbuf_ > 1) back_ ^= 1;
    }

    void shutdown() {
        if (client_) {
            fn_destroy destroy = (fn_destroy)T(a5kinproc::OFF_WC_destroyWindow);
            destroy(client_);
            client_ = 0;
        }
        teardownBuffers();
        if (shadow_) { free(shadow_); shadow_ = 0; }
        ok_ = false;
    }
    const char *name() const { return "rigel-inproc"; }

private:
    enum { MAXBUF = 2 };
    void teardownBuffers() {
        fn_free freeb = (fn_free)T(a5kinproc::OFF_ws_hal_free);
        for (int i = 0; i < MAXBUF; i++)
            if (allocd_[i]) { freeb(&bufs_[i]); allocd_[i] = false; }
    }

    a5kinproc::Engine *eng_;
    void   *client_;
    WSImageBuffer bufs_[MAXBUF];
    unsigned stride_[MAXBUF];     // aligned row pixel count per buffer
    bool     allocd_[MAXBUF];
    int      nbuf_, back_;
    bool     ok_;
    u8      *shadow_;
    int      shadowW_, shadowH_;
    bool     firstFrame_;
    bool     htsWas_;            // hand-to-stock: was stepped back (force full repaint on resume)
};

// ===========================================================================
// VdfDisplay -- present uicore's canvas by REUSING the stock GPU/VDF OSD path.
// This is the promising "reproduce sub_5D99D0" backend (the task's target).
//
// sub_5D99D0 (libObj 0x5D99D0, dmpNative::SenserIf OSD-present handler) -- FULLY
// decompiled & cross-checked (re/ida/libObj.so.c:1319581). It does exactly:
//
//   int64 dest; void* bmpOut;
//   BMP_CreateFromPhys(640,480, fmt=2, yuv=0, phys=dword_EEED1C, stride=640,
//                      &dest, &bmpOut, 0, 0);          // 0x589A00
//   BMP_PixelFormatToBytesPerPixel(2, &bpp);           // 0x589F48
//   update_osd_record rec;                             // 32 bytes, on stack
//   rec[+0]  = (u32)dest;            // v13  \ the 8-byte BMP_RectangleRecord
//   rec[+4]  = (u16)(dest>>16);      // v14  | geometry descriptor RETURNED BY
//   rec[+6]  = (u32)(dest>>32);      // v15  | BMP_CreateFromPhys (NOT hand-made)
//   rec[+10] = (u16)(dest>>32);      // v16  /
//   rec[+12] = bpp;                  // v17
//   rec[+16] = 0x103;                // v18  (flags: 259)
//   rec[+20] = phys (dword_EEED1C);  // v19
//   rec[+24] = 4;                    // v20
//   rec[+28] = bmpOut;               // v21
//   vdf_if_port_send_update(&rec, 1);   // 0x5DC3D4: msg type 5, memcpy 32B, send
//   BMP_Release(bmpOut);                // 0x589E18
//
// dword_EEED1C (0xEEED1C) = PHYSICAL addr of the stock "grm_sens_work" 16bpp OSD
// work buffer, allocated via MA by dmpNative::SenserCommunicatorBase::utilGetMemory
// (sub_5D9AEC) on SenserIf start. The panel scanout (AIF ch53) is fed from this
// buffer by VDF. => we fill THIS buffer with our RGBA->16bpp canvas, then submit
// one record exactly as above. No new Rigel window; the stock compositor/VDF
// carries it to the panel.
//
// *** HOW THIS BACKEND ACTUALLY WORKS (DETOUR-AND-SWAP, emulator-PROVEN 2026-11-17) ***
// We do NOT submit the record ourselves: calling vdf_if_port_send_update out-of-band
// from our injected worker thread CRASHES im.elf (gate B/C -- it needs the stock
// SenserIf thread / connected OSD pin 0x59000E). Instead:
//   1. utilGetMemory() -> our own physical OSD buffer (dword_EEED1C).
//   2. present() fills it 16bpp RGBA4444 (the live stock OSD record is 16bpp).
//   3. We HOOK vdf_if_port_send_update (sub_5DC3D4) and, on the STOCK submit thread
//      (correct context!), SWAP the panel-OSD record's buffer phys (+20) to ours.
//      The stock VDF then DMAs OUR buffer to the panel OSD layer (ch53), composited
//      by the hardware over the live-view YUV plane (transparent where we didn't draw).
// RESULT: our route-b OSD appears on the panel over live-view via the real VDF path,
// no CPU blit to the uncached scanout, and NO crash. See the vh:: hook + my_vdfSend.
//
// The record-format block above documents sub_5D99D0 (the SenserIf variant we RE'd
// first); the live record the hook sees is the same FINAL format: phys @ +20.
// Gated by A5K_UI_VDF (default = software OSD blit). A5K_VDF_NOSWAP=1 = instrument
// only (log the live records and pass through, no swap -- for RE).
class VdfDisplay : public Display {
public:
    explicit VdfDisplay(a5kinproc::Engine *eng)
        : eng_(eng), lo_(0), osdPhys_(0), mapLen_(0), mfd_(-1),
          nbuf_(1), back_(0), shadow_(0), line_(0), firstFrame_(true), ok_(false),
          loggedErr_(false), loggedOk_(false),
          patha_(false), mgrDisabled_(false), mgr_(0), mgrSaveWord_(0),
          pathaBmps_(0), rtFrames_(0), rtSubmit0_(0), suspended_(false) {
        bufPhys_[0] = bufPhys_[1] = 0;
        bufMap_[0]  = bufMap_[1]  = 0;
        recBmpOut_[0] = recBmpOut_[1] = 0;
        memset(rec_, 0, sizeof rec_);
        memset(&rtLast_, 0, sizeof rtLast_);
    }

    typedef int (*fn_bmpcreate)(short, short, int, int, int, short, void *, void *, int, int);
    typedef int (*fn_bmprelease)(int);
    typedef int (*fn_fmt2bpp)(int, short *);
    typedef int (*fn_vdfsend)(const void *, int);
    typedef void *(*fn_mgrget)();
    void *T(unsigned off) { return (void *)((lo_ + off) | 1); }

    // ---- Path A: full-rate DIRECT present (opt-in mode of this backend) --------
    // Build a persistent GRM bitmap + 32B OSD record from EACH of our own phys
    // buffers (the sub_5D99D0 recipe), ONCE. present() then calls
    // vdf_if_port_send_update ourselves every frame -> panel updates at our render
    // rate, decoupled from the stock OSD change-cadence (present_pipeline_full.md §3.2).
    // On ANY failure before the stock mgr is touched, cleans up and returns false so
    // init() falls back to the device-verified detour-swap. Never partially engaged.
    bool setupPathA() {
        fn_bmpcreate bmpCreate = (fn_bmpcreate)T(a5kinproc::OFF_BMP_CreateFromPhys);
        fn_fmt2bpp   f2b       = (fn_fmt2bpp)  T(a5kinproc::OFF_BMP_fmt2Bpp);
        fn_vdfsend   vsend     = (fn_vdfsend)  T(a5kinproc::OFF_vdf_send_update);
        const int fmt = env_int("A5K_VDF_FMT", 2);   // sub_5D99D0 uses fmt=2 (16bpp)
        short bpp = 0; f2b(fmt, &bpp);
        pathaBmps_ = 0;
        for (int i = 0; i < nbuf_; i++) {
            long long dest = 0; void *bmpOut = 0;
            int r = bmpCreate((short)PANEL_W, (short)PANEL_H, fmt, 0,
                              (int)bufPhys_[i], (short)PANEL_W, &dest, &bmpOut, 0, 0);
            if (r != 0 || !bmpOut) {
                plog("vdf: BMP_CreateFromPhys[%d] FAILED r=%d phys=0x%08x bmp=%p", i, r, bufPhys_[i], bmpOut);
                teardownPathA();
                return false;
            }
            recBmpOut_[i] = (unsigned)bmpOut;
            unsigned char *rc = rec_[i]; memset(rc, 0, 32);
            // exact sub_5D99D0 record store order (re/ida/libObj.so.c:1319581):
            *(unsigned *)(rc + 0)        = (unsigned)dest;              // v13
            *(unsigned short *)(rc + 4)  = (unsigned short)(dest >> 16);// v14
            *(unsigned *)(rc + 6)        = (unsigned)(dest >> 32);      // v15
            *(unsigned short *)(rc + 10) = (unsigned short)(dest >> 32);// v16
            *(unsigned short *)(rc + 12) = (unsigned short)bpp;        // v17
            *(unsigned *)(rc + 16)       = 0x103;                      // v18 flags
            *(unsigned *)(rc + 20)       = bufPhys_[i];                // v19 phys
            *(unsigned *)(rc + 24)       = 4;                          // v20
            *(unsigned *)(rc + 28)       = (unsigned)bmpOut;           // v21 bmp obj
            pathaBmps_++;
            plog("vdf: Path A bmp[%d] phys=0x%08x bmp=%p bpp=%d dest=%016llx", i, bufPhys_[i], bmpOut, bpp, dest);
        }
        // First DIRECT submit (buffers are memset transparent -> a safe no-op frame).
        // A nonzero return means the direct path is unusable -> fall back cleanly.
        int sr = vsend(rec_[0], 1);
        g_submitCount++;
        if (sr != 0) { plog("vdf: first direct vdf_send_update FAILED r=%d -> fall back", sr); teardownPathA(); return false; }
        plog("vdf: first direct vdf_send_update ok");
        // Only NOW (direct path confirmed) disable the stock UpdateManager present
        // ping-pong, so a fall-back path would still have left the stock present live.
        fn_mgrget mgrGet = (fn_mgrget)T(a5kinproc::OFF_fbc_mgr_get);
        mgr_ = mgrGet();
        if (mgr_) {
            volatile unsigned *w = (volatile unsigned *)((unsigned char *)mgr_ + a5kinproc::UPDATEMGR_PRESENT_DISABLE_WORD);
            mgrSaveWord_ = *w;
            *w = 1;
            mgrDisabled_ = true;
            plog("vdf: stock UpdateManager present disabled (mgr=%p +%d: %u->1)", mgr_, a5kinproc::UPDATEMGR_PRESENT_DISABLE_WORD, mgrSaveWord_);
        } else {
            plog("vdf: UpdateManager singleton null -> stock present NOT disabled (Path A active; may ping-pong)");
        }
        return true;
    }

    // Reverse setupPathA: release the GRM bitmaps and restore the stock mgr word.
    void teardownPathA() {
        fn_bmprelease bmpRel = (fn_bmprelease)T(a5kinproc::OFF_BMP_Release);
        for (int i = 0; i < pathaBmps_; i++)
            if (recBmpOut_[i]) { bmpRel((int)recBmpOut_[i]); recBmpOut_[i] = 0; }
        pathaBmps_ = 0;
        if (mgrDisabled_ && mgr_) {
            volatile unsigned *w = (volatile unsigned *)((unsigned char *)mgr_ + a5kinproc::UPDATEMGR_PRESENT_DISABLE_WORD);
            *w = mgrSaveWord_;
            mgrDisabled_ = false;
            plog("vdf: stock UpdateManager present RESTORED (mgr+%d=%u)", a5kinproc::UPDATEMGR_PRESENT_DISABLE_WORD, mgrSaveWord_);
        }
    }

    // ---- hand-to-stock suspend/resume (one-time transition handlers) -----------
    // enter: for Path A we RESTORE the stock UpdateManager present (mgr+100 = saved)
    // so the stock composites its own view while we are stepped back. Detour-swap
    // needs nothing here (my_vdfSend already skips the swap while g_hts.active, so
    // the stock buffer shows). RAM-only, reversible.
    void enterHandToStock() {
        if (patha_ && mgrDisabled_ && mgr_) {
            volatile unsigned *w = (volatile unsigned *)((unsigned char *)mgr_ + a5kinproc::UPDATEMGR_PRESENT_DISABLE_WORD);
            *w = mgrSaveWord_;
            plog("vdf: hand-to-stock -> stock present RESTORED (mgr+%d=%u) so stock view composites",
                 a5kinproc::UPDATEMGR_PRESENT_DISABLE_WORD, mgrSaveWord_);
        } else {
            plog("vdf: hand-to-stock -> detour-swap paused (stock buffer shown)");
        }
    }
    // exit: re-disable the stock present for Path A (we resume driving direct sends).
    void exitHandToStock() {
        if (patha_ && mgrDisabled_ && mgr_) {
            volatile unsigned *w = (volatile unsigned *)((unsigned char *)mgr_ + a5kinproc::UPDATEMGR_PRESENT_DISABLE_WORD);
            *w = 1;
            plog("vdf: resume -> stock present re-disabled (Path A direct resumes)");
        } else {
            plog("vdf: resume -> detour-swap resumes (our OSD repaints)");
        }
    }

    // Path A per-frame: submit the record for the just-published buffer directly.
    void sendDirect(int idx) {
        fn_vdfsend vsend = (fn_vdfsend)T(a5kinproc::OFF_vdf_send_update);
        vsend(rec_[idx], 1);
        g_submitCount++;
    }

    // Log the actual panel submit rate once per second (shows full-rate vs cadence).
    void rateTick() {
        rtFrames_++;
        struct timeval now; gettimeofday(&now, 0);
        long ms = (now.tv_sec - rtLast_.tv_sec) * 1000 + (now.tv_usec - rtLast_.tv_usec) / 1000;
        if (ms >= 1000) {
            unsigned subs = g_submitCount - rtSubmit0_;
            plog("vdf: present rate %u fps (panel submits %u/s, mode=%s)",
                 (unsigned)(rtFrames_ * 1000 / ms), (unsigned)((unsigned long)subs * 1000 / ms),
                 patha_ ? "pathA-direct" : "detour-swap");
            rtLast_ = now; rtFrames_ = 0; rtSubmit0_ = g_submitCount;
        }
    }

    bool init() {
        if (!eng_ || !eng_->ok()) { plog("vdf: engine unresolved"); return false; }
        // This backend is selected explicitly (A5K_UI_VDF / /android/a5k/vdf, see
        // inproc.cpp); that selection IS the gate, so init() does the real work.
        // The software OSD blit remains the default when the gate is absent.
        lo_ = eng_->libObj();
        osdPhys_ = *(volatile unsigned *)(lo_ + a5kinproc::OFF_g_osdBufPhys);
        plog("vdf: g_osdBufPhys(dword_EEED1C)=0x%08x", osdPhys_);
        // SUBMIT HAZARD (gate C, emulator-confirmed 2026-11-17): calling the raw
        // vdf_if_port_send_update out-of-band from our injected worker thread CRASHES
        // im.elf (camera-engine watchdog) -- even with a correct 32bpp record
        // (dest=unk_BC852E geometry 640x480, phys=our own grm_sens_work buffer) and
        // live-view active. The submit needs the stock SenserIf thread / connected
        // VDF OSD pin (0x59000E) context we do not hold. So by DEFAULT this backend
        // regression. DETOUR-AND-SWAP (emulator-PROVEN working, 2026-11-17): we do
        // NOT submit out-of-band (that crashes, gate B/C). We hook the stock submit
        // and swap its panel-OSD record's buffer phys to our own. A5K_VDF_RAW=1 is a
        // disposable-emulator-only escape to the OLD (crashing) out-of-band submit.
        plog("vdf: detour-and-swap engage (hook stock vdf_if_port_send_update)");
        // OWN-BUFFER path (PROVEN working): allocate our own grm_sens_work buffer(s)
        // via the stock utilGetMemory(ignored, GRM_SENS_WORK_TYPE): MA_CreateSpace(
        // "grm_sens_work") -> MA_Alloc -> MA_LogicalToPhysical(&dword_EEED1C). Binds a
        // 640x480 physical buffer we own, independent of boot/sensor state; we fill it
        // 16bpp (the live stock OSD record is 16bpp) and the hook points the stock
        // record at it.
        //
        // DOUBLE BUFFER (tearing fix): we allocate TWO such buffers. utilGetMemory
        // UNCONDITIONALLY re-creates the MA space and rewrites dword_EEED1C each call
        // (sub_5D9AEC: no early-out on byte_EEED20), so a second call yields a SECOND
        // independent physical buffer -- read dword_EEED1C right back to capture it.
        // The render thread (present) always fills the buffer NOT currently published,
        // then atomically publishes it; the submit thread (my_vdfSend) swaps in the
        // published one. Our two buffers are ours alone -- the stock OSD producer owns
        // its own double buffer (0x8f758000<->0x8f7ee000), downstream of our handoff.
        typedef int (*fn_util)(int, int);
        fn_util util = (fn_util)T(a5kinproc::OFF_utilGetMemory);

        // Buffer 0: reuse an already-bound grm_sens_work, else allocate our own.
        if (!osdPhys_) {
            plog("vdf: grm_sens_work unbound -> utilGetMemory[0](type=%d)", a5kinproc::GRM_SENS_WORK_TYPE);
            int ur = util(0, a5kinproc::GRM_SENS_WORK_TYPE);
            osdPhys_ = *(volatile unsigned *)(lo_ + a5kinproc::OFF_g_osdBufPhys);
            unsigned char flag = *(volatile unsigned char *)(lo_ + a5kinproc::OFF_g_osdAllocFlag);
            plog("vdf: utilGetMemory[0] ret=%d flag=%u -> g_osdBufPhys=0x%08x", ur, flag, osdPhys_);
        }
        if (!osdPhys_) { plog("vdf: osd phys still 0 -> fallback"); return false; }
        bufPhys_[0] = osdPhys_;

        // Buffer 1: a SECOND own grm_sens_work (new MA space) unless single-buffer is
        // forced. If the second alloc returns 0 or the same phys, degrade to single
        // buffer (no crash) -- utilGetMemory asserts on true MA failure, but this
        // guards the benign "same/empty" outcomes.
        // SINGLE-BUFFER IN-PLACE is the DEFAULT and the full-rate present path (RE +
        // emulator proof, 2026-11-19). The ch53 OSD scanout is CONTINUOUS: it re-reads the
        // latched buffer every frame (independently proven by the software OSD backend,
        // which presents at full rate with ZERO vdf submits). So with ONE buffer whose
        // phys stays latched in the panel record, our in-place present() writes appear at
        // scanout rate -- decoupled from the sparse stock submit (~1/s). The former DOUBLE
        // buffer was the actual latency bug: a freshly rendered back buffer is only shown
        // once a stock submit latches it, and stock submits are ~1/s (emulator: bar moved
        // every screendump in single-buffer at 1/s submits; forceSubmit mgr+96 did NOT
        // raise the submit rate -> the stock walk is genuinely sparse, Case B). Minor
        // tearing (scanout reading while we write) is bounded by the dirty-row skip below.
        // Opt back into the (laggy) double buffer only for A/B measurement.
        nbuf_ = 1;
        bool wantDouble = env_int("A5K_VDF_DOUBLEBUF", 0) || ::access("/android/a5k/vdf_dbuf", F_OK) == 0;
        if (wantDouble) {
            int ur = util(0, a5kinproc::GRM_SENS_WORK_TYPE);
            unsigned p1 = *(volatile unsigned *)(lo_ + a5kinproc::OFF_g_osdBufPhys);
            plog("vdf: [dbuf] utilGetMemory[1] ret=%d -> buf1 phys=0x%08x", ur, p1);
            if (p1 && p1 != bufPhys_[0]) { bufPhys_[1] = p1; nbuf_ = 2; }
            else plog("vdf: second buffer unavailable (phys=0x%08x) -> single-buffer mode", p1);
        }

        // OSD buffer mapping device. /dev/mem = strongly-ordered DEVICE memory -> CPU writes are
        // ~8MB/s (the present floor, device 2026-11-19). /dev/stream (stream.ko) maps the SAME SDRAM
        // phys NON-cacheable but WRITE-BUFFERED (write-combining; it clears L_PTE_MT_MASK), which is
        // far faster for our sequential OSD writes; it needs a write barrier per frame to drain (added
        // at the end of present()). RE: docs/RE/display/0006. Opt-in (marker / env) with /dev/mem
        // fallback so a bad /dev/stream can never break the panel. brick-0.
        bool wantStream = env_int("A5K_VDF_STREAM", 0) || ::access("/android/a5k/vdf_stream", F_OK) == 0;
        const char *memdev = wantStream ? "/dev/stream" : "/dev/mem";
        mfd_ = ::open(memdev, O_RDWR);
        if (mfd_ < 0 && wantStream) { plog("vdf: /dev/stream open failed -> /dev/mem"); memdev = "/dev/mem"; mfd_ = ::open(memdev, O_RDWR); }
        if (mfd_ < 0) { plog("vdf: %s open failed", memdev); return false; }
        plog("vdf: OSD buffer device = %s", memdev);
        // 16bpp OSD plane: 640x480x2 = 0x96000; map exactly what we fill.
        size_t len = (size_t)PANEL_W * PANEL_H * 2;
        len = (len + 0xfffUL) & ~0xfffUL;
        mapLen_ = len;
        for (int i = 0; i < nbuf_; i++) {
            unsigned ph = bufPhys_[i];
            void *m = ::mmap(0, len, PROT_READ | PROT_WRITE, MAP_SHARED, mfd_, ph & ~0xfffUL);
            if (m == MAP_FAILED) {
                plog("vdf: mmap buf%d phys 0x%08x failed", i, ph);
                if (i == 0) { ::close(mfd_); mfd_ = -1; return false; }
                nbuf_ = 1;   // buf1 map failed -> degrade to single buffer, keep going
                break;
            }
            bufMap_[i] = (volatile u8 *)((unsigned long)m + (ph & 0xfffUL));
            // Start transparent (RGBA4444 alpha nibble 0): an as-yet-unrendered
            // published buffer then shows live-view only, never stale garbage.
            memset(m, 0, len);
            plog("vdf: mapped buf%d phys 0x%08x -> %p", i, ph, (void *)bufMap_[i]);
        }
        plog("vdf: %s (nbuf=%d)", nbuf_ >= 2 ? "DOUBLE-BUFFERED (laggy, A/B only)" : "single-buffer in-place (full-rate)", nbuf_);

        // NOTE (RE + emulator proof 2026-11-19): the stock UpdateManager force-submit flag
        // mgr+96 (UPDATEMGR_FORCE_SUBMIT_WORD) can push sub_5DCAB4's change-gate to "submit",
        // but the stock OSD walk itself fires only ~1/s when our draw is suppressed, so
        // poking it did NOT raise the submit rate (Case B). Full rate therefore comes from
        // the continuous ch53 scanout of our single latched buffer, not from more submits --
        // hence single-buffer in-place above, no per-frame submit needed.

        // ---- Path A (opt-in, default OFF): full-rate DIRECT present ----------
        // A5K_VDF_PATHA=1 or the /android/a5k/vdf_patha marker engages it. Instead of
        // the detour-swap (throttled to the stock OSD change-cadence = the latency), we
        // build persistent GRM bitmaps from OUR phys buffers and call
        // vdf_if_port_send_update ourselves every present() = full render rate,
        // independent of stock cadence (present_pipeline_full.md path A). Fallback
        // ladder: Path A setup fail -> detour-swap (below) -> software (init returns
        // false). UNSET keeps the device-verified detour-swap as the DEFAULT.
        patha_ = env_int("A5K_VDF_PATHA", 0) || (::access("/android/a5k/vdf_patha", F_OK) == 0);
        if (patha_) {
            if (setupPathA()) {
                plog("vdf: Path A ENGAGED (direct full-rate present; stock cadence bypassed)");
                gettimeofday(&rtLast_, 0); rtFrames_ = 0; rtSubmit0_ = g_submitCount;
                ok_ = true;
                return true;
            }
            plog("vdf: Path A setup FAILED -> fall back to detour-and-swap (default)");
            patha_ = false;   // setupPathA cleaned up; stock mgr was never disabled on failure
        }

        // Install the hook. The stock vdf_if_port_send_update runs on the correct
        // SenserIf/OSD-pin thread; my_vdfSend swaps the panel-OSD record's phys (+20,
        // confirmed by the instrument log: record = {0, 480,640, 480,640, bpp=2,
        // flags=1, PHYS@+20, 2, bmp}) to the PUBLISHED buffer. We publish buffer
        // (1-back_) initially so the first present() (which renders into buf[back_=0])
        // never writes the buffer the submit thread may read: for double buffer that
        // is buf1 (transparent until the first full render); for single buffer it is
        // buf0. A5K_VDF_NOSWAP=1 = instrument-only; physOff overridable.
        back_ = 0;
        unsigned initPub = (nbuf_ >= 2) ? bufPhys_[1] : bufPhys_[0];
        int doSwap  = env_int("A5K_VDF_NOSWAP", 0) ? 0 : 1;
        int physOff = env_int("A5K_VDF_PHYSOFF", 20);
        if (!vh_install(lo_, initPub, doSwap, physOff)) { plog("vdf: hook install failed -> fallback"); return false; }
        gettimeofday(&rtLast_, 0); rtFrames_ = 0; rtSubmit0_ = g_submitCount;
        ok_ = true;
        return true;
    }

    // Convert one RGBA8888 canvas row to the 16bpp RGBA4444 OSD plane (same packing
    // display_osd.cpp uses: R<<12|G<<8|B<<4|A). alpha nibble 0 where uicore did not
    // draw -> live-view shows through.
    static inline void convRow(volatile u16 *d, const u8 *p, int W) {
        for (int x = 0; x < W; x++, p += 4)
            d[x] = (u16)(((p[0] >> 4) << 12) | ((p[1] >> 4) << 8) | ((p[2] >> 4) << 4) | (p[3] >> 4));
    }

    void present(const Canvas &c, const Rect &dr) {
        if (!ok_) return;
        // Hand-to-stock: step our overlay back so an opened stock view is visible.
        // (The resume decision is driven from input poll(); here we only react to
        // the flag so Path A restores/re-disables the stock present exactly once.)
        if (g_hts.active) {
            if (!suspended_) { enterHandToStock(); suspended_ = true; }
            return;                 // do not submit / fill our buffer while handed off
        }
        if (suspended_) { exitHandToStock(); suspended_ = false; firstFrame_ = true; }
        const int W = c.width(), H = c.height();
        const u8 *src = c.pixels();
        const size_t rowBytes = (size_t)W * 4;

        if (nbuf_ >= 2) {
            // DOUBLE BUFFER: draw the COMPLETE frame into the BACK buffer (never the
            // published one -- see init(): published index == 1-back_ always), then
            // atomically publish it. No dirty-row skip: because the two buffers
            // alternate, each one must be a whole frame on its own.
            volatile u8 *dst = bufMap_[back_];
            if (!dst) return;
            int just = back_;                  // the buffer we just rendered
            for (int y = 0; y < H; y++)
                convRow((volatile u16 *)(dst + (size_t)y * (W * 2)), src + (size_t)y * rowBytes, W);
            // PUBLISH (release): the barrier ensures all the pixel writes above are
            // globally visible before the submit thread can observe the new phys.
            __sync_synchronize();
            g_vh.ourPhys = bufPhys_[back_];   // atomic 32-bit store (detour-swap handoff)
            back_ ^= 1;                        // next frame renders the other buffer
            // Path A: submit our own record for the just-published buffer NOW, at our
            // render rate (no dependence on the stock present firing). Detour-swap mode
            // relies on the stock submit instead, so it does not send here.
            if (patha_) sendDirect(just);
            rateTick();
            (void)loggedOk_; (void)loggedErr_;
            return;
        }

        // SINGLE-BUFFER IN-PLACE -- the DEFAULT full-rate present (RE + emulator proof
        // 2026-11-19). The buffer phys stays latched in ch53 (continuous scanout), so these
        // in-place writes appear at render rate, not at the sparse stock-submit rate. The
        // dirty-row skip writes ONLY changed rows, which both cuts the uncached-write cost
        // and bounds tearing to the few rows that actually changed (negligible for menu nav).
        volatile u8 *dst = bufMap_[0];
        if (!dst) return;
        if (!shadow_) { shadow_ = (u8 *)malloc((size_t)W * H * 4); firstFrame_ = true; }
        if (!line_)   line_   = (u16 *)malloc((size_t)W * 2);   // cached 16bpp scratch row
        const unsigned dstRowB = (unsigned)W * 2;               // 16bpp bytes per dst row

        // PER-ROW dirty scan: the full-frame compare (reading the WHOLE canvas, DRAM-bound under
        // im.elf contention) was the fixed ~20ms/present cost -- even a 3-row menu move cost ~20ms
        // because a single bbox spans the menu header (top) + the selected item (lower) and every
        // row between was scanned (device 2026-11-19). render() now records, PER ROW, the column
        // span the UI actually drew; we scan ONLY those rows and skip the clean rows in between.
        // A full safety sweep every 16th present re-scans everything to heal any under-report.
        static unsigned presentN = 0; presentN++;
        bool sweep = firstFrame_ || (presentN & 15) == 0;   // full safety sweep every 16th present
        (void)dr;
        struct timeval pt0; gettimeofday(&pt0, 0);
        int dirtyRows = 0; long dirtyPix = 0;
        for (int y = 0; y < H; y++) {
            int cx0 = 0, cx1 = W - 1;
            if (!sweep && !c.rowDirty(y, &cx0, &cx1)) continue;    // UI says this row is clean
            const u8 *sp = src + (size_t)y * rowBytes;
            u8 *shr = shadow_ ? shadow_ + (size_t)y * rowBytes : 0;
            // Narrow the UI-dirty span [cx0,cx1] to the pixels that ACTUALLY changed vs shadow,
            // so we push the minimum to the uncached OSD scanout (convRow into a cached line, then
            // one memcpy burst of just the span).
            int x0 = cx0, x1 = cx1;
            if (shr && !firstFrame_) {
                const unsigned *s32 = (const unsigned *)sp;
                const unsigned *h32 = (const unsigned *)shr;
                while (x0 <= x1 && s32[x0] == h32[x0]) x0++;
                if (x0 > x1) continue;                             // marked dirty but pixels unchanged
                while (x1 > x0 && s32[x1] == h32[x1]) x1--;
            }
            int span = x1 - x0 + 1;
            if (span <= 0) continue;
            if (shr) memcpy(shr + (size_t)x0 * 4, sp + (size_t)x0 * 4, (size_t)span * 4);
            if (line_) {
                convRow(line_, sp + (size_t)x0 * 4, span);
                memcpy((void *)(dst + (size_t)y * dstRowB + (size_t)x0 * 2), line_, (size_t)span * 2);
            } else {
                convRow((volatile u16 *)(dst + (size_t)y * dstRowB + (size_t)x0 * 2), sp + (size_t)x0 * 4, span);
            }
            dirtyRows++; dirtyPix += span;
        }
        firstFrame_ = false;
        // Drain the write-combining buffer so the continuous ch53 scanout observes the new pixels
        // (required for /dev/stream write-buffered mapping; harmless for /dev/mem). RE: docs/RE/display/0006.
        if (dirtyRows > 0) __sync_synchronize();
        // Slow-present instrument (issue 1/4): cost = uncached OSD writes; log presents over 8ms
        // (rate-limited ~2/s) with the written pixel count so the stutter is measurable on device.
        if (dirtyRows > 0) {
            struct timeval pt1; gettimeofday(&pt1, 0);
            long us = (pt1.tv_sec - pt0.tv_sec) * 1000000L + (pt1.tv_usec - pt0.tv_usec);
            if (us > 8000) {
                static long lastSlowMs = 0;
                long nowMs = pt1.tv_sec * 1000L + pt1.tv_usec / 1000;
                if (nowMs - lastSlowMs > 500) { plog("vdf: present SLOW %ldus rows=%d/%d pix=%ld", us, dirtyRows, H, dirtyPix); lastSlowMs = nowMs; }
            }
        }
        // Detour-swap: the installed hook (my_vdfSend) runs on the stock submit thread
        // and swaps the panel-OSD record's phys to our buffer above. Path A instead
        // submits our own record directly at render rate.
        if (patha_) sendDirect(0);
        rateTick();
        (void)loggedOk_; (void)loggedErr_;
    }

    void shutdown() {
        if (patha_) teardownPathA();   // release GRM bitmaps + RESTORE stock mgr+100 (reversible)
        vh_uninstall();                // no-op if Path A (hook never installed)
        for (int i = 0; i < 2; i++) {
            if (bufMap_[i]) {
                ::munmap((void *)(((unsigned long)bufMap_[i]) & ~0xfffUL), mapLen_);
                bufMap_[i] = 0;
            }
        }
        if (mfd_ >= 0) { ::close(mfd_); mfd_ = -1; }
        if (shadow_) { free(shadow_); shadow_ = 0; }
        if (line_)   { free(line_);   line_   = 0; }
        ok_ = false;
    }
    const char *name() const { return "vdf-inproc"; }

private:
    a5kinproc::Engine *eng_;
    unsigned long lo_;
    unsigned osdPhys_;           // buffer-0 phys (also the g_osdBufPhys we log)
    unsigned bufPhys_[2];        // our two own grm_sens_work OSD buffers (phys)
    volatile u8 *bufMap_[2];     // their /dev/mem mappings (offset-corrected)
    unsigned long mapLen_;       // per-buffer mapped length (same for both)
    int mfd_;
    int nbuf_;                   // 2 = double buffered, 1 = single-buffer fallback
    int back_;                   // render-target index (the buffer NOT published)
    u8 *shadow_;
    u16 *line_;                  // cached 16bpp scratch row (convert here, burst-copy to uncached dst)
    bool firstFrame_;
    bool ok_;
    bool loggedErr_;
    bool loggedOk_;
    // ---- Path A (opt-in direct full-rate present) ----
    bool     patha_;             // A5K_VDF_PATHA / marker: drive vdf_send_update ourselves
    bool     mgrDisabled_;       // we wrote UpdateManager+100=1 (restore on shutdown)
    void    *mgr_;               // UpdateManager singleton (fbc_mgr_get)
    unsigned mgrSaveWord_;       // saved UpdateManager+100 dword
    int      pathaBmps_;         // number of persistent GRM bitmaps/records created
    unsigned recBmpOut_[2];      // bmpOut obj per buffer (for BMP_Release)
    unsigned char rec_[2][32];   // persistent 32B OSD record per buffer
    // present-rate logging
    struct timeval rtLast_;
    unsigned rtFrames_;
    unsigned rtSubmit0_;
    bool     suspended_;         // hand-to-stock: our present stepped back (one-time transition)
};

// ===========================================================================
// FbcDisplay -- present uicore's canvas through the GENERAL stock OSD present
// path: dmpNative::FbContainer panel element -> gen_struct_for_update ->
// UpdateManager::send (the GPU-composited OSD path the stock UI actually uses,
// RE_UI_BRINGUP: appFw UpdateManager -> OSDPin -> vdf_if_port_send_update ->
// VDF OSD pin 0x59000E -> ch53).  Unlike sub_5D99D0 (SenserIf/grm_sens_work),
// this is the path with FbContainer as the GPU render target.
//
// Mechanism (RE re/ida/libObj.so.c, sub_5DD33C flush):
//   container = fbContainer_get()          (0x5DB1F4, dword_EEEE00)
//   element   = container + 88             (panel OSD element; +200 = line/HDMI)
//   phys      = *(element + 104)           (GPU-composited OSD buffer, PHYSICAL)
//   gen_struct_for_update(element, rec, &n)  (0x5DD190) builds an INTERMEDIATE
//     record {geom, bpp@+12, phys@+16, aux@+24, bmp@+28} -- NOTE: different ABI
//     from sub_5D99D0's final vdf record (phys@+20, flags0x103@+16); the gen
//     record is consumed+reformatted by UpdateManager::send, not vdf directly.
//   UpdateManager::send(mgr, rec, n)       (0x5DCC1C) does the ack-accounted
//     submit (MSGID_PIN_SEND_COMP).  mgr = mgr_get() (0x5D695C, dword_EEE9E0).
// We fill the element's buffer (phys) with our 16bpp canvas, then gen+send.
//
// ENGAGE CONDITION: the panel element must have a BOUND buffer (phys != 0) and a
// valid current BMP, i.e. the stock compositor must be actively producing panel
// OSD frames (live-view / active sensor). Emulator measurement (2026-11-17, no
// live-view: AIF ch51 YUV start=0): container+192 (panel element phys) stays 0
// and panel isdirty=0 -> the element is UNBOUND, and gen_struct_for_update
// _assert_fails->abort on the invalid BMP. So by DEFAULT, if phys==0 at init,
// FbcDisplay falls back to the software OSD blit (zero regression, no crash,
// brick risk 0). On the real device with live-view the element binds and this
// engages. A5K_UI_FBC selects it; A5K_FBC_FORCE=1 lets the emulator attempt the
// gen+send even with phys==0 (known to abort -- disposable RE only).
class FbcDisplay : public Display {
public:
    explicit FbcDisplay(a5kinproc::Engine *eng)
        : eng_(eng), lo_(0), container_(0), element_(0), mgr_(0),
          mfd_(-1), mapPhys_(0), mapBase_(0), mapLen_(0), buf_(0),
          shadow_(0), firstFrame_(true), ok_(false), loggedErr_(false), loggedOk_(false),
          htsWas_(false) {}

    typedef void *(*fn_get)();
    typedef int   (*fn_gen)(void *, void *, int *);
    typedef void *(*fn_send)(void *, void *, int);
    void *T(unsigned off) { return (void *)((lo_ + off) | 1); }

    unsigned elemPhys() {
        return element_ ? *(volatile unsigned *)(element_ + a5kinproc::FBC_ELEM_PHYS) : 0;
    }

    bool init() {
        if (!eng_ || !eng_->ok()) { plog("fbc: engine unresolved"); return false; }
        lo_ = eng_->libObj();
        fn_get fbget  = (fn_get)T(a5kinproc::OFF_fbContainer_get);
        fn_get mgrget = (fn_get)T(a5kinproc::OFF_fbc_mgr_get);
        container_ = (unsigned long)fbget();
        mgr_       = mgrget();
        if (!container_ || !mgr_) { plog("fbc: container/mgr null (%lx/%p) -> fallback", container_, mgr_); return false; }
        element_ = container_ + a5kinproc::FBC_ELEM_PANEL;
        unsigned phys = elemPhys();
        plog("fbc: container=0x%lx element=0x%lx mgr=%p panelPhys=0x%08x", container_, element_, mgr_, phys);
        if (!phys && !env_int("A5K_FBC_FORCE", 0)) {
            plog("fbc: panel element UNBOUND (phys=0, no stock compositor this boot) -> software fallback.");
            plog("fbc:   (engages on real-device/live-view when the element binds; A5K_FBC_FORCE=1 to attempt anyway)");
            return false;
        }
        ok_ = true;
        return true;
    }

    // Fill the panel element's current buffer (phys) with our 16bpp canvas, then
    // drive the stock gen+send. phys can rotate between buffers, so resolve it per
    // frame and (re)map on change.
    void present(const Canvas &c, const Rect &) {
        if (!ok_) return;
        // Hand-to-stock: do NOT drive gen+send (that writes the stock panel element
        // buffer) while a stock view owns the panel. Step back; repaint on resume.
        if (g_hts.active) { htsWas_ = true; return; }
        if (htsWas_) { htsWas_ = false; firstFrame_ = true; }
        const int W = c.width(), H = c.height();
        const u8 *src = c.pixels();
        const size_t rowBytes = (size_t)W * 4;
        unsigned phys = elemPhys();
        if (!phys) {                      // momentarily unbound; skip this frame
            if (!loggedErr_) { plog("fbc: present: phys=0 (element unbound) -- skipping until it binds"); loggedErr_ = true; }
            return;
        }
        if (phys != mapPhys_) {           // (re)map the active buffer
            if (buf_) { ::munmap((void *)mapBase_, mapLen_); buf_ = 0; }
            size_t len = (size_t)W * H * 2; len = (len + 0xfffUL) & ~0xfffUL;
            unsigned long base = phys & ~0xfffUL;
            if (mfd_ < 0) mfd_ = ::open("/dev/mem", O_RDWR);
            if (mfd_ < 0) { plog("fbc: /dev/mem open failed"); ok_ = false; return; }
            void *m = ::mmap(0, len, PROT_READ | PROT_WRITE, MAP_SHARED, mfd_, base);
            if (m == MAP_FAILED) { plog("fbc: mmap phys 0x%08x failed", phys); return; }
            mapBase_ = (unsigned long)m; mapLen_ = len; mapPhys_ = phys;
            buf_ = (volatile u8 *)((unsigned long)m + (phys & 0xfffUL));
            firstFrame_ = true;
        }
        if (!shadow_) { shadow_ = (u8 *)malloc((size_t)W * H * 4); firstFrame_ = true; }
        // RGBA8888 -> RGBA4444 (same packing as display_osd / the 16bpp OSD plane),
        // dirty-row skip.
        for (int y = 0; y < H; y++) {
            const u8 *sp = src + (size_t)y * rowBytes;
            u8 *shr = shadow_ ? shadow_ + (size_t)y * rowBytes : 0;
            if (shr && !firstFrame_ && memcmp(shr, sp, rowBytes) == 0) continue;
            if (shr) memcpy(shr, sp, rowBytes);
            volatile u16 *d = (volatile u16 *)(buf_ + (size_t)y * (W * 2));
            const u8 *p = sp;
            for (int x = 0; x < W; x++, p += 4)
                d[x] = (u16)(((p[0] >> 4) << 12) | ((p[1] >> 4) << 8) | ((p[2] >> 4) << 4) | (p[3] >> 4));
        }
        firstFrame_ = false;

        // Drive the stock present: gen_struct_for_update(element) -> UpdateManager::send.
        fn_gen  gen  = (fn_gen) T(a5kinproc::OFF_fb_gen_update_rec);
        fn_send send = (fn_send)T(a5kinproc::OFF_fbc_mgr_send);
        unsigned char rec[64]; memset(rec, 0, sizeof rec);
        int n = 0;
        int r = gen((void *)element_, rec, &n);   // may abort if BMP invalid
        if (r != 0 || n <= 0) {
            if (!loggedErr_) { plog("fbc: gen ret=%d n=%d -- not submitting", r, n); loggedErr_ = true; }
            return;
        }
        if (!loggedOk_) {
            unsigned *rw = (unsigned *)rec;
            plog("fbc: gen ok n=%d rec: geom=%08x bpp=%08x phys=%08x aux=%08x bmp=%08x",
                 n, rw[0], rw[3], rw[4], rw[6], rw[7]);
            loggedOk_ = true;
        }
        send(mgr_, rec, n);
    }

    void shutdown() {
        if (buf_) { ::munmap((void *)mapBase_, mapLen_); buf_ = 0; }
        if (mfd_ >= 0) { ::close(mfd_); mfd_ = -1; }
        if (shadow_) { free(shadow_); shadow_ = 0; }
        ok_ = false;
    }
    const char *name() const { return "fbc-inproc"; }

private:
    a5kinproc::Engine *eng_;
    unsigned long lo_, container_, element_;
    void *mgr_;
    int mfd_;
    unsigned mapPhys_;
    unsigned long mapBase_, mapLen_;
    volatile u8 *buf_;
    u8 *shadow_;
    bool firstFrame_, ok_, loggedErr_, loggedOk_;
    bool htsWas_;                // hand-to-stock: was stepped back (force full repaint on resume)
};

// ---- RE probe: dump the stock FbContainer -> VDF OSD scanout state ----------
// READ-ONLY by default (just reads FbContainer memory + the live AIF ch53 DMA
// registers via /dev/mem). With /android/a5k/rprobe2 it ALSO calls the stock
// gen_struct_for_update to capture a live 32-byte update_osd_record (that call
// _assert_fails -> abort if the BMP is momentarily invalid, hence the extra
// gate). Persistent log (/setting/rprobe.log) so findings survive any reboot.
void doVdfProbe(a5kinproc::Engine *eng) {
    if (!eng || !eng->ok()) { plog("probe: engine not resolved"); return; }
    unsigned long lo = eng->libObj();
    plog("==== VDF/FbContainer probe ===="); plog("libObj=0x%lx", lo);

    // Live AIF DMA registers (RE_DISPLAY_SCANOUT: 0xF3441200 + 0x80*k, ch51..58;
    // ch53 = panel OSD = k=2 -> 0xF3441300; start +0x20, linebytes +0x24,
    // linecount +0x2C, pitch +0x48). ch51 = k=0 = panel YUV (live view).
    int mfd = ::open("/dev/mem", O_RDONLY);
    if (mfd >= 0) {
        volatile unsigned *r = (volatile unsigned *)::mmap(0, 0x1000, PROT_READ, MAP_SHARED, mfd, 0xF3441000UL);
        if (r != MAP_FAILED) {
            plog("AIF ch51(YUV) start=0x%08x linebytes=0x%x", r[0x220/4], r[0x224/4]);
            plog("AIF ch53(OSD) start=0x%08x linebytes=0x%x linecnt=0x%x pitch=0x%x",
                 r[0x320/4], r[0x324/4], r[0x32c/4], r[0x348/4]);
            ::munmap((void *)r, 0x1000);
        } else plog("probe: mmap AIF failed");
        ::close(mfd);
    } else plog("probe: /dev/mem open failed");

    // Stock "[SenserIf]" OSD work-buffer physical address (dword_EEED1C) -- the
    // phys that sub_5D99D0 feeds to BMP_CreateFromPhys + vdf_if_port_send_update.
    unsigned osdPhys = *(volatile unsigned *)(lo + a5kinproc::OFF_g_osdBufPhys);
    plog("g_osdBufPhys (dword_EEED1C) = 0x%08x", osdPhys);
    if (osdPhys) {
        int mf2 = ::open("/dev/mem", O_RDONLY);
        if (mf2 >= 0) {
            volatile unsigned *b = (volatile unsigned *)::mmap(0, 0x1000, PROT_READ, MAP_SHARED, mf2, osdPhys & ~0xfffUL);
            if (b != MAP_FAILED) { plog("  osdbuf[0..3] = %08x %08x %08x %08x", b[0], b[1], b[2], b[3]); ::munmap((void *)b, 0x1000); }
            ::close(mf2);
        }
    }

    // FbContainer singleton. The general stock OSD present path (sub_5DD33C)
    // treats container+88 as the PANEL OSD element and reads its GPU-composited
    // buffer phys at element+104 (= container+192). gen_struct_for_update takes
    // the ELEMENT (not the raw container).
    typedef void *(*fb_get)();
    fb_get get = (fb_get)((lo + a5kinproc::OFF_fbContainer_get) | 1);
    unsigned char *fb = (unsigned char *)get();
    plog("FbContainer=%p", fb);
    if (fb) {
        unsigned *w = (unsigned *)fb;
        for (int i = 0; i < 56; i += 4)   // dump through +220 (covers both elements)
            plog("  fb[+%3d] %08x %08x %08x %08x", i*4, w[i], w[i+1], w[i+2], w[i+3]);
        unsigned panelElem = (unsigned)(uintptr_t)(fb + a5kinproc::FBC_ELEM_PANEL);
        unsigned lineElem  = (unsigned)(uintptr_t)(fb + 200);
        unsigned panelPhys = *(volatile unsigned *)(fb + a5kinproc::FBC_ELEM_PANEL + a5kinproc::FBC_ELEM_PHYS);
        unsigned linePhys  = *(volatile unsigned *)(fb + 200 + a5kinproc::FBC_ELEM_PHYS);
        plog("  panel element=0x%08x  phys(elem+104)=0x%08x", panelElem, panelPhys);
        plog("  line  element=0x%08x  phys(elem+104)=0x%08x", lineElem, linePhys);
        // is-dirty gate state for each element
        typedef int (*dirtyf)(void *);
        dirtyf isdirty = (dirtyf)((lo + a5kinproc::OFF_fbc_elem_isdirty) | 1);
        plog("  panel isdirty=%d line isdirty=%d",
             isdirty(fb + a5kinproc::FBC_ELEM_PANEL), isdirty(fb + 200));
        if (panelPhys) {
            int mf3 = ::open("/dev/mem", O_RDONLY);
            if (mf3 >= 0) {
                volatile unsigned *b = (volatile unsigned *)::mmap(0, 0x1000, PROT_READ, MAP_SHARED, mf3, panelPhys & ~0xfffUL);
                if (b != MAP_FAILED) { plog("  panelbuf[0..3]=%08x %08x %08x %08x", b[0], b[1], b[2], b[3]); ::munmap((void *)b, 0x1000); }
                ::close(mf3);
            }
        }
    }

    if ((::access("/android/a5k/rprobe2", F_OK) == 0 || env_int("A5K_UI_PROBE", 0) >= 2) && fb) {
        plog("probe2: gen_struct_for_update(panel element=container+88) (may abort)...");
        typedef int (*genf)(void *, void *, int *);
        genf gen = (genf)((lo + a5kinproc::OFF_fb_gen_update_rec) | 1);
        unsigned char rec[64]; memset(rec, 0, sizeof rec);
        int n = 0;
        int rr = gen(fb + a5kinproc::FBC_ELEM_PANEL, rec, &n);
        plog("probe2: gen ret=%d n=%d  (rec+12=bpp, rec+16=phys, rec+28=bmp)", rr, n);
        unsigned *rw = (unsigned *)rec;
        for (int i = 0; i < 8; i++) plog("  rec[+%2d] = 0x%08x", i*4, rw[i]);
    }
    plog("==== probe done ====");
}

} // namespace

namespace a5kinproc {
uicore::Display *createRigelDisplay(Engine *eng) { return new RigelDisplay(eng); }
uicore::Display *createVdfDisplay(Engine *eng) { return new VdfDisplay(eng); }
uicore::Display *createFbcDisplay(Engine *eng) { return new FbcDisplay(eng); }
void rigelVdfProbe(Engine *eng) { doVdfProbe(eng); }

// ---- HAND-TO-STOCK manager (see the HandToStock comment block above) ---------
// Defined in input_createevent.cpp: force the input hooks to pass keys to stock.
void handToStock_forceReleaseInput();

// Is our overlay currently stepped back for a stock view? (read by present() and
// by the input hook / poll()).
bool handToStock_active() { return g_hts.active != 0; }

// Begin: an action opened a stock view. Suspend OUR present (present() + my_vdfSend
// react to g_hts.active) and release input to stock immediately. Snapshot the
// current-view global for the resume poll. RAM-only, reversible, brick-0.
void handToStock_begin(Engine *eng) {
    if (!eng || !eng->ok()) { uicore::logf("hts: begin skipped (engine unresolved)"); return; }
    g_hts.eng         = eng;
    g_hts.curViewAddr = eng->libObj() + OFF_g_currentView;
    g_hts.preOpen     = *(volatile unsigned *)g_hts.curViewAddr;
    g_hts.sawChange   = 0;
    g_hts.startMs     = hts_mono_ms();
    __sync_synchronize();
    g_hts.active = 1;                    // present()/my_vdfSend now step back
    handToStock_forceReleaseInput();     // keys to stock now (don't wait for poll())
    uicore::logf("hts: BEGIN preOpenView=0x%08x (suspend present + release input)", g_hts.preOpen);
    plog("hts: BEGIN preOpenView=0x%08x", g_hts.preOpen);
}

// Poll (called every frame from input poll(), which ALWAYS runs -- unlike present).
// Resume when the stock view closed (current-view changed then reverted to preOpen)
// or the fail-safe timeout fires. Clearing g_hts.active makes present() re-engage
// (one-time exit transition) and poll() re-grab input next frame.
void handToStock_poll() {
    if (!g_hts.active) return;
    if (g_hts.passthrough) return;      // indefinite stock-shooting: only stockShooting_end() resumes
    long age = hts_mono_ms() - g_hts.startMs;
    unsigned cur = g_hts.curViewAddr ? *(volatile unsigned *)g_hts.curViewAddr : g_hts.preOpen;
    if (cur != g_hts.preOpen) g_hts.sawChange = 1;           // stock view actually came up
    bool reverted = g_hts.sawChange && (cur == g_hts.preOpen);
    bool timedout = age >= HTS_TIMEOUT_MS;
    if (reverted || timedout) {
        g_hts.active = 0;
        __sync_synchronize();
        uicore::logf("hts: RESUME (%s) curView=0x%08x age=%ldms -> restore present + input",
                     reverted ? "view-closed" : "timeout", cur, age);
        plog("hts: RESUME (%s) curView=0x%08x age=%ldms", reverted ? "view-closed" : "timeout", cur, age);
    }
}

// Force resume (teardown / fail-safe). Safe to call when inactive.
void handToStock_end() {
    if (g_hts.active) {
        g_hts.active = 0;
        g_hts.passthrough = 0;
        __sync_synchronize();
        uicore::logf("hts: END (forced resume)");
    }
}

// ---- STOCK-SHOOTING PASSTHROUGH (route-b: shooting = Sony's stock OSD) --------
// Reuses the hand-to-stock present-gating (every present()/my_vdfSend checks g_hts.active,
// so our overlay steps back and the stock OSD scans out) + input release, but marks it
// `passthrough` so handToStock_poll() never auto-resumes (unlike an openView, which resumes
// on view-close/timeout). The app calls begin() at the shooting root and end() when our MENU
// opens. Brick-0: RAM-only, fully reversible; if our worker dies here the panel + input simply
// stay with the stock camera (the safe state).
bool stockShooting_active() { return g_hts.passthrough != 0; }
void stockShooting_begin(Engine *eng) {
    g_hts.eng = eng;
    g_hts.passthrough = 1;
    __sync_synchronize();
    g_hts.active = 1;                    // present()/my_vdfSend step back -> stock OSD shows
    handToStock_forceReleaseInput();     // keys to the stock camera now
    uicore::logf("hts: STOCK-SHOOTING begin (stock OSD + input; our overlay dormant)");
}
void stockShooting_end() {
    if (!g_hts.passthrough) return;
    g_hts.passthrough = 0;
    g_hts.active = 0;                     // re-engage our present + re-grab input next poll
    __sync_synchronize();
    uicore::logf("hts: STOCK-SHOOTING end (engage our UI)");
}
}
