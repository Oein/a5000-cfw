// a5000 Rigel UX_DEBUG_CLIENT display adapter. Composites the core canvas onto the
// camera panel via im.elf's Rigel Window Server. Ported from the emulator-verified
// app/native/research/rigel/rhello.c (app/docs/TRACKB_RUNTIME.md §1).
//
// Requirements (TRACKB_RUNTIME §1.3): run as root inside `busybox chroot /android`
// (bionic), buffers allocated with usage bit 0x100 (-> 0x133), order 0 = top, only
// composites in app (Scalar) mode, pixel bytes R,G,B,A (alpha 0 = live view shows).
#include "uicore/platform.h"
#include "uicore/canvas.h"
#include "uicore/log.h"
#include <dlfcn.h>
#include <string.h>

using namespace uicore;

namespace {

struct WSB { int w, h, fmt, rsv, slot; };   // WSImageBuffer, 20 bytes

typedef int  (*fn_alloc)(int, int, int, int, WSB *, int *);
typedef int  (*fn_free)(WSB *);
typedef int  (*fn_lock)(WSB *, int, int, int, int, int, void **);
typedef int  (*fn_unlock)(WSB *);
typedef void (*fn_reg)(WSB *);
typedef void *(*fn_create)(void *, const char *, int, int, unsigned, unsigned, unsigned char);
typedef int  (*fn_show)(void *, int);
typedef int  (*fn_swap)(void *, int, short, short, short, short);
typedef int  (*fn_destroy)(void *);
typedef int  (*fn_param)(void *, void *);

class RigelDisplay : public Display {
public:
    RigelDisplay()
        : hd_(0), hg_(0), win_(0), nbuf_(0), stride_(0), front_(0),
          alloc_(0), free_(0), lock_(0), unlock_(0), reg_(0),
          create_(0), show_(0), swap_(0), destroy_(0), get_(0), set_(0) {}

    const char *name() const { return "rigel"; }

    bool init() {
        hd_ = dlopen("/system/lib/libdihal.so", RTLD_NOW);
        hg_ = dlopen("/system/lib/hw/gralloc.diadem.so", RTLD_NOW);
        if (!hd_ || !hg_) { logf("rigel: dlopen dihal=%p gralloc=%p (need chroot /android)", hd_, hg_); return false; }
        alloc_   = (fn_alloc)  dlsym(hd_, "ws_hal_alloc");
        free_    = (fn_free)   dlsym(hd_, "ws_hal_free");
        lock_    = (fn_lock)   dlsym(hd_, "ws_hal_lock");
        unlock_  = (fn_unlock) dlsym(hd_, "ws_hal_unlock");
        reg_     = (fn_reg)    dlsym(hg_, "_Z15registerBuffersP13WSImageBuffer");
        create_  = (fn_create) dlsym(hg_, "rglCreateWindow");
        show_    = (fn_show)   dlsym(hg_, "rglShowWindow");
        swap_    = (fn_swap)   dlsym(hg_, "rglSwapBuffers");
        destroy_ = (fn_destroy)dlsym(hg_, "rglDestroyWindow");
        get_     = (fn_param)  dlsym(hg_, "rglGetWindowParam");
        set_     = (fn_param)  dlsym(hg_, "rglSetWindowParam");
        if (!alloc_ || !lock_ || !unlock_ || !reg_ || !create_ || !show_ || !swap_ || !set_) {
            logf("rigel: missing symbols"); return false;
        }
        const int usage = 0x133;   // HW_TEXTURE(0x100) mandatory | SW rw
        for (int i = 0; i < 2; i++) {
            memset(&buf_[i], 0, sizeof buf_[i]); buf_[i].slot = -1;
            int st = 0, r = alloc_(PANEL_W, PANEL_H, 1, usage, &buf_[i], &st);
            logf("rigel: alloc[%d]=%d stride=%d slot=%d", i, r, st, buf_[i].slot);
            if (r) { shutdown(); return false; }
            nbuf_++; stride_ = st;
            void *va = 0;
            if (!lock_(&buf_[i], usage, 0, 0, PANEL_W, PANEL_H, &va) && va) {
                memset(va, 0, (size_t)stride_ * PANEL_H * 4); unlock_(&buf_[i]);
            }
        }
        reg_(buf_);
        win_ = create_(0, "UX_DEBUG_CLIENT", PANEL_W, PANEL_H, 0, 0, 2);
        logf("rigel: createWindow=%p", win_);
        if (!win_) { shutdown(); return false; }
        show_(win_, 1);
        unsigned char wp[80]; memset(wp, 0, sizeof wp);
        get_(win_, wp); wp[16] = 0; set_(win_, wp);   // order 0 = top
        logf("rigel: window up (order 0)");
        return true;
    }

    void present(const Canvas &c, const Rect &) {
        if (!win_) return;
        int f = front_;
        void *va = 0;
        if (!lock_(&buf_[f], 0x133, 0, 0, PANEL_W, PANEL_H, &va) && va) {
            const u8 *src = c.pixels();
            u8 *dst = (u8 *)va;
            for (int y = 0; y < PANEL_H; y++)
                memcpy(dst + (size_t)y * stride_ * 4, src + (size_t)y * PANEL_W * 4, PANEL_W * 4);
            unlock_(&buf_[f]);
        }
        swap_(win_, f, 0, 0, PANEL_W, PANEL_H);
        front_ ^= 1;
    }

    void shutdown() {
        if (win_ && destroy_) { destroy_(win_); win_ = 0; }
        while (nbuf_ > 0) { nbuf_--; if (free_) free_(&buf_[nbuf_]); }
    }

private:
    void *hd_, *hg_, *win_;
    WSB   buf_[2];
    int   nbuf_, stride_, front_;
    fn_alloc alloc_; fn_free free_; fn_lock lock_; fn_unlock unlock_; fn_reg reg_;
    fn_create create_; fn_show show_; fn_swap swap_; fn_destroy destroy_; fn_param get_, set_;
};

} // namespace

namespace a5000 { uicore::Display *createRigelDisplay() { return new RigelDisplay(); } }
