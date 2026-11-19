// Fake card media for the host simulator (platform helper, not core): `count` generated test
// images (sky gradient + sun + hills + the index number) so the M3 gallery renders without a card or
// any Sony/personal photo. Mix: 3:2 stills, a few 16:9, one protected, one RAW+JPG, one movie.
// Supports remove() in memory (delete flow demo). No JPEG involved -- a real provider decodes with a
// scaled JPEG decode; that path is the a5000 wiring (stub, see platform.h).
#include "uicore/platform.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>

using namespace uicore;

namespace {

const char *kDigit[10] = {   // 3x5 digit font, row-major
    "111101101101111", "010110010010111", "111001111100111", "111001111001111", "101101111001001",
    "111100111001111", "111100111101111", "111001001001001", "111101111101111", "111101111001111" };

class FakeMedia : public Media {
public:
    explicit FakeMedia(int n) : n_(n < 0 ? 0 : (n > kMax ? kMax : n)) {
        for (int i = 0; i < n_; i++) id_[i] = i;
    }
    int count() { return n_; }

    bool info(int i, MediaEntry *e) {
        if (i < 0 || i >= n_) return false;
        int id = id_[i];
        memset(e, 0, sizeof *e);
        e->movie = (id == 5);
        e->prot  = (id == 3);
        e->raw   = (id == 7);
        e->folderNo = 100 + id / 9999; e->fileNo = id + 1;
        snprintf(e->folder, sizeof e->folder, "%03dMSDCF", e->folderNo);
        snprintf(e->name, sizeof e->name, e->movie ? "MAH%05d.MP4" : "DSC%05d.JPG", e->fileNo);
        bool wide = (id % 4 == 2) || e->movie;
        e->w = e->movie ? 1920 : (wide ? 6000 : 6000);
        e->h = e->movie ? 1080 : (wide ? 3376 : 4000);
        e->size = e->movie ? 48u * 1024 * 1024 : 5u * 1024 * 1024 + (unsigned)id * 37123;
        e->year = 2026; e->mon = 11; e->day = 1 + id % 28; e->hour = 9 + id % 10; e->min = (id * 7) % 60; e->sec = (id * 13) % 60;
        e->durationSec = e->movie ? 83 : 0;
        return true;
    }

    bool load(int i, int maxW, int maxH, bool /*thumb*/, MediaImage *out) {
        MediaEntry e;
        if (!info(i, &e) || maxW < 8 || maxH < 8) return false;
        int w = maxW, h = (int)((long)w * e.h / e.w);        // fit maxW x maxH, aspect preserved
        if (h > maxH) { h = maxH; w = (int)((long)h * e.w / e.h); }
        if (w < 8) w = 8; if (h < 8) h = 8;
        u8 *px = (u8 *)malloc((size_t)w * h * 4);
        if (!px) return false;
        render(id_[i], w, h, px);
        out->w = w; out->h = h; out->px = px;
        return true;
    }
    void release(MediaImage *img) { if (img && img->px) { free(img->px); img->px = 0; } }

    bool remove(int i) {
        if (i < 0 || i >= n_) return false;
        for (int k = i; k < n_ - 1; k++) id_[k] = id_[k + 1];
        n_--;
        return true;
    }

private:
    enum { kMax = 400 };
    int n_;
    int id_[kMax];

    static void put(u8 *px, int w, int x, int y, int r, int g, int b) {
        u8 *p = px + ((size_t)y * w + x) * 4; p[0] = (u8)r; p[1] = (u8)g; p[2] = (u8)b; p[3] = 255;
    }
    static int clamp8(int v) { return v < 0 ? 0 : (v > 255 ? 255 : v); }

    static void render(int id, int w, int h, u8 *px) {
        int hue = (id * 47) % 256;                                    // per-image palette
        int sr = 60 + (hue * 3) % 140, sg = 90 + (hue * 5) % 120, sb = 150 + (hue * 7) % 100;
        int sunx = w * (15 + (id * 23) % 70) / 100, suny = h * (18 + (id * 11) % 22) / 100, sunr = h / 8 + 2;
        for (int y = 0; y < h; y++) {
            for (int x = 0; x < w; x++) {
                int t = y * 256 / h;                                  // sky gradient
                int r = clamp8(sr + t / 2), g = clamp8(sg + t / 3), b = clamp8(sb - t / 4);
                long dx = x - sunx, dy = y - suny;
                if (dx * dx + dy * dy < (long)sunr * sunr) { r = 255; g = 230; b = 120; }
                int hill = h * 70 / 100 + (int)((x * 9 + id * 31) % 64 < 32 ? (x * 3 + id) % 24 : 24 - (x * 3 + id) % 24) * h / 480;
                int hill2 = h * 82 / 100 + ((x / 3 + id * 5) % 20) * h / 960;
                if (y > hill2)      { r = 30 + hue / 8; g = 70 + hue / 5; b = 40; }
                else if (y > hill)  { r = 60 + hue / 6; g = 110 + hue / 8; b = 70; }
                put(px, w, x, y, r, g, b);
            }
        }
        // big index number (1-based file number), white with a dark shadow
        char num[8]; snprintf(num, sizeof num, "%d", id + 1);
        int len = (int)strlen(num), sc = h / 14; if (sc < 1) sc = 1;
        int tw = len * 4 * sc - sc, x0 = (w - tw) / 2, y0 = h * 40 / 100;
        for (int pass = 0; pass < 2; pass++) {
            int off = pass == 0 ? sc / 2 + 1 : 0, c = pass == 0 ? 20 : 255;
            for (int k = 0; k < len; k++) {
                const char *d = kDigit[num[k] - '0'];
                for (int ry = 0; ry < 5; ry++) for (int rx = 0; rx < 3; rx++) {
                    if (d[ry * 3 + rx] != '1') continue;
                    for (int yy = 0; yy < sc; yy++) for (int xx = 0; xx < sc; xx++) {
                        int X = x0 + k * 4 * sc + rx * sc + xx + off, Y = y0 + ry * sc + yy + off;
                        if (X >= 0 && X < w && Y >= 0 && Y < h) put(px, w, X, Y, c, c, c);
                    }
                }
            }
        }
    }
};

} // namespace

namespace platform { uicore::Media *createFakeMedia(int count) { return new FakeMedia(count); } }
