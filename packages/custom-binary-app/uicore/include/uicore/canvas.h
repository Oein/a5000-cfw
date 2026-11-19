// Software RGBA8888 canvas, 640x480 physical. Memory byte order R,G,B,A per pixel
// (what the a5000 Rigel UX_DEBUG_CLIENT buffers want; TRACKB_RUNTIME §1.5). Pure
// core code: no platform includes.
#pragma once
#include "uicore/types.h"

namespace uicore {

class Canvas {
public:
    Canvas();
    ~Canvas();

    int width()  const { return PANEL_W; }
    int height() const { return PANEL_H; }
    const u8 *pixels() const { return px_; }   // PANEL_W*PANEL_H*4, RGBA
    u8 *pixels() { return px_; }

    void clear(Color c);
    void setPixel(int x, int y, Color c);      // source-over blend when c.a < 255
    void fillRect(int x, int y, int w, int h, Color c);
    void rectOutline(int x, int y, int w, int h, Color c);
    int  text(int x, int y, int scale, const char *s, Color c);
    int  textWidth(const char *s, int scale) const;

    // Logical (853-wide) helpers: X squeezed by 0.75 to physical.
    void lFillRect(int lx, int y, int lw, int h, Color c);
    int  lText(int lx, int y, int scale, const char *s, Color c);

    bool writePPM(const char *path) const;     // binary P6 (RGB, alpha dropped)
    bool writePAM(const char *path) const;     // binary P7 RGB_ALPHA (keeps alpha; for host compositing)

    // PER-ROW dirty tracking: each write records the changed COLUMN span of the rows it
    // touches, so present() can memcmp/blit ONLY the rows the UI actually drew -- skipping the
    // clean rows BETWEEN far-apart regions (e.g. a menu header at the top + a selected item row
    // lower down). A single bounding box fails here: it spans top..item and present then scans
    // every row between = the fixed ~20ms/present cost (device 2026-11-19). resetDirty() at the
    // start of each render; markDirty() is called by the primitives AND the direct pixel writers
    // (gfx baked text / icons, playback blit). rowDirty(y,..) gives a row's changed span.
    void resetDirty() { for (int i = 0; i < PANEL_H; i++) { rx0_[i] = (short)PANEL_W; rx1_[i] = -1; } }
    void markDirty(int x, int y, int w, int h) {
        if (w <= 0 || h <= 0) return;
        int x1 = x + w - 1, y1 = y + h - 1;
        if (x < 0) x = 0; if (y < 0) y = 0;
        if (x1 > PANEL_W - 1) x1 = PANEL_W - 1; if (y1 > PANEL_H - 1) y1 = PANEL_H - 1;
        if (x1 < x || y1 < y) return;
        for (int r = y; r <= y1; r++) {
            if (x  < rx0_[r]) rx0_[r] = (short)x;
            if (x1 > rx1_[r]) rx1_[r] = (short)x1;
        }
    }
    // Returns false (clean row) or the inclusive changed column span [*x0,*x1].
    bool rowDirty(int y, int *x0, int *x1) const {
        if (rx1_[y] < rx0_[y]) return false;
        *x0 = rx0_[y]; *x1 = rx1_[y]; return true;
    }
private:
    u8 *px_;
    short rx0_[PANEL_H], rx1_[PANEL_H];   // per-row dirty column span; clean when rx1_<rx0_
};

} // namespace uicore
