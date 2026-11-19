// a5000 in-process engine glue (route (b), TRACKB_INPROCESS.md §10-§18).
//
// This header is the ONLY place that knows im.elf internals: the per-boot load
// bases of libObj.so / viewUnified2.so (resolved live from /proc/self/maps) and
// the fixed vaddr offsets of the stock in-process APIs we reuse. Every function
// is Thumb (libObj/viewUnified2 FUNC symbols have LSB=1), so a resolved call
// target is (base + vaddr) | 1.
//
// All addresses are empirically proven in the emulator (see the cited sections);
// this file re-derives nothing. No platform header leaks into uicore core.
#pragma once
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <fcntl.h>
#include <unistd.h>
#include <signal.h>
#include <sys/mman.h>
#include "uicore/log.h"   // uicore::logf (SdcomClient diagnostics)

namespace a5kinproc {

// ---- fixed vaddr offsets (Thumb) -----------------------------------------
// libObj.so
enum {
    OFF_getModelInfo       = 0x39485C,  // §13.1 viewManagerIf::getModelInfo (global singleton)
    OFF_requestModelExecute= 0x3948E0,  // §13.1 viewManagerIf::requestModelExecute (global singleton)
    OFF_ParamList_ctor     = 0x70912E,  // §13.2 ParamList::ParamList (8 B object)
    OFF_ParamList_dtor     = 0x7091F8,
    OFF_ParamList_add      = 0x7095D6,  // ParamList::add(id, ParamBase*)
    OFF_drawGate_66D442    = 0x66D442   // §14.3 hook 2b: dirty-count test -> return 0 suppresses stock draw
};
// libObj.so -- Rigel WindowClient + ws_hal (di_hal.cpp) in-process backend
// (display_rigel.cpp). All Thumb FUNCs -> call target = (libObj + off) | 1.
// Pinned from re/ida/libObj.so.c (markers "// ---- <addr> sub_X"); every entry
// cross-checked against the rigel.cpp / WindowClient.cpp / di_hal.cpp asserts.
// See display_rigel.cpp's header for the (important) in-proc feasibility finding.
enum {
    // ---- Rigel client (rigel.cpp / WindowClient.cpp) ----
    OFF_rglCreateWindow    = 0x67796C,  // (factory, name, w, h, a5, a6, nbuf:u8) -> WindowClient* (rigel.cpp:42/43)
    OFF_rglCreateWindow2   = 0x6778CC,  // (..., nbuf:u8, mode:int) via createNativeWindow2 (rigel.cpp:68/69)
    OFF_createWindowClient = 0x6780AC,  // WindowClientFactory::createWindowClient() -> op new(0x1C)+ctor
    OFF_WC_createNativeWin = 0x678358,  // WindowClient::createNativeWindow(this,factory,name,w,h,a5,a6,nbuf)
    OFF_WC_showWindow      = 0x677F3C,  // (client, status) : msg+8=status, sendMessage(1)
    OFF_WC_post            = 0x678120,  // (client, frontIdx) : dirty/front, sendMessage(6)
    OFF_WC_hideWindow      = 0x6781F0,  // (client, 0) : sendMessage(2)
    OFF_WC_destroyWindow   = 0x678290,  // (client) : sendMessage(32) + dtor
    // ---- ws_hal (di_hal.cpp) : server's OWN buffer HAL, statically in libObj ----
    OFF_hal_create         = 0x6D287C,  // hal_create() -> 0 ok (server already called it at GraphicsServer::Init)
    OFF_ws_hal_alloc       = 0x6D2AB4,  // (w,h,fmt,usage, WSImageBuffer* out[5], uint* out2) -> 0 ok
    OFF_ws_hal_lock        = 0x6D2D44,  // (buf, 256, 0, 0, w, h, void** outptr) -> 0 ok
    OFF_ws_hal_unlock      = 0x6D2DD4,  // (buf) -> 0 ok
    OFF_ws_hal_free        = 0x6D2C50,  // (buf)
    // ---- dmpNative FbContainer -> av-cam VDF OSD pin (the stock GPU/VDF present
    // path: GPU renders FbContainer buffer -> gen_struct_for_update builds a
    // 32-byte update_osd_record (buffer phys at rec+16) -> vdf_if_port_send_update
    // hands it to av-cam VDF, which DMAs it out as the panel OSD plane, AIF ch53.
    // Reusing this is the "present via VDF" path -- no new Rigel window needed).
    OFF_fbContainer_get    = 0x5DB1F4,  // dmpNative::FbContainer singleton getter -> FbContainer* (dword_EEEE00)
    OFF_fb_gen_update_rec  = 0x5DD190,  // gen_struct_for_update(ELEMENT=container+88, rec[32], int* n) ; rec+16=elem+104 phys, rec+12=bpp, rec+28=bmp
    OFF_vdf_send_update    = 0x5DC3D4,  // vdf_if_port_send_update(update_osd_record* recs, int count) -- RAW (no ack accounting)
    // ---- the GENERAL stock OSD present path (sub_5DD33C): for panel element
    // (container+88) and line element (container+200), gen_struct_for_update ->
    // UpdateManager::send (sub_5DCC1C, with the MSGID_PIN_SEND_COMP ack path, so
    // SAFER than raw vdf_if_port_send_update). This is how the stock UI actually
    // presents the GPU-composited OSD. We reuse it from FbcDisplay.
    OFF_fbc_mgr_get        = 0x5D695C,  // UpdateManager singleton getter -> mgr* (dword_EEE9E0)
    OFF_fbc_mgr_send       = 0x5DCC1C,  // UpdateManager::send(mgr, update_osd_record recs[], int count) ; count clamped to 2
    // Path A (display_rigel.cpp VdfDisplay): set UpdateManager +100 (mgr[25]) = 1 to
    // DISABLE the stock present ping-pong, so sub_5DCAB4's change-gate early-outs and
    // the stock never submits the panel OSD record (present_pipeline_full.md §2.2).
    // We drive vdf_if_port_send_update ourselves at render rate instead. Reversible
    // (save/restore the word on shutdown).
    UPDATEMGR_PRESENT_DISABLE_WORD = 100,
    // Force-submit-once flag (mgr[24]=+96): sub_5DCAB4's change-gate returns "submit"
    // when this is 1 (then clears it to 0), BEFORE the record +28 content compare. The
    // stock resend path (sub_5DD2E4 -> sub_5DC928) sets it the same way, so writing it
    // from our worker is stock-sanctioned (a plain dword flag). DOCUMENTED FINDING, NOT
    // USED: emulator 2026-11-19 showed the stock OSD walk fires only ~1/s when our draw
    // is suppressed, so poking this did NOT raise the submit rate ("Case B"). Full-rate
    // present instead comes from the continuous ch53 scanout of our single latched buffer
    // (display_rigel.cpp single-buffer in-place) -- no per-frame submit needed. Kept here
    // only as the RE record; a real-device session could retest if its GLES commit rate
    // is higher.
    UPDATEMGR_FORCE_SUBMIT_WORD = 96,
    OFF_fbc_elem_isdirty   = 0x5DCD80,  // bool element_has_update(element) ; stock flush gates gen on this
    FBC_ELEM_PANEL         = 88,        // container + 88 = panel OSD element (ch53)
    FBC_ELEM_PHYS          = 104,       // element + 104 = GPU-composited OSD buffer PHYSICAL addr (= rec+16)
    // ---- stock "[SenserIf]" OSD submit sub_5D99D0 decomposed (the exact, proven
    // recipe to push a 16bpp 640x480 buffer to the panel OSD plane via VDF):
    //   BMP_CreateFromPhys(640,480,fmt=2,yuv=0, phys, stride=640, &dest, &bmpOut,0,0)
    //   record{ dest[+0..15], flags=0x103(+16), addr=phys(+20), 4(+24), bmpOut(+28) }
    //   vdf_if_port_send_update(record, 1);  BMP_Release(bmpOut)
    // phys comes from the libObj global dword_EEED1C (stock OSD work buffer, 16bpp).
    OFF_BMP_CreateFromPhys = 0x589A00,  // (w16,h16,fmt,yuv,phys,stride16,&dest64,&bmpOut,0,0) -> 0 ok
    OFF_BMP_Release        = 0x589E18,  // (bmpHandle) -> 0 ok
    OFF_BMP_fmt2Bpp        = 0x589F48,  // BMP_PixelFormatToBytesPerPixel(fmt,&out)
    OFF_g_osdBufPhys       = 0xEEED1C,  // dword_EEED1C: stock OSD work buffer PHYSICAL address (global)
    // ---- own-buffer path: utilGetMemory allocates the stock "grm_sens_work" MA
    // buffer (640x480x16bpp) and stores its PHYSICAL addr in dword_EEED1C. Its a1
    // is unused (body: s[0]="grm_sens_work"; s[1]=a2=type; MA_CreateSpace ->
    // MA_Alloc -> MA_LogicalToPhysical(&dword_EEED1C)). Calling it ourselves binds
    // the buffer regardless of whether the SenserIf start path ran, so the
    // sub_5D99D0 VDF present works boot-state-independently (no reliance on an
    // unbound stock buffer). asserts on MA error -> we gate the call.
    OFF_utilGetMemory      = 0x5D9AEC,  // dmpNative::SenserCommunicatorBase::utilGetMemory(ignored, type) -> sets dword_EEED1C
    OFF_g_osdAllocFlag     = 0xEEED20,  // byte_EEED20: 1 after utilGetMemory succeeded
    GRM_SENS_WORK_TYPE     = 268054     // the 'type' arg the stock passes (0x41716) -> 640x480x16bpp grm_sens_work
};
// viewUnified2.so
enum {
    OFF_ParamList_get      = 0x0EA4B0,  // §16.2 ParamList::get(list, id) -> ParamBase*; value at +12
    OFF_IntParam_ctor      = 0x0EA468,  // §16.1 IntParam ctor(obj, value)
    OFF_CVPDM_guard        = 0x47FFF4,  // §18.1 CmnViewProcessDataMgr _cxa_guard
    OFF_CVPDM_obj          = 0x47FFF8,  // §18.1 CmnViewProcessDataMgr global singleton
    CVPDM_RD_VOFF          = 12,        // vtable[3]: read (m(obj,1,&out))
    CVPDM_WR_VOFF          = 56         // vtable[14]: write (m(obj,1,val))
};

// ---- OSD scanout (AIF ch53, §10.1) ---------------------------------------
static const unsigned long OSD_PHYS   = 0x8f758000UL; // double buffer base
static const unsigned      OSD_STRIDE = 0x500;        // 1280 bytes/line (640px * 2B)
static const unsigned      OSD_LINES  = 480;
static const unsigned      OSD_FB_BYTES = OSD_STRIDE * OSD_LINES; // 0x96000; buffers are back-to-back
static const unsigned      OSD_NBUF   = 2;
static const unsigned long AIF_PHYS   = 0xF3440000UL;
static const unsigned      AIF_OSD_ON = 0x144;        // bit1 = panel OSD on
static const unsigned      OSD_ON_BIT = 0x2;

// ---- base resolution ------------------------------------------------------
// Lowest r-xp mapping start of `name` in this process. Per-boot (ASLR-ish).
inline unsigned long find_base(const char *name, unsigned long *endOut = 0) {
    int fd = open("/proc/self/maps", O_RDONLY);
    if (fd < 0) return 0;
    static char buf[1 << 20];
    int off = 0, r;
    while (off < (int)sizeof buf - 1 && (r = read(fd, buf + off, sizeof buf - 1 - off)) > 0) off += r;
    close(fd);
    if (off <= 0) return 0;
    buf[off] = 0;
    unsigned long base = 0, end = 0;
    for (char *p = buf; p; ) {
        char *nl = strchr(p, '\n');
        if (nl) *nl = 0;
        if (strstr(p, name)) {
            if (strstr(p, "r-xp")) { unsigned long s = strtoul(p, 0, 16); if (!base || s < base) base = s; }
            char *d = strchr(p, '-'); unsigned long e = d ? strtoul(d + 1, 0, 16) : 0; if (e > end) end = e;
        }
        if (!nl) break;
        p = nl + 1;
    }
    if (endOut) *endOut = end;
    return base;
}

// ---- Engine: resolved handles + typed trampolines -------------------------
class Engine {
public:
    Engine() : libObj_(0), vu_(0), vuEnd_(0), ok_(false),
               gatePatched_(false), gateTarget_(0) {}

    bool resolve() {
        libObj_ = find_base("libObj.so");
        vu_     = find_base("viewUnified2.so", &vuEnd_);
        ok_ = libObj_ && vu_;
        return ok_;
    }
    bool ok() const { return ok_; }
    unsigned long libObj() const { return libObj_; }
    unsigned long vu() const { return vu_; }

    // ---- READ (§13/§16) ---------------------------------------------------
    // getModelInfo("model/<name>", selector, out) then ParamList::get(out,id)->+12.
    // selector is a small int group number (§16.2). Returns false if unread.
    bool readModelParam(const char *model, unsigned long selector,
                        unsigned int paramId, int *valOut) {
        if (!ok_) return false;
        typedef void *(*pl_ctor)(void *);
        typedef int   (*gmi)(const char *, unsigned long, void *, void *, void *);
        typedef void *(*pl_get)(void *, unsigned int);
        pl_ctor ctor = (pl_ctor)((libObj_ + OFF_ParamList_ctor) | 1);
        gmi     g    = (gmi)    ((libObj_ + OFF_getModelInfo)   | 1);
        pl_get  get  = (pl_get) ((vu_     + OFF_ParamList_get)  | 1);
        unsigned char storage[0x20];
        memset(storage, 0, sizeof storage);
        ctor(storage);
        int ret = g(model, selector, storage, 0, 0);
        if (ret == 0) return false;
        void *p = get(storage, paramId);
        if (!p) return false;
        if (valOut) *valOut = *(int *)((char *)p + 12);
        return true;
    }

    // ---- 2-STEP READ: getModelInfo WITH an input ParamList ------------------
    // Some model params are only answered when the request carries an INPUT
    // ParamList {inParamId = inValue} (the stock getters build one on the stack
    // and getModelInfo fills the matching output param). Proven uses (viewUnified2):
    //   * battery %  : model/POWER sel 1, in {0x107 = (model/POWER sel 0, 0x105)},
    //                  out 0x10C (ACT_IconBattery sub_27D934 @0x27D934).
    //   * WB coltemp : @M00B sel 21, in {0x14D = mode}, out 0x150/0x14F.
    // getModelInfo is SYNCHRONOUS for a read (unlike requestModelExecute), so the
    // engine consumes the input during the call and the IntParam is freed right
    // after (malloc/free matched; no ParamList::~ so the out list -- which only
    // holds references into engine-owned storage -- is handled exactly as the
    // single-step readModelParam does). Returns false if unresolved. RAM-only.
    bool readModelParamIn(const char *model, unsigned long selector,
                          unsigned int inParamId, int inValue,
                          unsigned int outParamId, int *valOut) {
        if (!ok_ || !model) return false;
        typedef void *(*pl_ctor)(void *);
        typedef void *(*ip_ctor)(void *, int);
        typedef void  (*pl_add)(void *, unsigned long, void *);
        typedef int   (*gmi)(const char *, unsigned long, void *, void *, void *);
        typedef void *(*pl_get)(void *, unsigned int);
        pl_ctor ctor = (pl_ctor)((libObj_ + OFF_ParamList_ctor) | 1);
        pl_add  add  = (pl_add) ((libObj_ + OFF_ParamList_add)  | 1);
        ip_ctor ipc  = (ip_ctor)((vu_     + OFF_IntParam_ctor)  | 1);
        gmi     g    = (gmi)    ((libObj_ + OFF_getModelInfo)   | 1);
        pl_get  get  = (pl_get) ((vu_     + OFF_ParamList_get)  | 1);
        unsigned char outStore[0x20], inStore[0x20];
        memset(outStore, 0, sizeof outStore);
        memset(inStore, 0, sizeof inStore);
        ctor(outStore);
        ctor(inStore);
        void *ip = malloc(0x10);           // IntParam(inValue); freed after the sync call
        if (!ip) return false;
        ipc(ip, inValue);
        add(inStore, inParamId, ip);
        int ret = g(model, selector, outStore, inStore, 0);
        bool ok = false;
        if (ret != 0) {
            void *p = get(outStore, outParamId);
            if (p) { if (valOut) *valOut = *(int *)((char *)p + 12); ok = true; }
        }
        free(ip);
        return ok;
    }

    // ---- SET one int param through the stock enqueue path (§15.B/§16.1) -----
    // requestModelExecute("model/<name>", cmd, ParamList{ paramId = value }). Same ABI as the proven
    // set2_so.c: ParamList(8B) + IntParam(0x10B) + add + requestModelExecute (enqueue; the appFw pump
    // applies it on its own thread; the event owns the list, so it is NOT freed). Guarded: refuses if
    // the bases are unresolved. Returns the call's ret (0 = enqueued, as in §16.1), -1 = refused.
    int modelSet1(const char *model, unsigned long cmd, unsigned int paramId, int value) {
        if (!ok_ || !model) return -1;
        typedef void *(*pl_ctor)(void *);
        typedef void *(*ip_ctor)(void *, int);
        typedef void  (*pl_add)(void *, unsigned long, void *);
        typedef int   (*req_fn)(const char *, unsigned long, void *);
        pl_ctor ctor = (pl_ctor)((libObj_ + OFF_ParamList_ctor) | 1);
        pl_add  add  = (pl_add) ((libObj_ + OFF_ParamList_add)  | 1);
        ip_ctor ipc  = (ip_ctor)((vu_     + OFF_IntParam_ctor)  | 1);
        req_fn  req  = (req_fn) ((libObj_ + OFF_requestModelExecute) | 1);
        void *list = malloc(8);  if (!list) return -1;
        void *p    = malloc(0x10); if (!p) return -1;
        ctor(list);
        ipc(p, value);
        add(list, paramId, p);
        return req(model, cmd, list);
    }

    // ---- NON-SETTING menu actions (app-launch / stock action view / toggle) -----
    // Brick-0 CONTRACT: RAM/runtime only -- NONE of these ever write Backup/flash/NAND.

    // requestModelExecute(model, cmd) with an EMPTY ParamList = a param-less stock model-exec
    // (airplane/demo-style toggle, §0-B). Same proven enqueue ABI as modelSet1/exposureStep; the
    // event owns the list, so it is NOT freed. Returns the call's ret (0 = enqueued), -1 refused.
    // [device-seal] the exact `cmd` per toggle lives inside the (un-decompiled) view*.so; capture it
    // on device (SDCom/event dump around the stock menu toggle) before trusting a specific value.
    int modelExec0(const char *model, unsigned long cmd) {
        if (!ok_ || !model) return -1;
        typedef void *(*pl_ctor)(void *);
        typedef int   (*req_fn)(const char *, unsigned long, void *);
        pl_ctor ctor = (pl_ctor)((libObj_ + OFF_ParamList_ctor) | 1);
        req_fn  req  = (req_fn) ((libObj_ + OFF_requestModelExecute) | 1);
        void *list = malloc(8); if (!list) return -1;
        ctor(list);
        return req(model, cmd, list);
    }

    // Open a stock action VIEW ("view/FORMAT", "view/SELECT_FOLDER", "view/USBAUTO", "view/AREA_SET",
    // "view/KEYBOARD", "view/INITIAL_SETTING", ...) WITHOUT knowing the engine cmd (the view supplies
    // it internally, §0-A). Returns 0 if it dispatched, 1 = staged/device-seal.
    //
    // PINNED (static, libObj.so.c): we use viewManagerIf::openView @ 0x00394A1C -- NOT ViewBase::openView
    // @ 0x00394288. The ViewBase variant needs a live `ViewBase* this` we do not hold in-proc; the
    // viewManagerIf variant is the exact sibling of OFF_getModelInfo (0x39485C) and OFF_requestModelExecute
    // (0x3948E0) -- it pulls the ViewManager singleton from libObj globals (dword_EEDDD8 = ViewManager,
    // dword_EEDDE0 = current-view) inside sub_394944 and pushes an open-view Event (0x11012001) onto that
    // queue. `this` is only a fallback IdGenerator source-name and is OVERRIDDEN by the live current view
    // when one exists, so no real view object is required. ABI (0x394A1C):
    //     int viewManagerIf::openView(const char* caller /*this*/, const char* viewId, int arg)
    // Enqueueing an open-view Event is the SAME RAM/engine-queue class as requestModelExecute (already
    // trusted by modelSet1/modelExec0) -- brick-0 (nothing writes Backup/flash/NAND).
    // NOTE: route-b must still tear down its Rigel overlay so the opened stock view is visible; that is
    // handled by the caller/display layer, not here.
    static const unsigned long OFF_openView = 0x394A1C;  // libObj viewManagerIf::openView (cite above)
    int openStockView(const char *viewId, int arg = 0) {
        if (!ok_ || !viewId) return 1;
        if (OFF_openView == 0) return 1;            // unresolved -> device-seal, no blind call
        typedef int (*ov_fn)(const void *, const char *, int);
        ov_fn ov = (ov_fn)((libObj_ + OFF_openView) | 1);
        // Pass viewId as the fallback caller name (ignored when a live current view exists). The call
        // enqueues the open-view event; we report dispatched (0) regardless of its own ret, matching the
        // contract (0 = dispatched, 1 = device-seal) used by actions_inproc.cpp.
        ov((const void *)viewId, viewId, arg);
        return 0;
    }

    // ---- VIEW-STATE probes (issue 2/3: gate our OSD to shooting-only, diagnose reads) -----
    // Both read libObj globals and IGNORE `this` (pass libObj_ base, same trick as openStockView).
    // PINNED (re/ida/libObj.so.c): ScalarAppfwIF::IsViewModeDiadem @0x7098B8 = (dword_F13D84==0)
    // => native Sony UI foreground (shooting/playback/menu) returns !=0, Android-app foreground 0.
    // viewManagerIf::getCurrentFuncEventMode @0x394924 = sub_70A734(dword_EEDDD8) = the top VALID
    // view's mode code (enum shooting/playback/menu -- values being pinned by RE). It is the stock's
    // own getter (run constantly by the engine), so calling it is the same safety class as the engine.
    static const unsigned long OFF_IsViewModeDiadem        = 0x7098B8;
    static const unsigned long OFF_getCurrentFuncEventMode = 0x394924;
    // true = native Sony UI foreground; false = Android app. Fail-safe: unresolved -> true
    // (never hide our overlay / never lock the panel on a bad read).
    bool viewIsNative() {
        if (!ok_) return true;
        typedef int (*fn)(const void *);
        fn f = (fn)((libObj_ + OFF_IsViewModeDiadem) | 1);
        return f((const void *)libObj_) != 0;
    }
    // Top view's func-event-mode code (-1 = unresolved engine). Enum meaning pinned by RE.
    int currentFuncEventMode() {
        if (!ok_) return -1;
        typedef int (*fn)(const void *);
        fn f = (fn)((libObj_ + OFF_getCurrentFuncEventMode) | 1);
        return f((const void *)libObj_);
    }

    // ---- SET via global CmnViewProcessDataMgr vtable (§18, guarded) --------
    bool cvpdmConstructed() {
        if (!ok_) return false;
        unsigned int guard = *(volatile unsigned int *)(vu_ + OFF_CVPDM_guard);
        return (guard & 1) != 0;
    }
    bool inVu(unsigned long a) const { return a >= vu_ && a < vuEnd_; }

    // ---- generic CmnViewProcessDataMgr per-GROUP read/write (TRACKB §18 + §23) --
    // The stock Wrapper setters (viewUnified2) all drive the SAME global singleton
    // with vtable[14](obj, GROUP, internalValue) and read via vtable[3](obj, GROUP,
    // &out), GROUP differing per setting (aspect 1, image-quality 2, drive 7,
    // flash-mode 8, flash-comp 9, metering 22, dro/hdr 24, smile 31, ...). Group
    // round-trips are emulator-verified (grpscan/grpscan2). Same guards as aspect.
    bool cvpdmGroupRead(int group, int *out) {
        if (!ok_ || !cvpdmConstructed()) return false;
        void **obj = (void **)(vu_ + OFF_CVPDM_obj);
        unsigned long vt = (unsigned long)*obj;
        if (!inVu(vt)) return false;
        unsigned long m = *(unsigned long *)(vt + CVPDM_RD_VOFF);
        if (!inVu(m & ~1UL)) return false;
        typedef int (*rd)(void *, int, int *);
        int v = -1; int r = ((rd)m)(obj, group, &v);
        if (r == 0) return false;
        if (out) *out = v;
        return true;
    }
    // Returns the write method's ret (nonzero = accepted; 0 = engine refused, e.g.
    // the setting is inhibited in the current shooting mode -- stock parity).
    int cvpdmGroupWrite(int group, int val) {
        if (!ok_ || !cvpdmConstructed()) return 0;
        void **obj = (void **)(vu_ + OFF_CVPDM_obj);
        unsigned long vt = (unsigned long)*obj;
        if (!inVu(vt)) return 0;
        unsigned long m = *(unsigned long *)(vt + CVPDM_WR_VOFF);
        if (!inVu(m & ~1UL)) return 0;
        typedef int (*wr)(void *, int, int);
        return ((wr)m)(obj, group, val);
    }
    // aspect = group 1 (back-compat wrappers; §18).
    bool cvpdmRead(int *out)  { return cvpdmGroupRead(1, out); }
    bool cvpdmWrite(int val)  { return cvpdmGroupWrite(1, val) != 0; }

    // ---- stock-draw suppression (§14.3/§15 hook 2b, detach-reversible) -----
    bool suppressStock() {
        if (!ok_ || gatePatched_) return gatePatched_;
        gateTarget_ = libObj_ + OFF_drawGate_66D442;
        unsigned long page = gateTarget_ & ~0xfffUL;
        if (mprotect((void *)page, 0x2000, PROT_READ | PROT_WRITE | PROT_EXEC)) return false;
        unsigned char *t = (unsigned char *)gateTarget_;
        gateSave_[0]=t[0]; gateSave_[1]=t[1]; gateSave_[2]=t[2]; gateSave_[3]=t[3];
        t[0]=0x00; t[1]=0x20; t[2]=0x70; t[3]=0x47;   // movs r0,#0 ; bx lr
        __builtin___clear_cache((char *)gateTarget_, (char *)gateTarget_ + 4);
        mprotect((void *)page, 0x2000, PROT_READ | PROT_EXEC);
        gatePatched_ = true;
        return true;
    }
    void restoreStock() {
        if (!gatePatched_) return;
        unsigned long page = gateTarget_ & ~0xfffUL;
        if (mprotect((void *)page, 0x2000, PROT_READ | PROT_WRITE | PROT_EXEC) == 0) {
            unsigned char *t = (unsigned char *)gateTarget_;
            t[0]=gateSave_[0]; t[1]=gateSave_[1]; t[2]=gateSave_[2]; t[3]=gateSave_[3];
            __builtin___clear_cache((char *)gateTarget_, (char *)gateTarget_ + 4);
            mprotect((void *)page, 0x2000, PROT_READ | PROT_EXEC);
            gatePatched_ = false;
        }
    }
    bool suppressed() const { return gatePatched_; }

private:
    unsigned long libObj_, vu_, vuEnd_;
    bool ok_;
    bool gatePatched_;
    unsigned long gateTarget_;
    unsigned char gateSave_[4];
};

// ---- SDCom client (ScalarDaemon), resolved PURELY BY OFFSET (brick-0) --------
// Re-implementation of the in-proc SDCom apply path. The prior attempt (commit
// 2d8b92b, REVERTED at b8c0827) #include'd sdcom.h and declared a GLOBAL object of
// a `class : public SDComIPCListener`; that pulled UNDEFINED libsdcom dynamic
// symbols (SDComIPCListener vtable/ctor, SDComIPC::*, SDComMsgUtil::*) into
// a5kinproc.so via eager vtable relocations. Because autostart LD_PRELOADs the .so
// into EVERY boot process (busybox, mount, bootin.elf -- none of which have
// libsdcom), those processes died at load (`symbol lookup error`, rc=127) -> dead
// boot. This version takes ZERO link dependency on libsdcom:
//   * NO #include "sdcom.h", NO subclass of any libsdcom type.
//   * Every entry point is resolved the SAME way Engine resolves libObj/vu --
//     find_base("libsdcom.so") + fixed vaddr, cast to a function pointer, called
//     (Thumb -> |1). So a5kinproc.so keeps NEEDED = libpthread/libm/libc/ld-linux
//     and 0 undefined libsdcom symbols (CLAUDE.md §1 HARD REQUIREMENT).
//   * The IPC / IntParam / Listener objects are raw malloc()'d blocks of the
//     proven sizes (CLAUDE.md §10.9: IPC 0x44 -> 0x100 pad, IntParam 0x10,
//     Listener 0x20). Their ctors (by offset) install the real libsdcom vtables;
//     the listener's vptr is set to the real _ZTV16SDComIPCListener slot by offset,
//     so stray receive-thread callbacks hit the real (no-op) base methods.
//   * Resolution is LAZY -- it happens only inside the set path (never a global
//     ctor, never at load). The whole client is inert unless an SDCom-routed
//     setting is actually applied from the im.elf worker.
//
// Offsets are from the EXACT device binary (glibc /usr/lib/libsdcom.so, md5
// 1fd1081b90e77d4b35e4577d69068da6 == firmware nflasha15 copy), via
//   arm-linux-gnueabi-readelf -sW app/native/sysroot/lib/libsdcom.so
enum {
    OFF_sd_ipc_ctor      = 0x0c6eb, // _ZN8SDComIPCC1EP16SDComIPCListener (SDComIPC::SDComIPC(listener*))
    OFF_sd_connect       = 0x0d03d, // _ZN8SDComIPC7connectEjh (connect(queue, componentId))
    OFF_sd_disconnect    = 0x0cd7d, // _ZN8SDComIPC10disconnectEv
    OFF_sd_sendSync      = 0x0cc65, // _ZN8SDComIPC8sendSyncEP8SDComMsgPS1_i (sendSync(msg,&rep,timeoutSec))
    OFF_sd_newTxId       = 0x0bc45, // _ZN8SDComIPC22createNewTransactionIdEPj
    OFF_sd_newMessage    = 0x10cc9, // _ZN12SDComMsgUtil16createNewMessageEjhj (dst,type,txid) -> SDComMsg*
    OFF_sd_setMsgId      = 0x10ba5, // _ZN12SDComMsgUtil12setMessageIdEP8SDComMsgt
    OFF_sd_addParam      = 0x10be9, // _ZN12SDComMsgUtil8addParamEP8SDComMsgPK13SDComMsgParam (keeps a COPY)
    OFF_sd_freeMessage   = 0x10abd, // _ZN12SDComMsgUtil11freeMessageEP8SDComMsgb
    OFF_sd_intparam_ctor = 0x0fea5, // _ZN16SDComMsgIntParamC1Eti (SDComMsgIntParam(u16 id, int value))
    OFF_sd_vt_listener   = 0x1a260  // _ZTV16SDComIPCListener (vtable object; object vptr = +8)
};
static const unsigned long SD_SCALARDAEMON_QUEUE = 0x1540432UL; // ScalarDaemon recv queue
static const unsigned char SD_MSG_REQUEST        = 5;           // SDComHelper::sendMsg request type
static const unsigned      SD_COMPONENT          = 2;           // camera component (camera_sdcom.cpp)
static const unsigned short SD_MSG_SETPARAMS     = 0x1003;      // CameraSetParameters
static const int           SD_SETPARAMS_FIRST_ID = 52;          // leading Int param id (=0) per settings_readwrite_full.md

// SdcomClient result codes (honest; never faked).
enum {
    SR_OK      =  1,   // assembled + sendSync acked (rc==0)
    SR_REJECT  =  0,   // assembled + sent, daemon rc != 0 (engine refused)
    SR_NOCONN  = -1,   // libsdcom mapped + msg assembled, but connect failed (emulator/stock screen: -5)
    SR_NOLIB   = -2,   // libsdcom not mapped / resolve failed (standalone/host) -> caller: NO_ROUTE
    SR_DRYRUN  =  2    // SD_DRYRUN: assembled + logged, intentionally NOT sent
};

class SdcomClient {
public:
    SdcomClient() : base_(0), ipc_(0), listener_(0), connected_(false), dryrun_(false), txCtr_(1) {
        const char *d = getenv("SD_DRYRUN");
        dryrun_ = (d && d[0] && d[0] != '0');
    }

    bool dryrun() const { return dryrun_; }
    bool connected() const { return connected_; }

    // Brick-0 (CLAUDE.md §10.9 rule 3): called from the crash handler so an open
    // osal queue is not left dangling (which would hang the camera engine).
    void disconnect() {
        if (connected_ && ipc_ && base_) {
            typedef int (*fn)(void *);
            ((fn)((base_ + OFF_sd_disconnect) | 1))(ipc_);
        }
        connected_ = false;
    }

    // Apply ONE setting via 0x1003 CameraSetParameters { 52=0, paramId=value }.
    // RAM/runtime only, brick-safe (engine state, re-created on reboot). Returns an
    // SR_* code; the actual on-device send is [device-seal] (in-guest SDCom is
    // blocked, connect ret -5 off the stock app). Logs the assembled param list.
    int setParam(int paramId, int value) {
        if (!resolve()) { uicore::logf("sdcom: setParam %d=%d -> libsdcom not mapped (NO_ROUTE)", paramId, value); return SR_NOLIB; }
        if (!initIpc())  { uicore::logf("sdcom: setParam %d=%d -> IPC alloc/ctor failed", paramId, value); return SR_NOLIB; }

        // --- assemble 0x1003 { 52=0, paramId=value } (proven sdprobe wire format) ---
        unsigned tx = nextTx();
        typedef void *(*new_msg)(unsigned, unsigned char, unsigned);
        typedef void  (*set_id)(void *, unsigned short);
        typedef void  (*ip_ctor)(void *, unsigned short, int);
        typedef int   (*add_p)(void *, const void *);
        typedef void  (*free_msg)(void *, bool);
        void *msg = ((new_msg)((base_ + OFF_sd_newMessage) | 1))(SD_SCALARDAEMON_QUEUE, SD_MSG_REQUEST, tx);
        if (!msg) { uicore::logf("sdcom: createNewMessage failed"); return SR_NOLIB; }
        ((set_id)((base_ + OFF_sd_setMsgId) | 1))(msg, SD_MSG_SETPARAMS);

        ip_ctor ipc_ctor = (ip_ctor)((base_ + OFF_sd_intparam_ctor) | 1);
        add_p   add      = (add_p)  ((base_ + OFF_sd_addParam)      | 1);
        // Two Int params. addParam keeps its own COPY (sdprobe.cpp §write), so each
        // raw block is freed right after add -- no leak, matched malloc/free.
        int ids[2]  = { SD_SETPARAMS_FIRST_ID, paramId };
        int vals[2] = { 0,                     value   };
        int added   = 0;
        for (int i = 0; i < 2; i++) {
            void *p = malloc(0x10);            // sizeof(SDComMsgIntParam) == 0x10 (§10.9)
            if (!p) break;
            ipc_ctor(p, (unsigned short)ids[i], vals[i]);
            int r = add(msg, p);
            free(p);
            if (r >= 0) added++;
        }
        uicore::logf("sdcom: assembled msg 0x%04x tx=%u params=[%d=%d, %d=%d] added=%d",
             SD_MSG_SETPARAMS, tx, ids[0], vals[0], ids[1], vals[1], added);

        free_msg freeMsg = (free_msg)((base_ + OFF_sd_freeMessage) | 1);

        // SD_DRYRUN: proven assembly only, never touch the daemon queue.
        if (dryrun_) { freeMsg(msg, false); uicore::logf("sdcom: SD_DRYRUN -> not sent"); return SR_DRYRUN; }

        if (!ensureConnected()) { freeMsg(msg, false); uicore::logf("sdcom: not connected (connect rc!=0) -> NO_ROUTE [device-seal]"); return SR_NOCONN; }

        typedef int (*send_fn)(void *, void *, void **, int);
        void *rep = 0;
        int rc = ((send_fn)((base_ + OFF_sd_sendSync) | 1))(ipc_, msg, &rep, 5);
        if (rep) freeMsg(rep, false);
        freeMsg(msg, false);
        uicore::logf("sdcom: sendSync 0x%04x param %d=%d -> rc=%d", SD_MSG_SETPARAMS, paramId, value, rc);
        return rc == 0 ? SR_OK : SR_REJECT;
    }

private:
    bool resolve() {
        if (base_) return true;
        base_ = find_base("libsdcom.so");
        return base_ != 0;
    }
    // Construct the IPC + listener objects (ctor only -- NO connect, NO thread).
    bool initIpc() {
        if (ipc_) return true;
        if (!base_) return false;
        listener_ = malloc(0x20);               // small; only its vptr is read
        if (!listener_) return false;
        memset(listener_, 0, 0x20);
        // object vptr -> real _ZTV16SDComIPCListener first slot (skip 2-word RTTI header).
        *(unsigned long *)listener_ = (base_ + OFF_sd_vt_listener + 8);
        ipc_ = malloc(0x100);                    // real 0x44, padded (§10.9)
        if (!ipc_) { free(listener_); listener_ = 0; return false; }
        memset(ipc_, 0, 0x100);
        typedef void (*ctor)(void *, void *);
        ((ctor)((base_ + OFF_sd_ipc_ctor) | 1))(ipc_, listener_);
        return true;
    }
    bool ensureConnected() {
        if (connected_) return true;
        if (!ipc_) return false;
        typedef int (*fn)(void *, unsigned, unsigned char);
        int rc = ((fn)((base_ + OFF_sd_connect) | 1))(ipc_, SD_SCALARDAEMON_QUEUE, SD_COMPONENT);
        uicore::logf("sdcom: connect(0x%lx, %u) = %d%s", SD_SCALARDAEMON_QUEUE, SD_COMPONENT, rc,
             rc == -5 ? " (not app mode)" : "");
        connected_ = (rc == 0);
        if (connected_) installCrashGuard();
        return connected_;
    }
    unsigned nextTx() {
        // Prefer the real id source when the ctor has run; else a local counter
        // (only matters for reply matching, which dryrun/no-conn never do).
        if (ipc_ && base_) {
            typedef int (*fn)(void *, unsigned *);
            unsigned tx = 0;
            if (((fn)((base_ + OFF_sd_newTxId) | 1))(ipc_, &tx) == 0 && tx) return tx;
        }
        return txCtr_++;
    }

    // ---- crash-handler disconnect (installed lazily, only after a real connect) --
    static SdcomClient *s_active;
    static struct sigaction s_old[3];
    static const int s_sig[3];
    static bool s_installed;
    static void onFatal(int sig) {
        if (s_active) s_active->disconnect();
        // chain to the previous handler (do NOT fight im.elf's own watchdog).
        for (int i = 0; i < 3; i++) if (s_sig[i] == sig) {
            const struct sigaction *o = &s_old[i];
            if (o->sa_flags & SA_SIGINFO) { if (o->sa_sigaction) { o->sa_sigaction(sig, 0, 0); return; } }
            else if (o->sa_handler && o->sa_handler != SIG_DFL && o->sa_handler != SIG_IGN) { o->sa_handler(sig); return; }
            break;
        }
        signal(sig, SIG_DFL);
        raise(sig);
    }
    void installCrashGuard() {
        if (s_installed) { s_active = this; return; }
        s_active = this;
        struct sigaction sa;
        memset(&sa, 0, sizeof sa);
        sa.sa_handler = &SdcomClient::onFatal;
        sigemptyset(&sa.sa_mask);
        sa.sa_flags = 0;
        for (int i = 0; i < 3; i++) sigaction(s_sig[i], &sa, &s_old[i]);
        s_installed = true;
    }

    unsigned long base_;
    void         *ipc_;
    void         *listener_;
    bool          connected_;
    bool          dryrun_;
    unsigned      txCtr_;
};

// ---- AIF: is the panel OSD layer on yet? (same probe a5kpreload uses) ------
inline int osd_on() {
    int fd = open("/dev/mem", O_RDONLY); if (fd < 0) return -1;
    volatile uint32_t *r = (volatile uint32_t *)mmap(0, 0x1000, PROT_READ, MAP_SHARED, fd, AIF_PHYS);
    close(fd);
    if (r == MAP_FAILED) return -1;
    uint32_t v = r[AIF_OSD_ON / 4];
    munmap((void *)r, 0x1000);
    return (v & OSD_ON_BIT) ? 1 : 0;
}

} // namespace a5kinproc
