#include "uicore/canvas.h"
#include "font_data.h"
#include <stdio.h>
#include <stdlib.h>

namespace uicore {

Canvas::Canvas() {
    px_ = (u8 *)malloc((size_t)PANEL_W * PANEL_H * 4);
    resetDirty();
    Color black = { 0, 0, 0, 255 };
    clear(black);
}
Canvas::~Canvas() { free(px_); }

void Canvas::clear(Color c) {
    for (int i = 0; i < PANEL_W * PANEL_H; i++) {
        px_[i * 4 + 0] = c.r; px_[i * 4 + 1] = c.g; px_[i * 4 + 2] = c.b; px_[i * 4 + 3] = c.a;
    }
    markDirty(0, 0, PANEL_W, PANEL_H);
}

void Canvas::setPixel(int x, int y, Color c) {
    if (x < 0 || x >= PANEL_W || y < 0 || y >= PANEL_H) return;
    markDirty(x, y, 1, 1);
    u8 *p = &px_[(y * PANEL_W + x) * 4];
    if (c.a == 255) { p[0] = c.r; p[1] = c.g; p[2] = c.b; p[3] = 255; return; }
    if (c.a == 0) return;
    int a = c.a, ia = 255 - a;
    p[0] = (u8)((c.r * a + p[0] * ia) / 255);
    p[1] = (u8)((c.g * a + p[1] * ia) / 255);
    p[2] = (u8)((c.b * a + p[2] * ia) / 255);
    p[3] = (u8)(a + p[3] * ia / 255);
}

void Canvas::fillRect(int x, int y, int w, int h, Color c) {
    for (int j = y; j < y + h; j++)
        for (int i = x; i < x + w; i++)
            setPixel(i, j, c);
}

void Canvas::rectOutline(int x, int y, int w, int h, Color c) {
    fillRect(x, y, w, 1, c);
    fillRect(x, y + h - 1, w, 1, c);
    fillRect(x, y, 1, h, c);
    fillRect(x + w - 1, y, 1, h, c);
}

static const Glyph5x7 *glyph(char ch) {
    if (ch >= 'a' && ch <= 'z') ch = (char)(ch - 'a' + 'A');
    for (int i = 0; i < kFont5x7Count; i++)
        if (kFont5x7[i].c == ch) return &kFont5x7[i];
    return 0;
}

// The built-in 5x7 fallback is authored in square (logical) pixels but, like every
// other uicore element, must be PRE-SQUEEZED on X by 0.75 so the panel's 640->853
// horizontal stretch un-squeezes it to correct proportions (same as the baked fonts,
// whose glyphs are rastered at xscale_q8=192). Without this the fallback text renders
// at full width and the panel stretch makes it oversized and run off-screen (the exact
// real-device symptom when the baked assets are absent). Height (Y) stays 1:1 -- the
// panel only stretches X. SQ() maps a logical-X span to physical, consistent with LX().
static inline int sqx(int logical) { return (logical * 3) / 4; }

int Canvas::textWidth(const char *s, int scale) const {
    int n = 0; for (const char *p = s; *p; p++) n++;
    return sqx(n * 6 * scale);
}

int Canvas::text(int x, int y, int scale, const char *s, Color c) {
    int ci = 0;
    for (; *s; s++, ci++) {
        const Glyph5x7 *g = glyph(*s);
        int base = ci * 6 * scale;                 // this cell's logical-X origin
        if (!g) continue;
        for (int r = 0; r < 7; r++) {
            u8 bits = g->rows[r];
            for (int k = 0; k < 5; k++)
                if (bits & (1 << (4 - k))) {
                    int px0 = x + sqx(base + k * scale);
                    int px1 = x + sqx(base + (k + 1) * scale);
                    int w = px1 - px0; if (w < 1) w = 1;   // keep thin columns visible
                    fillRect(px0, y + r * scale, w, scale, c);
                }
        }
    }
    return x + sqx(ci * 6 * scale);
}

void Canvas::lFillRect(int lx, int y, int lw, int h, Color c) {
    int x0 = LX(lx), x1 = LX(lx + lw);
    fillRect(x0, y, x1 - x0, h, c);
}

int Canvas::lText(int lx, int y, int scale, const char *s, Color c) {
    return text(LX(lx), y, scale, s, c);
}

bool Canvas::writePAM(const char *path) const {
    FILE *f = fopen(path, "wb");
    if (!f) return false;
    fprintf(f, "P7\nWIDTH %d\nHEIGHT %d\nDEPTH 4\nMAXVAL 255\nTUPLTYPE RGB_ALPHA\nENDHDR\n", PANEL_W, PANEL_H);
    fwrite(px_, 1, (size_t)PANEL_W * PANEL_H * 4, f);
    fclose(f);
    return true;
}

bool Canvas::writePPM(const char *path) const {
    FILE *f = fopen(path, "wb");
    if (!f) return false;
    fprintf(f, "P6\n%d %d\n255\n", PANEL_W, PANEL_H);
    u8 *row = (u8 *)malloc(PANEL_W * 3);
    for (int y = 0; y < PANEL_H; y++) {
        for (int x = 0; x < PANEL_W; x++) {
            const u8 *p = &px_[(y * PANEL_W + x) * 4];
            row[x * 3 + 0] = p[0]; row[x * 3 + 1] = p[1]; row[x * 3 + 2] = p[2];
        }
        fwrite(row, 1, PANEL_W * 3, f);
    }
    free(row);
    fclose(f);
    return true;
}

} // namespace uicore
