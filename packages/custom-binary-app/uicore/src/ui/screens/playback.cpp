// M3 core: playback / gallery. Track A Gallery parity (app/camera-ui Gallery.java), restyled for the native
// 640x480 panel (X squeezed 0.75 from the 853x480 logical space):
//   - Thumbnail GRID (default): 4 columns x 3 rows = 12 per page, 3:2 cells, centre-cropped, orange selection
//     frame, page-wise paging (like Track A moveGrid), newest photo first, "n / total" + DCF name on top.
//   - SINGLE view: fit to the logical 853x480 (aspect kept in logical space), prev/next (wraps), OK = info
//     overlay (file / date / size / folder), UP or ZOOM_W = back to the grid, DELETE = confirm modal.
//   - Empty state: black screen + "사진이 없습니다" (golden test/golden/gallery__empty.png).
//   - Input = abstract actions only: NAV_* / DIAL_MAIN_* move, CONFIRM opens / info, BACK or PLAYBACK or MENU
//     leave (single -> grid -> exit), ZOOM_T opens the single view from the grid, DELETE.
// Media comes through the Platform Media interface (list + scaled load -> RGBA); no decoding/file code here.
// Not yet (follow-ups, see README): pinch/zoom + pan of the single view, slideshow, protect/rotate/DPOF,
// movie playback (M3b a5kmpr), EXIF exposure data, async thumbnail decode on the device.
// No platform headers: only uicore canvas/gfx/platform interfaces.
#include "uicore/screen.h"
#include "uicore/app.h"
#include "uicore/canvas.h"
#include "uicore/gfx.h"
#include "uicore/platform.h"
#include "uicore/log.h"
#include <stdio.h>
#include <string.h>
#include <stdlib.h>

namespace uicore {
namespace {

const Color kBlack  = { 0, 0, 0, 255 };
const Color kPlace  = { 0x1a, 0x1a, 0x1a, 255 };        // empty thumb cell (Track A 0xff1a1a1a)
const Color kFrame  = { 0xff, 0xb0, 0x00, 255 };        // selection frame (Track A 0xffffb000)
const Color kWhite  = { 255, 255, 255, 255 };
const Color kHint   = { 0xbf, 0xc2, 0xc6, 255 };        // Track A hint 0xffbfc2c6
const Color kDim    = { 0xb9, 0xbc, 0xc0, 255 };
const Color kCard   = { 0x20, 0x22, 0x26, 255 };
const Color kCardLn = { 0x44, 0x47, 0x4c, 255 };
const Color kBtn    = { 0x3a, 0x3d, 0x42, 255 };

enum {
    COLS = 4, ROWS = 3, PER_PAGE = COLS * ROWS,
    TOP_H = 34, CELL_W = PANEL_W / COLS, CELL_H = 136, GRID_Y = TOP_H, HINT_Y = GRID_Y + ROWS * CELL_H,
    CACHE = 24, THUMB_W = 192, THUMB_H = 144,          // thumbs are requested up to this (>= cell, cropped)
    LOADS_PER_FRAME = 3, FLASH_MS = 1800
};

// ---- bilinear scaled blit of src sub-rect (16.16 fixed) into a canvas rect (clipped) ----
void blit(Canvas &c, int dx, int dy, int dw, int dh, const MediaImage &im, int sx0, int sy0, int sw, int sh) {
    if (dw <= 0 || dh <= 0 || sw <= 0 || sh <= 0 || !im.px) return;
    c.markDirty(dx, dy, dw, dh);   // writes c.pixels() directly below -> report dirty bbox
    long stepX = ((long)sw << 16) / dw, stepY = ((long)sh << 16) / dh;
    u8 *dst = c.pixels();
    for (int y = 0; y < dh; y++) {
        int Y = dy + y; if (Y < 0 || Y >= PANEL_H) continue;
        long fy = ((long)sy0 << 16) + stepY * y + (stepY >> 1) - 0x8000;
        if (fy < 0) fy = 0;
        int y0 = (int)(fy >> 16), wy = (int)((fy >> 8) & 0xff);
        int y1 = y0 + 1; if (y0 >= im.h) y0 = im.h - 1; if (y1 >= im.h) y1 = im.h - 1;
        const u8 *r0 = im.px + (size_t)y0 * im.w * 4, *r1 = im.px + (size_t)y1 * im.w * 4;
        for (int x = 0; x < dw; x++) {
            int X = dx + x; if (X < 0 || X >= PANEL_W) continue;
            long fx = ((long)sx0 << 16) + stepX * x + (stepX >> 1) - 0x8000;
            if (fx < 0) fx = 0;
            int x0 = (int)(fx >> 16), wx = (int)((fx >> 8) & 0xff);
            int x1 = x0 + 1; if (x0 >= im.w) x0 = im.w - 1; if (x1 >= im.w) x1 = im.w - 1;
            u8 *o = dst + ((size_t)Y * PANEL_W + X) * 4;
            for (int ch = 0; ch < 3; ch++) {
                int a = r0[x0 * 4 + ch] * (256 - wx) + r0[x1 * 4 + ch] * wx;
                int b = r1[x0 * 4 + ch] * (256 - wx) + r1[x1 * 4 + ch] * wx;
                o[ch] = (u8)((a * (256 - wy) + b * wy) >> 16);
            }
            o[3] = 255;
        }
    }
}

// Cover-crop an image into a physical cell (Track A CENTER_CROP, aspect judged in logical space).
void blitCover(Canvas &c, int dx, int dy, int dw, int dh, const MediaImage &im) {
    // cell logical aspect = (dw*4/3) : dh ; crop the source to that aspect
    long cellA = (long)dw * 4 * 1000 / (3L * dh), imA = (long)im.w * 1000 / im.h;
    int sx = 0, sy = 0, sw = im.w, sh = im.h;
    if (imA > cellA) { sw = (int)((long)im.h * cellA / 1000); sx = (im.w - sw) / 2; }
    else             { sh = (int)((long)im.w * 1000 / cellA); sy = (im.h - sh) / 2; }
    blit(c, dx, dy, dw, dh, im, sx, sy, sw, sh);
}

void frame(Canvas &c, int x, int y, int w, int h, int t, Color col) {
    for (int i = 0; i < t; i++) c.rectOutline(x + i, y + i, w - 2 * i, h - 2 * i, col);
}

void playTriangle(Canvas &c, int cx, int cy, int r, Color col) {      // movie badge
    for (int y = -r; y <= r; y++) {
        int ay = y < 0 ? -y : y, len = (r - ay) * 3 / 2;
        if (len > 0) c.fillRect(cx - r / 2, cy + y, len, 1, col);
    }
}

class PlaybackScreen : public Screen {
public:
    PlaybackScreen() : grid_(true), sel_(0), infoOn_(false), modal_(false), modalDelete_(false),
                       flashUntil_(0), curIdx_(-1), tick_(0), mediaRef_(0) {
        memset(cache_, 0, sizeof cache_);
        cur_.px = 0; cur_.w = cur_.h = 0;
        flash_[0] = 0;
    }
    ~PlaybackScreen() { dropAll(); }

    Context context() const { return CTX_PLAYBACK; }
    const char *title() const { return "playback"; }

    // -------------------------------------------------------------- input
    void onAction(Action a, int value, App &app) {
        if (value == 0 && a != ACT_NONE) return;                         // key-down / repeat only
        Media *m = app.media();
        int n = m ? m->count() : 0;
        if (sel_ >= n) sel_ = n > 0 ? n - 1 : 0;

        if (modal_) { modalAction(a, app, m, n); return; }
        if (n == 0) { if (a == ACT_BACK || a == ACT_PLAYBACK || a == ACT_MENU) app.popScreen(); return; }

        if (grid_) {
            switch (a) {
                case ACT_NAV_LEFT:  case ACT_DIAL_MAIN_CCW: moveSel(-1, n); break;
                case ACT_NAV_RIGHT: case ACT_DIAL_MAIN_CW:  moveSel(+1, n); break;
                case ACT_NAV_UP:    moveSel(-COLS, n); break;
                case ACT_NAV_DOWN:  moveSel(+COLS, n); break;
                case ACT_CONFIRM: case ACT_ZOOM_T: grid_ = false; infoOn_ = false; break;
                case ACT_DELETE:    askDelete(app, m, n); break;
                case ACT_BACK: case ACT_PLAYBACK: case ACT_MENU: app.popScreen(); break;
                default: break;
            }
        } else {
            switch (a) {
                case ACT_NAV_LEFT:  case ACT_DIAL_MAIN_CCW: wrapSel(-1, n); break;
                case ACT_NAV_RIGHT: case ACT_DIAL_MAIN_CW:  wrapSel(+1, n); break;
                case ACT_NAV_UP: case ACT_ZOOM_W: case ACT_BACK: grid_ = true; infoOn_ = false; break;
                case ACT_CONFIRM: {
                    MediaEntry e;
                    if (m && m->info(pidx(n), &e) && e.movie) flash(app, "동영상 재생은 준비 중입니다");
                    else infoOn_ = !infoOn_;
                    break;
                }
                case ACT_ZOOM_T:    flash(app, "확대는 준비 중입니다"); break;
                case ACT_DELETE:    askDelete(app, m, n); break;
                case ACT_PLAYBACK: case ACT_MENU: app.popScreen(); break;
                default: break;
            }
        }
    }

    // -------------------------------------------------------------- draw
    void draw(Canvas &c, App &app) {
        const Gfx &g = app.gfx();
        Media *m = app.media();
        int n = m ? m->count() : 0;
        if (sel_ >= n) sel_ = n > 0 ? n - 1 : 0;
        tick_++;
        c.clear(kBlack);                                   // playback covers the live view

        if (n == 0) { message(c, g, "사진이 없습니다"); return; }

        int loads = LOADS_PER_FRAME;
        if (grid_) drawGrid(c, g, m, n, &loads); else drawSingle(c, g, m, n);
        drawBars(c, g, m, n);
        if (!grid_ && infoOn_) drawInfo(c, g, m, n);
        if (modal_) drawModal(c, g, m, n);
        if (flash_[0] && app.nowMs() < flashUntil_) drawFlash(c, g);
    }

private:
    bool grid_;
    int  sel_;                     // 0 = newest
    bool infoOn_, modal_, modalDelete_;
    long flashUntil_;
    char flash_[48];

    struct Slot { int key; long use; MediaImage img; };
    Slot cache_[CACHE];            // thumbnail cache, keyed by provider index
    MediaImage cur_; int curIdx_;  // current full preview
    long tick_;
    Media *mediaRef_;              // provider-owned pixels go back through release()

    int pidx(int n) const { return n - 1 - sel_; }                       // screen index -> provider index
    int page() const { return sel_ / PER_PAGE; }

    void moveSel(int d, int n) { int v = sel_ + d; if (v < 0) v = 0; if (v >= n) v = n - 1; sel_ = v; }
    void wrapSel(int d, int n) { sel_ = ((sel_ + d) % n + n) % n; }
    void flash(App &app, const char *s) { strncpy(flash_, s, sizeof flash_ - 1); flash_[sizeof flash_ - 1] = 0; flashUntil_ = app.nowMs() + FLASH_MS; }

    void askDelete(App &app, Media *m, int n) {
        (void)m; (void)n;
        if (!app.opt().allowWrite) { flash(app, "쓰기 금지 모드 (--allow-write)"); return; }
        modal_ = true; modalDelete_ = false;                              // default focus = cancel (safe)
    }

    void modalAction(Action a, App &app, Media *m, int n) {
        switch (a) {
            case ACT_NAV_LEFT: case ACT_NAV_RIGHT: case ACT_NAV_UP: case ACT_NAV_DOWN:
            case ACT_DIAL_MAIN_CW: case ACT_DIAL_MAIN_CCW: modalDelete_ = !modalDelete_; break;
            case ACT_BACK: case ACT_MENU: modal_ = false; break;
            case ACT_CONFIRM:
                modal_ = false;
                if (modalDelete_ && m && m->remove(pidx(n))) {
                    dropAll();                                            // provider indices shifted
                    if (m->count() == 0) grid_ = true;
                    else if (sel_ >= m->count()) sel_ = m->count() - 1;
                    flash(app, "삭제했습니다");
                } else if (modalDelete_) flash(app, "삭제 실패");
                break;
            default: break;
        }
    }

    // ---- caches ----
    void dropAll() {
        for (int i = 0; i < CACHE; i++) {
            if (cache_[i].img.px && mediaRef_) mediaRef_->release(&cache_[i].img);
            cache_[i].img.px = 0; cache_[i].key = 0; cache_[i].use = 0;
        }
        if (cur_.px && mediaRef_) mediaRef_->release(&cur_);
        cur_.px = 0; curIdx_ = -1;
    }

    const MediaImage *thumb(Media *m, int pi, int *budget) {
        mediaRef_ = m;
        for (int i = 0; i < CACHE; i++) if (cache_[i].img.px && cache_[i].key == pi + 1) { cache_[i].use = tick_; return &cache_[i].img; }
        if (*budget <= 0) return 0;
        (*budget)--;
        int victim = 0;
        for (int i = 0; i < CACHE; i++) { if (!cache_[i].img.px) { victim = i; break; } if (cache_[i].use < cache_[victim].use) victim = i; }
        if (cache_[victim].img.px) m->release(&cache_[victim].img);
        cache_[victim].img.px = 0;
        if (!m->load(pi, THUMB_W, THUMB_H, true, &cache_[victim].img)) { cache_[victim].key = 0; return 0; }
        cache_[victim].key = pi + 1; cache_[victim].use = tick_;
        return &cache_[victim].img;
    }

    // ---- drawing ----
    void message(Canvas &c, const Gfx &g, const char *s) {
        int w = g.textWidth(GF_UI_26, s);
        g.text(c, GF_UI_26, (PANEL_W - w) / 2, 240 + g.fontAscent(GF_UI_26) / 2 - 2, s, kWhite, false);
    }

    void drawGrid(Canvas &c, const Gfx &g, Media *m, int n, int *loads) {
        int first = page() * PER_PAGE;
        // cursor first: load the selected thumb before the rest of the page
        for (int pass = 0; pass < 2; pass++) {
            for (int k = 0; k < PER_PAGE; k++) {
                int si = first + k;
                if (si >= n) break;
                bool isSel = (si == sel_);
                if ((pass == 0) != isSel) continue;
                int x = (k % COLS) * CELL_W + 2, y = GRID_Y + (k / COLS) * CELL_H + 2, w = CELL_W - 4, h = CELL_H - 4;
                int pi = n - 1 - si;
                c.fillRect(x, y, w, h, kPlace);
                const MediaImage *im = thumb(m, pi, loads);
                if (im) blitCover(c, x, y, w, h, *im);
                MediaEntry e;
                if (m->info(pi, &e)) {
                    if (e.movie) { playTriangle(c, x + 18, y + h - 20, 12, kWhite); }
                    if (e.prot)  g.text(c, GF_UI_19, x + w - g.textWidth(GF_UI_19, "잠김") - 4, y + h - 6, "잠김", kFrame, true);
                }
                if (isSel) frame(c, x - 2, y - 2, w + 4, h + 4, 3, kFrame);
            }
        }
    }

    void drawSingle(Canvas &c, const Gfx &g, Media *m, int n) {
        int pi = pidx(n);
        mediaRef_ = m;
        if (curIdx_ != pi) {
            if (cur_.px) m->release(&cur_);
            cur_.px = 0; curIdx_ = -1;
            if (m->load(pi, 800, 480, false, &cur_)) curIdx_ = pi;       // 800x480 fit: 720x480 for 3:2 (logical 1:1 vertically)
        }
        if (!cur_.px) { message(c, g, "미리보기 없음"); return; }
        // fit into logical 853x480 keeping aspect there, then squeeze x*0.75 to the panel
        long a = (long)cur_.w * 1000 / cur_.h;                            // aspect in logical space
        int lw, lh;
        if (a >= 853L * 1000 / 480) { lw = 853; lh = (int)(853L * 1000 / a); } else { lh = 480; lw = (int)(480L * a / 1000); }
        int dw = LX(lw), dx = (PANEL_W - dw) / 2, dy = (PANEL_H - lh) / 2;
        blit(c, dx, dy, dw, lh, cur_, 0, 0, cur_.w, cur_.h);
        MediaEntry e;
        if (m->info(pi, &e) && e.movie) {                                  // movie: play badge (OK = not wired yet)
            c.fillRect(PANEL_W / 2 - 34, PANEL_H / 2 - 34, 68, 68, rgba(0, 0, 0, 120));
            playTriangle(c, PANEL_W / 2 - 2, PANEL_H / 2, 24, kWhite);
        }
    }

    void infoLine(char *out, int outN, const MediaEntry &e, int n) {
        char extra[24] = ""; if (e.raw) snprintf(extra, sizeof extra, "   RAW"); 
        snprintf(out, outN, "%d-%04d    %d / %d%s%s", e.folderNo, e.fileNo % 10000, sel_ + 1, n, extra, e.prot ? "   [잠김]" : "");
    }

    void drawBars(Canvas &c, const Gfx &g, Media *m, int n) {
        MediaEntry e;
        bool ok = m->info(pidx(n), &e);
        if (grid_) { c.fillRect(0, 0, PANEL_W, TOP_H, rgba(0, 0, 0, 255)); c.fillRect(0, HINT_Y, PANEL_W, PANEL_H - HINT_Y, rgba(0, 0, 0, 255)); }
        else       { c.fillRect(0, 0, PANEL_W, TOP_H, rgba(0, 0, 0, 120)); c.fillRect(0, PANEL_H - 32, PANEL_W, 32, rgba(0, 0, 0, 120)); }
        if (ok) {
            char line[64];
            if (e.movie) snprintf(line, sizeof line, "%s    %d / %d   %d:%02d", e.name, sel_ + 1, n, e.durationSec / 60, e.durationSec % 60);
            else         infoLine(line, sizeof line, e, n);
            g.text(c, GF_UI_20, 10, 25, line, kWhite, true);
        }
        const char *h = grid_ ? "방향/다이얼 선택   OK 보기   재생 나가기"
                              : (ok && e.movie ? "OK 재생   좌우 이동   위 인덱스   삭제   재생 나가기"
                                               : "좌우 이동   위 인덱스   OK 정보   T 확대   삭제   재생 나가기");
        g.text(c, GF_UI_19, 10, PANEL_H - 10, h, kHint, true);
    }

    void drawInfo(Canvas &c, const Gfx &g, Media *m, int n) {
        MediaEntry e;
        if (!m->info(pidx(n), &e)) return;
        char l[5][64];
        snprintf(l[0], 64, "파일  %s", e.name);
        snprintf(l[1], 64, "날짜  %04d-%02d-%02d  %02d:%02d:%02d", e.year, e.mon, e.day, e.hour, e.min, e.sec);
        snprintf(l[2], 64, "크기  %d x %d", e.w, e.h);
        snprintf(l[3], 64, "용량  %.1f MB", e.size / (1024.0 * 1024.0));
        snprintf(l[4], 64, "폴더  %s%s", e.folder, e.prot ? "   [잠김]" : "");
        int bw = 330, bh = 12 + 5 * 28 + 6;
        c.fillRect(10, TOP_H + 6, bw, bh, rgba(0, 0, 0, 150));
        for (int i = 0; i < 5; i++) g.text(c, GF_UI_20, 20, TOP_H + 6 + 28 * (i + 1) + 2, l[i], kWhite, true);
    }

    void drawModal(Canvas &c, const Gfx &g, Media *m, int n) {
        MediaEntry e; bool ok = m->info(pidx(n), &e);
        c.fillRect(0, 0, PANEL_W, PANEL_H, rgba(0, 0, 0, 150));
        int x = 120, y = 150, w = 400, h = 180;
        c.fillRect(x, y, w, h, kCard); c.rectOutline(x, y, w, h, kCardLn);
        g.text(c, GF_UI_24, x + 20, y + 38, "삭제", kWhite, false);
        if (ok) g.text(c, GF_UI_20, x + 20, y + 76, e.name, kDim, false);
        g.text(c, GF_UI_20, x + 20, y + 104, "삭제하시겠습니까?", kWhite, false);
        const char *lab[2] = { "삭제", "취소" };
        for (int i = 0; i < 2; i++) {
            bool foc = (i == 0) == modalDelete_;
            int bx = x + 20 + i * 190, by = y + h - 46, bw = 170, bh = 34;
            c.fillRect(bx, by, bw, bh, foc ? sonyOrange() : kBtn);
            int tw = g.textWidth(GF_UI_20, lab[i]);
            g.text(c, GF_UI_20, bx + (bw - tw) / 2, by + 24, lab[i], kWhite, false);
        }
    }

    void drawFlash(Canvas &c, const Gfx &g) {
        int tw = g.textWidth(GF_UI_20, flash_), w = tw + 36, x = (PANEL_W - w) / 2, y = PANEL_H - 92;
        c.fillRect(x, y, w, 36, rgba(0x20, 0x22, 0x26, 230));
        g.text(c, GF_UI_20, x + 18, y + 26, flash_, kWhite, false);
    }
};

} // namespace

Screen *createPlaybackScreen() { return new PlaybackScreen(); }

} // namespace uicore
