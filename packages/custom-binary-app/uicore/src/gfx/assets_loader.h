// Loader for the build-time assets produced by uicore/tools/{extract_icons,bake_fonts}.py:
//   icons.bin       RGBA8888 icon atlas ("UIIA")
//   font_<f>_<px>.bin  A8 glyph atlas + metrics ("UIFA")
// File formats: uicore/tools/README.md.
//
// Portable: no platform headers. Bytes come from an AssetSource callback pair; a
// libc FILE* implementation is provided (mmap variant with -DUICORE_ASSETS_MMAP).
// The a6400 port can feed blobs from wherever its mod loader keeps them.
// All header fields are validated (files are derived from firmware; a truncated or
// mismatched file must fail cleanly, never read out of bounds).
#pragma once
#include "uicore/types.h"

namespace uicore {

// ---- byte source ----------------------------------------------------------
struct AssetBlob {
    const u8 *data;
    u32       size;
    void     *handle;   // owned by the source (malloc'd buffer / mapping)
};

struct AssetSource {
    void *ctx;
    bool (*open)(void *ctx, const char *name, AssetBlob *out);
    void (*close)(void *ctx, AssetBlob *blob);
};

// Default source: reads "<dir>/<name>" with fopen/fread (or mmap, see above).
struct FileAssetDir { char dir[192]; };
void assetsFileSource(FileAssetDir *storage, const char *dir, AssetSource *out);

// ---- icons ----------------------------------------------------------------
struct IconRectRT { u16 x, y, w, h; };

class IconAtlas {
public:
    IconAtlas();
    ~IconAtlas();
    // expectChecksum: ICONS_CHECKSUM from gen/icons.h (0 = don't check).
    bool load(const AssetSource &src, const char *name = "icons.bin", u32 expectChecksum = 0);
    void unload();
    bool loaded() const { return pixels_ != 0; }

    int  count() const { return count_; }
    int  find(const char *resName) const;          // -1 if absent
    bool rect(int id, IconRectRT *out) const;
    const char *name(int id) const;
    int  atlasW() const { return w_; }
    int  atlasH() const { return h_; }
    const u8 *pixels() const { return pixels_; }  // RGBA, straight alpha, stride atlasW*4

    // Source-over blit of icon `id` at (x,y) into an RGBA buffer (stride in bytes).
    void blit(u8 *dst, int dstW, int dstH, int stride, int x, int y, int id) const;

private:
    AssetSource src_;
    AssetBlob   blob_;
    const u8   *entries_;
    const char *names_;
    u32         namesSize_;
    const u8   *pixels_;
    u8         *decoded_;   // RLE output (malloc), else 0
    int         count_, w_, h_;
};

// ---- fonts ----------------------------------------------------------------
struct GlyphInfo {
    u32 cp;
    u16 x, y;           // atlas position
    u8  w, h;
    i32 left, top;      // bitmap top-left = (penX + left, baseline - top)
    i32 advanceQ6;      // pen advance in 1/64 px (already includes xscale)
};

class FontAtlas {
public:
    FontAtlas();
    ~FontAtlas();
    bool load(const AssetSource &src, const char *name, u32 expectChecksum = 0);
    void unload();
    bool loaded() const { return alpha_ != 0; }

    int px() const { return px_; }
    int ascent() const { return ascent_; }
    int descent() const { return descent_; }
    int lineHeight() const { return line_; }
    int xscaleQ8() const { return xscaleQ8_; }
    int glyphCount() const { return count_; }
    int atlasW() const { return w_; }
    int atlasH() const { return h_; }
    const u8 *alpha() const { return alpha_; }     // A8, stride atlasW

    bool find(u32 cp, GlyphInfo *out) const;       // binary search

    // Text helpers (UTF-8). Missing glyphs try `fallback` (e.g. OSD -> UI for Hangul),
    // then '?' . Return width in px.
    int measure(const char *utf8, const FontAtlas *fallback = 0) const;
    // Draws with baseline at y. Source-over into RGBA buffer. Returns end pen x.
    int draw(u8 *dst, int dstW, int dstH, int stride, int x, int baselineY,
             const char *utf8, Color c, const FontAtlas *fallback = 0) const;

private:
    AssetSource src_;
    AssetBlob   blob_;
    const u8   *glyphs_;
    const u8   *alpha_;
    int px_, ascent_, descent_, line_, xscaleQ8_, count_, w_, h_;
};

// Decodes one UTF-8 code point and advances *s (invalid bytes -> U+FFFD).
u32 utf8Next(const char **s);

} // namespace uicore
