// a5000 in-process Display adapter (route (b)).
// Blits uicore's RGBA8888 Canvas straight onto the stock panel OSD scanout buffer
// via /dev/mem (TRACKB_INPROCESS.md §10): 640x480, stride 1280, 16bpp RGBA4444.
//
// 16-bit LE word v: R=v[15:12] G=v[11:8] B=v[7:4] A=v[3:0]; i.e. bytes are
// byte0=(B<<4)|A, byte1=(R<<4)|G. Alpha nibble 0xF = opaque, 0 = fully
// transparent (live-view YUV plane shows through) -- straight-over compositing,
// so uicore's clear(0,0,0,0) correctly reveals the live view and opaque OSD
// elements overlay it.
//
// The scanout flips between the two back-to-back buffers every vsync, so each
// present() writes BOTH buffers (same approach as osdpoke / a5kpreload). VPH
// re-DMAs the active buffer each frame; no explicit commit is needed.
#include "uicore/platform.h"
#include "uicore/canvas.h"
#include "uicore/types.h"
#include "uicore/log.h"
#include "engine.h"
#include <fcntl.h>
#include <unistd.h>
#include <sys/mman.h>
#include <stdlib.h>
#include <string.h>

using namespace uicore;

namespace {

class OsdDisplay : public Display {
public:
    OsdDisplay() : fd_(-1), fb_(0), mapLen_(0), shadow_(0), shadowW_(0), shadowH_(0), firstFrame_(true) {}

    bool init() {
        fd_ = ::open("/dev/mem", O_RDWR);
        if (fd_ < 0) { logf("osd: /dev/mem open FAILED"); return false; }
        mapLen_ = (a5kinproc::OSD_NBUF * a5kinproc::OSD_FB_BYTES + 0xfffUL) & ~0xfffUL;
        fb_ = (volatile u8 *)::mmap(0, mapLen_, PROT_READ | PROT_WRITE, MAP_SHARED,
                                    fd_, a5kinproc::OSD_PHYS);
        if (fb_ == MAP_FAILED) { logf("osd: mmap OSD FAILED"); fb_ = 0; ::close(fd_); fd_ = -1; return false; }
        logf("osd: mapped double buffer phys 0x%lx len 0x%lx", a5kinproc::OSD_PHYS, mapLen_);
        return true;
    }

    // The naive version rewrote BOTH 640x480 buffers pixel-by-pixel to UNCACHED
    // /dev/mem every frame (~614k volatile u16 stores -> ~580 ms/frame = ~1.7 fps on
    // the single-core A5). Instead keep a cached RGBA4444 shadow of what is on screen
    // and push ONLY changed pixels to the two scanout buffers: idle frames do zero
    // uncached writes, and a menu move touches only the few rows that changed. The
    // convert+compare pass is all cached reads, so it stays cheap.
    void present(const Canvas &c, const Rect &) {
        if (!fb_) return;
        const u8 *src = c.pixels();                 // RGBA8888, PANEL_W*PANEL_H*4
        const int W = c.width(), H = c.height();
        const unsigned stride = a5kinproc::OSD_STRIDE;

        const size_t rowBytes = (size_t)W * 4;      // RGBA8888 source row
        if (!shadow_ || shadowW_ != W || shadowH_ != H) {
            free(shadow_);
            shadow_ = (u8 *)malloc((size_t)W * H * 4);
            shadowW_ = W; shadowH_ = H;
            firstFrame_ = true;
        }
        volatile u8 *buf0 = fb_;
        volatile u8 *buf1 = fb_ + (unsigned long)(a5kinproc::OSD_NBUF > 1 ? 1 : 0) * a5kinproc::OSD_FB_BYTES;
        for (int y = 0; y < H; y++) {
            const u8 *sp = src + (size_t)y * rowBytes;
            u8 *shrow = shadow_ ? shadow_ + (size_t)y * rowBytes : 0;
            // Row-level dirty test: a plain cached memcmp of the RGBA8888 row. Unchanged
            // rows (most of the screen when a highlight moves) are skipped WHOLE -- no
            // convert, no uncached /dev/mem write -- which is what makes nav responsive.
            if (shrow && !firstFrame_ && memcmp(shrow, sp, rowBytes) == 0) continue;
            if (shrow) memcpy(shrow, sp, rowBytes);
            volatile u16 *r0 = (volatile u16 *)(buf0 + (unsigned)y * stride);
            volatile u16 *r1 = (volatile u16 *)(buf1 + (unsigned)y * stride);
            const u8 *p = sp;
            for (int x = 0; x < W; x++, p += 4) {
                u16 R = p[0] >> 4, G = p[1] >> 4, Bl = p[2] >> 4, A = p[3] >> 4;
                u16 v = (u16)((R << 12) | (G << 8) | (Bl << 4) | A);
                r0[x] = v;
                if (a5kinproc::OSD_NBUF > 1) r1[x] = v;
            }
        }
        firstFrame_ = false;
    }

    void shutdown() {
        if (fb_) { ::munmap((void *)fb_, mapLen_); fb_ = 0; }
        if (fd_ >= 0) { ::close(fd_); fd_ = -1; }
        if (shadow_) { free(shadow_); shadow_ = 0; }
    }
    const char *name() const { return "osd-inproc"; }

private:
    int fd_;
    volatile u8 *fb_;
    unsigned long mapLen_;
    u8 *shadow_;             // cached RGBA8888 copy of last canvas (row-level dirty-skip)
    int shadowW_, shadowH_;
    bool firstFrame_;        // force a full write after (re)alloc
};

} // namespace

namespace a5kinproc {
uicore::Display *createOsdDisplay() { return new OsdDisplay(); }
}
