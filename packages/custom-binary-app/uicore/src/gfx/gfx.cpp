#include "uicore/gfx.h"
#include "uicore/canvas.h"
#include "assets_loader.h"
#include "../../gen/fonts.h"
#include "../../gen/icons.h"
#include <string.h>

namespace uicore {

namespace {
struct FontMap { int gf; int fontId; int fallbackScale; };
// Fallback (no assets): scale of the 5x7 font that roughly matches the pixel size.
const FontMap kMap[GF_COUNT] = {
    { GF_OSD_15, FONT_OSD_15, 2 },
    { GF_OSD_16, FONT_OSD_16, 2 },
    { GF_OSD_24, FONT_OSD_24, 3 },
    { GF_UI_12,  FONT_UI_12,  1 },
    { GF_OSD_21, FONT_OSD_21, 3 },
    { GF_OSD_26, FONT_OSD_26, 3 },
    { GF_OSD_40, FONT_OSD_40, 5 },
    { GF_UI_19,  FONT_UI_19,  2 },
    { GF_UI_20,  FONT_UI_20,  2 },
    { GF_UI_24,  FONT_UI_24,  3 },
    { GF_UI_26,  FONT_UI_26,  3 },
    { GF_UI_27,  FONT_UI_27,  3 },
    { GF_OSD_19, FONT_OSD_19, 2 },
};
}

Gfx::Gfx() : icons_(0), fontsOk_(false), iconsOk_(false), src_(0) {
    for (int i = 0; i < GF_COUNT; i++) fonts_[i] = 0;
}

Gfx::~Gfx() {
    for (int i = 0; i < GF_COUNT; i++) delete fonts_[i];
    delete icons_;
    delete (FileAssetDir *)src_;
}

bool Gfx::load(const char *dir) {
    if (!dir || !*dir) return false;
    FileAssetDir *fd = new FileAssetDir;
    src_ = fd;
    AssetSource src;
    assetsFileSource(fd, dir, &src);

    int okFonts = 0;
    for (int i = 0; i < GF_COUNT; i++) {
        FontAtlas *f = new FontAtlas;
        const FontInfo &fi = kFonts[kMap[i].fontId];
        if (f->load(src, fi.file, fi.checksum)) { fonts_[i] = f; okFonts++; }
        else delete f;
    }
    fontsOk_ = okFonts == GF_COUNT;

    IconAtlas *ia = new IconAtlas;
    if (ia->load(src, "icons.bin", ICONS_CHECKSUM)) { icons_ = ia; iconsOk_ = true; }
    else delete ia;
    return fontsOk_ && iconsOk_;
}

int Gfx::text(Canvas &c, int fontId, int x, int baseline, const char *s, Color col, bool outline) const {
    if (fontId < 0 || fontId >= GF_COUNT) return x;
    const FontAtlas *f = fonts_[fontId];
    if (!f) {                                   // 5x7 fallback
        int sc = kMap[fontId].fallbackScale;
        int ty = baseline - 7 * sc;
        if (outline) {
            static const int d[4][2] = { {-1,0},{1,0},{0,-1},{0,1} };
            for (int k = 0; k < 4; k++) c.text(x + d[k][0], ty + d[k][1], sc, s, rgba(0, 0, 0, 200));
        }
        return c.text(x, ty, sc, s, col);
    }
    u8 *px = c.pixels();
    // Direct baked-font draw bypasses Canvas::setPixel -> report the dirty bbox so present()
    // can bound its scan (glyph box + 2px outline glow margin).
    c.markDirty(x - 3, baseline - f->ascent() - 3, f->measure(s, 0) + 6, f->lineHeight() + 6);
    if (outline) {
        // Android's setShadowLayer(2dp, 0,0, black) is a soft glow, not a hard outline: a faint wide
        // ring plus a darker tight ring reproduces it (matches the Track A golden).
        static const int d2[8][2] = { {-2,0},{2,0},{0,-2},{0,2},{-2,-2},{2,-2},{-2,2},{2,2} };
        static const int d1[8][2] = { {-1,0},{1,0},{0,-1},{0,1},{-1,-1},{1,-1},{-1,1},{1,1} };
        Color wide = rgba(0, 0, 0, 40), tight = rgba(0, 0, 0, 105);
        for (int k = 0; k < 8; k++)
            f->draw(px, PANEL_W, PANEL_H, PANEL_W * 4, x + d2[k][0], baseline + d2[k][1], s, wide, 0);
        for (int k = 0; k < 8; k++)
            f->draw(px, PANEL_W, PANEL_H, PANEL_W * 4, x + d1[k][0], baseline + d1[k][1], s, tight, 0);
    }
    return f->draw(px, PANEL_W, PANEL_H, PANEL_W * 4, x, baseline, s, col, 0);
}

void Gfx::textBig(Canvas &c, int fontId, int cx, int cy, const char *s, Color col, int sx, int sy, bool outline) const {
    if (fontId < 0 || fontId >= GF_COUNT || sx < 1 || sy < 1) return;
    const FontAtlas *f = fonts_[fontId];
    if (!f) {                                   // fallback: 5x7 font scaled directly
        int sc = kMap[fontId].fallbackScale * sy;
        c.text(cx - c.textWidth(s, sc) / 2, cy - 7 * sc / 2, sc, s, col);
        return;
    }
    enum { TW = 96, TH = 48 };
    static u8 tmp[TW * TH * 4];
    int w = f->measure(s, 0) + 4, asc = f->ascent(), h = f->lineHeight() + 4;
    if (w > TW) w = TW;
    if (h > TH) h = TH;
    memset(tmp, 0, sizeof tmp);
    f->draw(tmp, TW, TH, TW * 4, 2, 2 + asc, s, rgba(255, 255, 255, 255), 0);
    int ow = w * sx, oh = h * sy, ox = cx - ow / 2, oy = cy - oh / 2;
    for (int pass = outline ? 0 : 1; pass < 2; pass++) {   // pass 0 = dark outline, 1 = fill
        static const int off[5][2] = { {-2,0},{2,0},{0,-2},{0,2},{0,0} };
        int n = pass == 0 ? 4 : 1;
        for (int k = 0; k < n; k++) {
            int dx = pass == 0 ? off[k][0] : 0, dy = pass == 0 ? off[k][1] : 0;
            for (int y = 0; y < oh; y++) {
                int fy = (y * 256 + 128) / sy - 128; int y0 = fy >> 8, wy = fy & 255;
                for (int x = 0; x < ow; x++) {
                    int fx = (x * 256 + 128) / sx - 128; int x0 = fx >> 8, wx = fx & 255;
                    int a = 0;
                    for (int j = 0; j < 2; j++) for (int i = 0; i < 2; i++) {
                        int xx = x0 + i, yy = y0 + j;
                        if (xx < 0) xx = 0; if (xx >= w) xx = w - 1;
                        if (yy < 0) yy = 0; if (yy >= h) yy = h - 1;
                        a += tmp[(yy * TW + xx) * 4 + 3] * (i ? wx : 256 - wx) * (j ? wy : 256 - wy);
                    }
                    a >>= 16;
                    if (!a) continue;
                    Color pc = pass == 0 ? rgba(0, 0, 0, (u8)(a * 170 / 255)) : rgba(col.r, col.g, col.b, (u8)(a * col.a / 255));
                    int px = ox + x + dx, py = oy + y + dy;
                    if (px >= 0 && px < PANEL_W && py >= 0 && py < PANEL_H) c.setPixel(px, py, pc);
                }
            }
        }
    }
}

int Gfx::textWidth(int fontId, const char *s) const {
    if (fontId < 0 || fontId >= GF_COUNT) return 0;
    const FontAtlas *f = fonts_[fontId];
    if (!f) { int n = 0; for (const char *p = s; *p; p++) n++; return (n * 6 * kMap[fontId].fallbackScale * 3) / 4; }  // X0.75 pre-squeeze (matches Canvas::text)
    return f->measure(s, 0);
}

int Gfx::fontAscent(int fontId) const {
    if (fontId < 0 || fontId >= GF_COUNT) return 0;
    const FontAtlas *f = fonts_[fontId];
    return f ? f->ascent() : 7 * kMap[fontId].fallbackScale;
}

int Gfx::iconByName(const char *n) const { return icons_ ? icons_->find(n) : -1; }

bool Gfx::iconSize(int id, int *w, int *h) const {
    IconRectRT r;
    if (!icons_ || !icons_->rect(id, &r)) return false;
    if (w) *w = r.w;
    if (h) *h = r.h;
    return true;
}

bool Gfx::icon(Canvas &c, int id, int x, int y, int alpha) const {
    IconRectRT r;
    if (!icons_ || !icons_->rect(id, &r)) return false;
    c.markDirty(x, y, r.w, r.h);   // blit/loop below writes px directly (alpha>=255) or via setPixel
    if (alpha >= 255) { icons_->blit(c.pixels(), PANEL_W, PANEL_H, PANEL_W * 4, x, y, id); return true; }
    const u8 *atlas = icons_->pixels();
    int aw = icons_->atlasW();
    for (int j = 0; j < r.h; j++) {
        int dy = y + j; if (dy < 0 || dy >= PANEL_H) continue;
        const u8 *s = atlas + ((r.y + j) * aw + r.x) * 4;
        for (int i = 0; i < r.w; i++, s += 4) {
            int dx = x + i; if (dx < 0 || dx >= PANEL_W) continue;
            c.setPixel(dx, dy, rgba(s[0], s[1], s[2], (u8)(s[3] * alpha / 255)));
        }
    }
    return true;
}

bool Gfx::iconScaled(Canvas &c, int id, int x, int y, int num, int den,
                     int *outW, int *outH, int alpha) const {
    IconRectRT r;
    if (!icons_ || !icons_->rect(id, &r)) return false;
    if (num <= 0 || den <= 0) { num = den = 1; }
    int ow = r.w * num / den, oh = r.h * num / den;
    if (ow < 1) ow = 1; if (oh < 1) oh = 1;
    if (outW) *outW = ow;
    if (outH) *outH = oh;
    const u8 *atlas = icons_->pixels();
    int aw = icons_->atlasW();
    for (int oy = 0; oy < oh; oy++) {
        int dy = y + oy; if (dy < 0 || dy >= PANEL_H) continue;
        int sy = r.y + oy * den / num; if (sy >= r.y + r.h) sy = r.y + r.h - 1;
        const u8 *row = atlas + (unsigned)sy * aw * 4;
        for (int ox = 0; ox < ow; ox++) {
            int dx = x + ox; if (dx < 0 || dx >= PANEL_W) continue;
            int sx = r.x + ox * den / num; if (sx >= r.x + r.w) sx = r.x + r.w - 1;
            const u8 *s = row + (unsigned)sx * 4;
            unsigned a = (unsigned)s[3] * (unsigned)alpha / 255;
            if (a == 0) continue;
            c.setPixel(dx, dy, rgba(s[0], s[1], s[2], (u8)a));
        }
    }
    return true;
}

} // namespace uicore
