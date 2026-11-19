// See assets_loader.h. libc only (stdio/stdlib/string); optional POSIX mmap.
#include "assets_loader.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#if defined(UICORE_ASSETS_MMAP)
#include <fcntl.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <unistd.h>
#endif

namespace uicore {

// ---- little-endian readers (alignment-safe) -------------------------------
static inline u32 rd16(const u8 *p) { return (u32)p[0] | ((u32)p[1] << 8); }
static inline u32 rd32(const u8 *p) {
    return (u32)p[0] | ((u32)p[1] << 8) | ((u32)p[2] << 16) | ((u32)p[3] << 24);
}
static inline i32 rds16(const u8 *p) { return (i32)(int16_t)rd16(p); }

static u32 fnv1a(const u8 *p, u32 n) {
    u32 h = 0x811C9DC5u;
    for (u32 i = 0; i < n; ++i) h = (h ^ p[i]) * 0x01000193u;
    return h;
}

// ---- file source ----------------------------------------------------------
static bool fileOpen(void *ctx, const char *name, AssetBlob *out) {
    const FileAssetDir *d = (const FileAssetDir *)ctx;
    char path[320];
    if (snprintf(path, sizeof path, "%s/%s", d->dir, name) >= (int)sizeof path) return false;
#if defined(UICORE_ASSETS_MMAP)
    int fd = open(path, O_RDONLY);
    if (fd < 0) return false;
    struct stat st;
    if (fstat(fd, &st) != 0 || st.st_size <= 0 || st.st_size > 0x7fffffff) { close(fd); return false; }
    void *m = mmap(0, (size_t)st.st_size, PROT_READ, MAP_PRIVATE, fd, 0);
    close(fd);
    if (m == MAP_FAILED) return false;
    out->data = (const u8 *)m;
    out->size = (u32)st.st_size;
    out->handle = m;
    return true;
#else
    FILE *f = fopen(path, "rb");
    if (!f) return false;
    long n = -1;
    if (fseek(f, 0, SEEK_END) == 0) n = ftell(f);
    if (n <= 0 || n > 0x7fffffff || fseek(f, 0, SEEK_SET) != 0) { fclose(f); return false; }
    u8 *buf = (u8 *)malloc((size_t)n);
    if (!buf) { fclose(f); return false; }
    size_t got = fread(buf, 1, (size_t)n, f);
    fclose(f);
    if (got != (size_t)n) { free(buf); return false; }
    out->data = buf;
    out->size = (u32)n;
    out->handle = buf;
    return true;
#endif
}

static void fileClose(void *ctx, AssetBlob *b) {
    (void)ctx;
    if (!b->handle) return;
#if defined(UICORE_ASSETS_MMAP)
    munmap(b->handle, b->size);
#else
    free(b->handle);
#endif
    b->handle = 0;
    b->data = 0;
    b->size = 0;
}

void assetsFileSource(FileAssetDir *storage, const char *dir, AssetSource *out) {
    size_t n = strlen(dir);
    if (n >= sizeof storage->dir) n = sizeof storage->dir - 1;
    memcpy(storage->dir, dir, n);
    storage->dir[n] = 0;
    out->ctx = storage;
    out->open = fileOpen;
    out->close = fileClose;
}

// ---- blending -------------------------------------------------------------
static inline void blendPx(u8 *d, u32 r, u32 g, u32 b, u32 a) {
    if (a == 0) return;
    if (a == 255) { d[0] = (u8)r; d[1] = (u8)g; d[2] = (u8)b; d[3] = 255; return; }
    u32 ia = 255 - a;
    d[0] = (u8)((r * a + d[0] * ia + 127) / 255);
    d[1] = (u8)((g * a + d[1] * ia + 127) / 255);
    d[2] = (u8)((b * a + d[2] * ia + 127) / 255);
    d[3] = (u8)(a + (d[3] * ia + 127) / 255);
}

static void blitA8(u8 *dst, int dstW, int dstH, int stride, int x, int y,
                   const u8 *src, int srcStride, int w, int h, Color c) {
    for (int j = 0; j < h; ++j) {
        int dy = y + j;
        if (dy < 0 || dy >= dstH) continue;
        const u8 *s = src + j * srcStride;
        u8 *row = dst + dy * stride;
        for (int i = 0; i < w; ++i) {
            int dx = x + i;
            if (dx < 0 || dx >= dstW) continue;
            u32 a = (u32)s[i] * c.a / 255;
            blendPx(row + dx * 4, c.r, c.g, c.b, a);
        }
    }
}

// ---- IconAtlas ------------------------------------------------------------
// Header 32 B: magic[4] ver16 fmt16 w16 h16 count32 namesOff32 pixOff32 pixSize32 checksum32
IconAtlas::IconAtlas()
    : entries_(0), names_(0), namesSize_(0), pixels_(0), decoded_(0), count_(0), w_(0), h_(0) {
    memset(&src_, 0, sizeof src_);
    memset(&blob_, 0, sizeof blob_);
}
IconAtlas::~IconAtlas() { unload(); }

void IconAtlas::unload() {
    if (decoded_) { free(decoded_); decoded_ = 0; }
    if (blob_.handle && src_.close) src_.close(src_.ctx, &blob_);
    memset(&blob_, 0, sizeof blob_);
    entries_ = 0; names_ = 0; pixels_ = 0; count_ = w_ = h_ = 0; namesSize_ = 0;
}

static bool rleDecode(const u8 *s, u32 n, u8 *dst, u32 pixels) {
    u32 i = 0, o = 0;
    while (o < pixels) {
        if (i >= n) return false;
        u32 ctl = s[i++];
        u32 run = (ctl & 0x7f) + 1;
        if (o + run > pixels) return false;
        if (ctl & 0x80) {
            if (i + 4 > n) return false;
            for (u32 k = 0; k < run; ++k) memcpy(dst + (o + k) * 4, s + i, 4);
            i += 4;
        } else {
            if (i + run * 4 > n) return false;
            memcpy(dst + o * 4, s + i, run * 4);
            i += run * 4;
        }
        o += run;
    }
    return true;
}

bool IconAtlas::load(const AssetSource &src, const char *name, u32 expectChecksum) {
    unload();
    src_ = src;
    if (!src.open || !src.open(src.ctx, name, &blob_)) return false;
    const u8 *p = blob_.data;
    u32 n = blob_.size;
    if (n < 32 || memcmp(p, "UIIA", 4) != 0 || rd16(p + 4) != 1) { unload(); return false; }
    u32 fmt = rd16(p + 6), w = rd16(p + 8), h = rd16(p + 10), count = rd32(p + 12);
    u32 namesOff = rd32(p + 16), pixOff = rd32(p + 20), pixSize = rd32(p + 24), sum = rd32(p + 28);
    if (fmt > 1 || w == 0 || h == 0 || w > 4096 || h > 4096 || count > 4096 ||
        32 + count * 12 > namesOff || namesOff > pixOff || pixOff > n || pixSize > n - pixOff ||
        (fmt == 0 && pixSize != w * h * 4)) { unload(); return false; }
    entries_ = p + 32;
    if (fnv1a(entries_, count * 12) != sum || (expectChecksum && expectChecksum != sum)) {
        unload(); return false;
    }
    names_ = (const char *)(p + namesOff);
    namesSize_ = pixOff - namesOff;
    for (u32 i = 0; i < count; ++i) {            // every rect inside atlas, every name terminated
        const u8 *e = entries_ + i * 12;
        if (rd16(e) + rd16(e + 4) > w || rd16(e + 2) + rd16(e + 6) > h) { unload(); return false; }
        u32 no = rd32(e + 8);
        if (no >= namesSize_ || !memchr(names_ + no, 0, namesSize_ - no)) { unload(); return false; }
    }
    if (fmt == 1) {
        decoded_ = (u8 *)malloc((size_t)w * h * 4);
        if (!decoded_ || !rleDecode(p + pixOff, pixSize, decoded_, w * h)) { unload(); return false; }
        pixels_ = decoded_;   // blob stays open: entries_/names_ point into it (it is small)
    } else {
        pixels_ = p + pixOff;
    }
    count_ = (int)count; w_ = (int)w; h_ = (int)h;
    return true;
}

int IconAtlas::find(const char *resName) const {
    for (int i = 0; i < count_; ++i)
        if (strcmp(name(i), resName) == 0) return i;
    return -1;
}

bool IconAtlas::rect(int id, IconRectRT *out) const {
    if (id < 0 || id >= count_) return false;
    const u8 *e = entries_ + id * 12;
    out->x = (u16)rd16(e); out->y = (u16)rd16(e + 2); out->w = (u16)rd16(e + 4); out->h = (u16)rd16(e + 6);
    return true;
}

const char *IconAtlas::name(int id) const {
    if (id < 0 || id >= count_) return "";
    return names_ + rd32(entries_ + id * 12 + 8);
}

void IconAtlas::blit(u8 *dst, int dstW, int dstH, int stride, int x, int y, int id) const {
    IconRectRT r;
    if (!pixels_ || !rect(id, &r)) return;
    for (int j = 0; j < r.h; ++j) {
        int dy = y + j;
        if (dy < 0 || dy >= dstH) continue;
        const u8 *s = pixels_ + ((r.y + j) * w_ + r.x) * 4;
        u8 *row = dst + dy * stride;
        for (int i = 0; i < r.w; ++i, s += 4) {
            int dx = x + i;
            if (dx < 0 || dx >= dstW) continue;
            blendPx(row + dx * 4, s[0], s[1], s[2], s[3]);
        }
    }
}

// ---- FontAtlas ------------------------------------------------------------
// Header 36 B: magic[4] ver16 px16 w16 h16 asc16 desc16 line16 xs16 count32 glyphOff32 pixOff32 checksum32
// Glyph 16 B: cp32 x16 y16 w8 h8 left8 top8 adv16 rsv16
FontAtlas::FontAtlas()
    : glyphs_(0), alpha_(0), px_(0), ascent_(0), descent_(0), line_(0), xscaleQ8_(256),
      count_(0), w_(0), h_(0) {
    memset(&src_, 0, sizeof src_);
    memset(&blob_, 0, sizeof blob_);
}
FontAtlas::~FontAtlas() { unload(); }

void FontAtlas::unload() {
    if (blob_.handle && src_.close) src_.close(src_.ctx, &blob_);
    memset(&blob_, 0, sizeof blob_);
    glyphs_ = 0; alpha_ = 0; count_ = w_ = h_ = 0;
}

bool FontAtlas::load(const AssetSource &src, const char *name, u32 expectChecksum) {
    unload();
    src_ = src;
    if (!src.open || !src.open(src.ctx, name, &blob_)) return false;
    const u8 *p = blob_.data;
    u32 n = blob_.size;
    if (n < 36 || memcmp(p, "UIFA", 4) != 0 || rd16(p + 4) != 1) { unload(); return false; }
    u32 w = rd16(p + 8), h = rd16(p + 10), count = rd32(p + 20);
    u32 gOff = rd32(p + 24), pOff = rd32(p + 28), sum = rd32(p + 32);
    if (w == 0 || h == 0 || w > 8192 || h > 8192 || count > 0x20000 || gOff < 36 || gOff + count * 16 > pOff ||
        pOff > n || w * h > n - pOff) { unload(); return false; }
    glyphs_ = p + gOff;
    if (fnv1a(glyphs_, count * 16) != sum || (expectChecksum && expectChecksum != sum)) {
        unload(); return false;
    }
    for (u32 i = 0; i < count; ++i) {
        const u8 *g = glyphs_ + i * 16;
        if (rd16(g + 4) + g[8] > w || rd16(g + 6) + g[9] > h) { unload(); return false; }
        if (i && rd32(g) <= rd32(g - 16)) { unload(); return false; }   // must be sorted
    }
    px_ = (int)rd16(p + 6);
    ascent_ = rds16(p + 12); descent_ = rds16(p + 14); line_ = rds16(p + 16);
    xscaleQ8_ = (int)rd16(p + 18);
    count_ = (int)count; w_ = (int)w; h_ = (int)h;
    alpha_ = p + pOff;
    return true;
}

bool FontAtlas::find(u32 cp, GlyphInfo *out) const {
    int lo = 0, hi = count_ - 1;
    while (lo <= hi) {
        int mid = (lo + hi) >> 1;
        const u8 *g = glyphs_ + mid * 16;
        u32 c = rd32(g);
        if (c < cp) lo = mid + 1;
        else if (c > cp) hi = mid - 1;
        else {
            out->cp = c;
            out->x = (u16)rd16(g + 4); out->y = (u16)rd16(g + 6);
            out->w = g[8]; out->h = g[9];
            out->left = (int8_t)g[10]; out->top = (int8_t)g[11];
            out->advanceQ6 = (i32)rd16(g + 12);
            return true;
        }
    }
    return false;
}

u32 utf8Next(const char **ps) {
    const u8 *s = (const u8 *)*ps;
    u32 c = s[0];
    int n = c < 0x80 ? 0 : (c & 0xE0) == 0xC0 ? 1 : (c & 0xF0) == 0xE0 ? 2 : (c & 0xF8) == 0xF0 ? 3 : -1;
    if (n < 0) { *ps += 1; return 0xFFFD; }
    if (n > 0) c &= 0x3F >> n;
    for (int i = 1; i <= n; ++i) {
        if ((s[i] & 0xC0) != 0x80) { *ps += i; return 0xFFFD; }
        c = (c << 6) | (s[i] & 0x3F);
    }
    *ps += n + 1;
    return c;
}

// Glyph lookup with fallback chain; returns the atlas that owns it (or 0).
static const FontAtlas *lookup(const FontAtlas *f, const FontAtlas *fb, u32 cp, GlyphInfo *g) {
    if (f->find(cp, g)) return f;
    if (fb && fb->loaded() && fb->find(cp, g)) return fb;
    if (f->find('?', g)) return f;
    return 0;
}

int FontAtlas::measure(const char *s, const FontAtlas *fallback) const {
    i32 pen = 0;
    while (s && *s) {
        GlyphInfo g;
        if (lookup(this, fallback, utf8Next(&s), &g)) pen += g.advanceQ6;
    }
    return (pen + 32) >> 6;   // same rounding as draw()
}

int FontAtlas::draw(u8 *dst, int dstW, int dstH, int stride, int x, int baselineY,
                    const char *s, Color c, const FontAtlas *fallback) const {
    i32 pen = x << 6;
    while (s && *s) {
        GlyphInfo g;
        const FontAtlas *f = lookup(this, fallback, utf8Next(&s), &g);
        if (!f) continue;
        if (g.w && g.h)
            blitA8(dst, dstW, dstH, stride, ((pen + 32) >> 6) + g.left, baselineY - g.top,
                   f->alpha_ + g.y * f->w_ + g.x, f->w_, g.w, g.h, c);
        pen += g.advanceQ6;
    }
    return (pen + 32) >> 6;
}

} // namespace uicore
