// Gfx: OSD text + icon drawing on a Canvas using the baked Sony font/icon atlases
// (uicore/tools). Falls back to the built-in 5x7 font and no icons when the asset
// files are absent (device without the asset directory, bare checkouts).
#pragma once
#include "uicore/types.h"
#include "uicore/gen_fonts_ids.h"

namespace uicore {

class Canvas;
class FontAtlas;
class IconAtlas;

class Gfx {
public:
    Gfx();
    ~Gfx();
    // Loads icons.bin + the OSD fonts from dir. Returns true if fonts and icons loaded.
    bool load(const char *dir);
    bool hasFonts() const { return fontsOk_; }
    bool hasIcons() const { return iconsOk_; }

    // Draw text with a dark outline (Sony OSD look). Baseline-anchored. Returns the end x.
    int  text(Canvas &c, int fontId, int x, int baseline, const char *s, Color col, bool outline = true) const;
    // Large text: renders `s` at the base size then bilinear-scales alpha by sx/sy (integer
    // multiples), centred on (cx,cy), with a dark outline. For the self-timer number.
    void textBig(Canvas &c, int fontId, int cx, int cy, const char *s, Color col, int sx, int sy, bool outline = true) const;
    int  textWidth(int fontId, const char *s) const;
    int  fontAscent(int fontId) const;

    bool iconSize(int iconId, int *w, int *h) const;
    // Source-over blit at physical (x,y); alpha 0..255 multiplies the icon's own alpha.
    bool icon(Canvas &c, int iconId, int x, int y, int alpha = 255) const;
    // Nearest-neighbour scaled blit (num/den of native size). Writes the scaled w/h to
    // outW/outH (for centring). Used to render pictograms a touch smaller in tight cells.
    bool iconScaled(Canvas &c, int iconId, int x, int y, int num, int den,
                    int *outW = 0, int *outH = 0, int alpha = 255) const;
    int  iconByName(const char *resName) const;

private:
    FontAtlas *fonts_[GF_COUNT];
    IconAtlas *icons_;
    bool fontsOk_, iconsOk_;
    void *src_;       // FileAssetDir (kept alive for lazy sources)
};

} // namespace uicore
