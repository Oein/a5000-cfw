// Text-input widget, a5000 backend: 1D wheel-scrubbed character strip + assist row.
//
//  +--------------------------------------------------+
//  | title                                            |
//  | [추천] [chip][chip][chip]    <- presets/recent/autocomplete/generated (UP to focus)
//  | [ edit field  text|caret  (ghost completion)  ]  |  live validation line + strength
//  | [abc] ABC 123 #+=                                |  groups (Fn cycles)
//  |   ... x y [ Z ] a b c ...        <- 1D strip, big focused cell, wraps around
//  +--------------------------------------------------+
// Wheel/LEFT/RIGHT scrub, CONFIRM types the focused cell, trash=backspace, zoom lever=caret,
// FN=next group, UP=assist row, MENU/BACK=cancel. Specials live in the strip (space/del/show/done).
#include "text_input.h"
#include "uicore/screen.h"
#include "uicore/app.h"
#include "uicore/canvas.h"
#include "uicore/gfx.h"
#include "uicore/log.h"
#include <math.h>
#include <stdio.h>
#include <string.h>

namespace uicore {
namespace {

const Color kBg     = { 0, 0, 0, 255 };
const Color kSel    = { 0xD3, 0x41, 0x00, 255 };
const Color kAccent = { 0xff, 0x9a, 0x2e, 255 };
const Color kDim    = { 0xb9, 0xbc, 0xc0, 255 };
const Color kFaint  = { 0x6a, 0x6d, 0x72, 255 };
const Color kRule   = { 0x3a, 0x3d, 0x42, 255 };
const Color kWhite  = { 255, 255, 255, 255 };
const Color kCard   = { 0x20, 0x22, 0x26, 255 };
const Color kCardLn = { 0x44, 0x47, 0x4c, 255 };
const Color kOk     = { 0x6b, 0xd0, 0x7a, 255 };
const Color kBad    = { 0xff, 0x6a, 0x4a, 255 };

enum { G_LOWER, G_UPPER, G_DIGIT, G_SYMBOL, G_N };
const char *kGroupName[G_N]  = { "abc", "ABC", "123", "#+=" };
const char *kGroupChars[G_N] = {
    "abcdefghijklmnopqrstuvwxyz", "ABCDEFGHIJKLMNOPQRSTUVWXYZ", "0123456789",
    "-_.:@!#$%&*+=?/\\()[]{}<>,;'\"^`|~" };
const unsigned kGroupBit[G_N] = { TG_LOWER, TG_UPPER, TG_DIGIT, TG_SYMBOL };

enum CellKind { CK_CHAR, CK_SPACE, CK_DEL, CK_SHOW, CK_DONE };
struct Cell { int kind; char ch; };

enum { MAXT = 64, MAXS = 6, STRIP_Y0 = 262, STRIP_H = 112 };

// ---------------------------------------------------------------- tiny drawing helpers
void fillRR(Canvas &c, int x, int y, int w, int h, int r, Color col) {
    for (int j = 0; j < h; j++) {
        int inset = 0;
        float dy = j < r ? (float)(r - j) - 0.5f : (j >= h - r ? (float)(j - (h - r)) + 0.5f : 0.0f);
        if (dy > 0) { float dx = sqrtf((float)(r * r) - dy * dy); inset = (int)((float)r - dx + 0.5f); }
        c.fillRect(x + inset, y + j, w - 2 * inset, 1, col);
    }
}
void cardRR(Canvas &c, int x, int y, int w, int h, int r, Color fill, Color line) {
    fillRR(c, x, y, w, h, r, line);
    fillRR(c, x + 2, y + 2, w - 4, h - 4, r > 2 ? r - 2 : 0, fill);
}
void disc(Canvas &c, int cx, int cy, int r, Color col) {
    for (int y = -r; y <= r; y++) for (int x = -r; x <= r; x++) if (x * x + y * y <= r * r + r / 2) c.setPixel(cx + x, cy + y, col);
}
Color fade(Color a, int pct) { Color o = { (u8)(a.r * pct / 100), (u8)(a.g * pct / 100), (u8)(a.b * pct / 100), 255 }; return o; }
bool ieq(char a, char b) { if (a >= 'A' && a <= 'Z') a += 32; if (b >= 'A' && b <= 'Z') b += 32; return a == b; }
bool prefixI(const char *s, const char *p) { for (; *p; s++, p++) if (!*s || !ieq(*s, *p)) return false; return true; }

// ---------------------------------------------------------------- password generator (host-safe LCG; platform seeds real entropy)
void genPassword(unsigned *st, char *out, int n) {
    struct R { static unsigned nx(unsigned *s) { *s = *s * 1664525u + 1013904223u; return *s >> 12; } };
    static const char L[] = "abcdefghijkmnpqrstuvwxyz", U[] = "ABCDEFGHJKLMNPQRSTUVWXYZ", D[] = "23456789", S[] = "-_.!#$%";
    const char *cls[4] = { L, U, D, S };
    int len[4] = { 24, 24, 8, 7 };
    int w = n - 1; if (w > 12) w = 12;
    char tmp[16];
    for (int i = 0; i < w; i++) { int k = i < 4 ? i : (int)(R::nx(st) % 3); tmp[i] = cls[k][R::nx(st) % len[k]]; }
    for (int i = w - 1; i > 0; i--) { int j = (int)(R::nx(st) % (i + 1)); char t = tmp[i]; tmp[i] = tmp[j]; tmp[j] = t; }
    memcpy(out, tmp, w); out[w] = 0;
}

// ================================================================ the widget screen
class TextInputScreen : public Screen {
public:
    explicit TextInputScreen(const TextEditRequest &r) : lastNow_(0), lastWheel_(0), seed_(1234567u) { load(r); }
    Context context() const { return CTX_TEXT; }
    const char *title() const { return req_.field.title; }

    void load(const TextEditRequest &r) {
        req_ = r;
        const TextConstraints &c = req_.c;
        if (!c.groups) req_.c.groups = TG_ALL;
        if (c.seed) seed_ = c.seed;
        len_ = 0; text_[0] = 0;
        for (const char *p = r.field.initial ? r.field.initial : ""; *p && len_ < MAXT && len_ < c.maxLen; p++)
            if (*p >= 32 && *p < 127) text_[len_++] = *p;
        text_[len_] = 0;
        caret_ = len_; selAll_ = len_ > 0;
        show_ = false; lastTypedMs_ = -100000; flash_[0] = 0; flashUntil_ = 0;
        focus_ = 1; sugSel_ = 0; sugStart_ = 0; auto_ = false;
        for (int g = 0; g < G_N; g++) pos_[g] = 0;
        group_ = firstGroup();
        syncAutoCap();
        rebuildGroup();
        rebuildSuggestions();
    }

    // ------------------------------------------------------------ input
    void onAction(Action a, int value, App &app) {
        if (value == 0 && a != ACT_NONE) return;                 // key down / repeat only
        if (flash_[0]) flash_[0] = 0;
        long now = app.nowMs();
        switch (a) {
            case ACT_DIAL_MAIN_CW: case ACT_NAV_RIGHT: scrub(+wheelStep(now)); break;
            case ACT_DIAL_MAIN_CCW: case ACT_NAV_LEFT: scrub(-wheelStep(now)); break;
            case ACT_DIAL_SUB_CW:  scrub(+8); break;                   // coarse scrub (a6400 rear/front dial)
            case ACT_DIAL_SUB_CCW: scrub(-8); break;
            case ACT_NAV_UP:   if (nSug_ > 0) { focus_ = 0; if (sugSel_ >= nSug_) sugSel_ = 0; } break;
            case ACT_NAV_DOWN: focus_ = 1; break;
            case ACT_CONFIRM:  if (focus_ == 0) applySuggestion(sugSel_); else activate(app); break;
            case ACT_FN:       nextGroup(); break;
            case ACT_DELETE:   backspace(); break;
            case ACT_ZOOM_T:   selAll_ = false; if (caret_ < len_) caret_++; break;
            case ACT_ZOOM_W:   selAll_ = false; if (caret_ > 0) caret_--; break;
            case ACT_BACK: case ACT_MENU: finish(app, false); return;
            default: break;
        }
        lastNow_ = now;
    }

    int wheelStep(long now) {          // fast spin = bigger steps (no effect on single clicks)
        int dt = (int)(now - lastWheel_); lastWheel_ = now;
        return (dt > 0 && dt < 35) ? 3 : (dt > 0 && dt < 70) ? 2 : 1;
    }

    void scrub(int d) {
        if (focus_ == 0) { if (nSug_ > 0) sugSel_ = ((sugSel_ + d) % nSug_ + nSug_) % nSug_; return; }
        int n = nCells_;
        if (d > 1 || d < -1) {         // coarse: stay on characters, clamp (no wrap through the specials)
            int nc = nChars_; int p = pos_[group_] + d;
            if (p < 0) p = 0; if (p >= nc) p = nc - 1;
            pos_[group_] = p; return;
        }
        pos_[group_] = ((pos_[group_] + d) % n + n) % n;
    }

    void nextGroup() {
        for (int i = 1; i <= G_N; i++) {
            int g = (group_ + i) % G_N;
            if (req_.c.groups & kGroupBit[g]) { group_ = g; break; }
        }
        auto_ = false; focus_ = 1; rebuildGroup();
    }
    int firstGroup() const {
        for (int g = 0; g < G_N; g++) if (req_.c.groups & kGroupBit[g]) return g;
        return G_LOWER;
    }

    void activate(App &app) {
        const Cell &k = cells_[pos_[group_]];
        switch (k.kind) {
            case CK_CHAR:  insert(k.ch); break;
            case CK_SPACE: insert(' '); break;
            case CK_DEL:   backspace(); break;
            case CK_SHOW:  show_ = !show_; break;
            case CK_DONE:  tryCommit(app); break;
        }
    }

    void insert(char ch) {
        if (ch == ' ' && !req_.c.allowSpace) { flash("공백은 사용할 수 없음"); return; }
        if (selAll_) { len_ = 0; caret_ = 0; text_[0] = 0; selAll_ = false; }
        if (len_ >= req_.c.maxLen || len_ >= MAXT) { char b[40]; snprintf(b, sizeof b, "최대 %d자", req_.c.maxLen); flash(b); return; }
        memmove(text_ + caret_ + 1, text_ + caret_, len_ - caret_ + 1);
        text_[caret_++] = ch; len_++;
        lastTypedMs_ = lastNow_;
        textChanged();
    }
    void backspace() {
        if (selAll_) { len_ = 0; caret_ = 0; text_[0] = 0; selAll_ = false; textChanged(); return; }
        if (caret_ <= 0) return;
        memmove(text_ + caret_ - 1, text_ + caret_, len_ - caret_ + 1);
        caret_--; len_--;
        lastTypedMs_ = -100000;
        textChanged();
    }
    void textChanged() { syncAutoCap(); rebuildSuggestions(); if (focus_ == 0 && nSug_ == 0) focus_ = 1; }

    void syncAutoCap() {
        if (!req_.c.autoCap) return;
        bool wordStart = caret_ == 0 || text_[caret_ - 1] == ' ';
        if (wordStart && group_ == G_LOWER && (req_.c.groups & TG_UPPER)) { group_ = G_UPPER; auto_ = true; rebuildGroup(); }
        else if (auto_ && group_ == G_UPPER && !wordStart) { group_ = G_LOWER; auto_ = false; rebuildGroup(); }
    }

    void flash(const char *m) { snprintf(flash_, sizeof flash_, "%s", m); flashUntil_ = 1; }

    void tryCommit(App &app) {
        TextValidity v; textValidate(req_.c, text_, &v);
        if (v.level >= 2) { flash(v.msg); logf("textinput: commit blocked (%s)", v.msg); return; }
        finish(app, true);
    }

    void finish(App &app, bool ok) {
        TextEditRequest next;
        bool chain = req_.cb ? req_.cb(req_.user, ok, text_, &next) : false;
        if (chain && ok) { load(next); return; }       // re-target in place (e.g. SSID -> password)
        app.popScreen();
    }

    // ------------------------------------------------------------ model
    void rebuildGroup() {
        nCells_ = 0; nChars_ = 0;
        const char *s = kGroupChars[group_];
        for (; *s; s++) { cells_[nCells_].kind = CK_CHAR; cells_[nCells_].ch = *s; nCells_++; nChars_++; }
        Cell sp = { CK_SPACE, ' ' }, del = { CK_DEL, 0 }, sh = { CK_SHOW, 0 }, dn = { CK_DONE, 0 };
        if (req_.c.allowSpace) cells_[nCells_++] = sp;
        cells_[nCells_++] = del;
        if (req_.c.secret) cells_[nCells_++] = sh;
        cells_[nCells_++] = dn;
        if (pos_[group_] >= nCells_) pos_[group_] = 0;
    }

    void addSug(const char *s) {
        if (!s || !*s || nSug_ >= MAXS) return;
        if ((int)strlen(s) > req_.c.maxLen) return;
        for (int i = 0; i < nSug_; i++) if (!strcmp(sug_[i], s)) return;
        if (!strcmp(s, text_)) return;
        snprintf(sug_[nSug_++], sizeof sug_[0], "%s", s);
    }
    void rebuildSuggestions() {
        nSug_ = 0;
        const TextConstraints &c = req_.c;
        bool typed = len_ > 0 && !selAll_;
        if (c.secret) {                                         // no history for secrets: generated only, on empty field
            if (c.suggestGenerated && (len_ == 0 || selAll_)) {
                for (int i = 0; i < 3; i++) { char b[24]; genPassword(&seed_, b, c.maxLen + 1); addSug(b); }
            }
        } else {
            for (int i = 0; i < c.nRecent; i++) if (!typed || prefixI(c.recent[i], text_)) addSug(c.recent[i]);
            for (int i = 0; i < c.nPresets; i++) if (!typed || prefixI(c.presets[i], text_)) addSug(c.presets[i]);
        }
        if (sugSel_ >= nSug_) sugSel_ = 0;
        sugStart_ = 0;
    }
    void applySuggestion(int i) {
        if (i < 0 || i >= nSug_) return;
        snprintf(text_, sizeof text_, "%s", sug_[i]);
        len_ = (int)strlen(text_); caret_ = len_; selAll_ = false; lastTypedMs_ = -100000;
        focus_ = 1; syncAutoCap(); rebuildSuggestions();
        logf("textinput: suggestion -> %s", req_.c.secret ? "(secret)" : text_);
    }

    // ------------------------------------------------------------ drawing
    void draw(Canvas &c, App &app) {
        const Gfx &g = app.gfx();
        lastNow_ = app.nowMs();
        c.clear(kBg);
        g.text(c, GF_UI_27, 15, 34, req_.field.title, kAccent, false);
            c.fillRect(0, 50, PANEL_W, 2, kRule);
        drawAssist(c, g);
        drawField(c, g);
        drawTabs(c, g);
        drawStrip(c, g);
        c.fillRect(0, 443, PANEL_W, 2, kRule);
        const char *h = "휠 선택  \xe2\x97\x8f 입력  Fn 종류  \xe2\x96\xb2 추천  휴지통 지움  MENU 취소";
        g.text(c, GF_UI_20, PANEL_W - 10 - g.textWidth(GF_UI_20, h), 468, h, kDim, false);
    }

    void drawAssist(Canvas &c, const Gfx &g) {
        g.text(c, GF_UI_20, 15, 86, req_.c.secret ? "생성" : "추천", focus_ == 0 ? kAccent : kDim, false);
        int x0 = 66, x = x0;
        if (nSug_ == 0) {
            const char *m = req_.c.secret ? "글자를 지우면 안전한 비밀번호를 추천합니다" : "입력하면 자동완성이 여기에 표시됩니다";
            g.text(c, GF_UI_20, x0, 86, m, kFaint, false);
            return;
        }
        // keep the selected chip visible: advance sugStart_
        for (;;) {
            int xx = x0; bool vis = true;
            for (int i = sugStart_; i <= sugSel_; i++) { xx += chipW(g, i) + 8; if (xx > PANEL_W - 14 && i == sugSel_ && sugStart_ < sugSel_) vis = false; }
            if (vis) break;
            sugStart_++;
        }
        if (sugStart_ > 0) g.text(c, GF_UI_19, 52, 84, "\xe2\x97\x80", kFaint, false);
        bool typed = len_ > 0 && !selAll_;
        for (int i = sugStart_; i < nSug_; i++) {
            int w = chipW(g, i);
            if (x + w > PANEL_W - 10) { g.text(c, GF_UI_19, PANEL_W - 22, 84, "\xe2\x96\xb6", kFaint, false); break; }
            bool sel = (i == sugSel_);
            bool hot = sel && focus_ == 0;
            cardRR(c, x, 62, w, 36, 8, hot ? kSel : kCard, hot ? kSel : (sel ? kAccent : kCardLn));
            const char *s = sug_[i];
            if (req_.c.secret) { g.text(c, GF_UI_20, x + 12, 87, s, hot ? kWhite : kDim, false); }
            else if (typed && prefixI(s, text_)) {     // typed part dim, completion bright
                char head[MAXT + 1]; int n = len_; memcpy(head, s, n); head[n] = 0;
                int hw = g.textWidth(GF_UI_20, head);
                g.text(c, GF_UI_20, x + 12, 87, head, hot ? kWhite : kFaint, false);
                g.text(c, GF_UI_20, x + 12 + hw, 87, s + n, hot ? kWhite : kWhite, false);
            } else g.text(c, GF_UI_20, x + 12, 87, s, hot ? kWhite : kDim, false);
            x += w + 8;
        }
    }
    int chipW(const Gfx &g, int i) const { return g.textWidth(GF_UI_20, sug_[i]) + 24; }

    // character i of the edit field as displayed; width
    bool revealed(int i, long now) const { return show_ || (i == caret_ - 1 && now - lastTypedMs_ < 1800); }
    int charW(const Gfx &g, int i, long now) const {
        if (req_.c.secret && !revealed(i, now)) return 17;
        char b[2] = { text_[i], 0 }; return g.textWidth(GF_UI_27, b);
    }

    void drawField(Canvas &c, const Gfx &g) {
        TextValidity v; textValidate(req_.c, text_, &v);
        const int bx = 12, by = 108, bw = PANEL_W - 24, bh = 62;
        Color edge = v.level >= 2 ? kBad : kSel;
        cardRR(c, bx, by, bw, bh, 8, kCard, edge);
        long now = lastNow_;
        int innerL = bx + 14, innerR = bx + bw - 76;
        // window: choose start so the caret stays visible
        int start = 0;
        for (;;) {
            int w = 0; for (int i = start; i < caret_; i++) w += charW(g, i, now);
            if (w <= innerR - innerL - 24 || start >= caret_) break;
            start++;
        }
        int x = innerL;
        if (selAll_ && len_ > 0) {                                 // select-all highlight
            int w = 0; for (int i = 0; i < len_; i++) w += charW(g, i, now);
            c.fillRect(x - 3, by + 9, (w < innerR - innerL ? w : innerR - innerL) + 6, bh - 18, rgba(0xD3, 0x41, 0x00, 150));
        }
        int caretX = x;
        for (int i = start; i <= len_; i++) {
            if (i == caret_) caretX = x;
            if (i == len_) break;
            int w = charW(g, i, now);
            if (x + w > innerR) break;
            if (req_.c.secret && !revealed(i, now)) disc(c, x + 8, by + 33, 5, kWhite);
            else { char b[2] = { text_[i], 0 }; g.text(c, GF_UI_27, x, by + 42, b, kWhite, false); }
            x += w;
        }
        if (!selAll_) {
            c.fillRect(caretX, by + 12, 2, bh - 24, kAccent);
            // ghost autocomplete of the best match (caret at end)
            if (!req_.c.secret && caret_ == len_ && len_ > 0 && nSug_ > 0 && prefixI(sug_[0], text_) && caretX < innerR - 40)
                g.text(c, GF_UI_27, caretX + 3, by + 42, sug_[0] + len_, kFaint, false);
        }
        char cnt[16]; snprintf(cnt, sizeof cnt, "%d/%d", len_, req_.c.maxLen);
        g.text(c, GF_UI_20, bx + bw - 12 - g.textWidth(GF_UI_20, cnt), by + 54, cnt, v.level >= 2 ? kBad : kDim, false);
        // validation / flash line
        const char *msg = flash_[0] ? flash_ : v.msg;
        Color mc = flash_[0] ? kBad : (v.level >= 2 ? kBad : (v.level == 1 ? kAccent : kOk));
        g.text(c, GF_UI_20, 16, 198, msg, mc, false);
        if (req_.c.secret && len_ > 0) {                           // strength bar (3 segments)
            static const char *nm[4] = { "", "약함", "보통", "강함" };
            Color sc = v.strength <= 1 ? kBad : (v.strength == 2 ? kAccent : kOk);
            int bx2 = PANEL_W - 12 - 3 * 28;
            for (int i = 0; i < 3; i++) c.fillRect(bx2 + i * 28, 186, 24, 6, i < v.strength ? sc : kRule);
            if (v.strength) g.text(c, GF_UI_19, bx2 - 8 - g.textWidth(GF_UI_19, nm[v.strength]), 198, nm[v.strength], sc, false);
        } else if (req_.c.prompt && !flash_[0] && v.level == 0 && !len_)
            g.text(c, GF_UI_20, 16, 198, req_.c.prompt, kDim, false);
    }

    void drawTabs(Canvas &c, const Gfx &g) const {
        int x = 14;
        for (int i = 0; i < G_N; i++) {
            if (!(req_.c.groups & kGroupBit[i])) continue;
            int w = g.textWidth(GF_UI_24, kGroupName[i]) + 28;
            bool on = i == group_;
            fillRR(c, x, 212, w, 34, 8, on ? kSel : kCard);
            g.text(c, GF_UI_24, x + 14, 237, kGroupName[i], on ? kWhite : kDim, false);
            x += w + 6;
        }
        const char *h = "Fn 으로 전환";
        g.text(c, GF_UI_19, PANEL_W - 14 - g.textWidth(GF_UI_19, h), 236, h, kFaint, false);
    }

    int cellW(const Gfx &g, const Cell &k) const {
        switch (k.kind) {
            case CK_CHAR: return 62;
            case CK_SPACE: return g.textWidth(GF_UI_24, "공백") + 34;
            case CK_DEL:   return g.textWidth(GF_UI_24, "지움") + 34;
            case CK_SHOW:  return g.textWidth(GF_UI_24, show_ ? "숨김" : "표시") + 34;
            default:       return g.textWidth(GF_UI_24, "완료") + 34;
        }
    }
    const char *cellLabel(const Cell &k) const {
        switch (k.kind) { case CK_SPACE: return "공백"; case CK_DEL: return "지움"; case CK_SHOW: return show_ ? "숨김" : "표시"; default: return "완료"; }
    }

    void drawCell(Canvas &c, const Gfx &g, const Cell &k, int x, int w, bool sel, int dist) const {
        int pct = dist == 0 ? 100 : (dist == 1 ? 80 : (dist == 2 ? 60 : 42));
        if (k.kind == CK_CHAR) {
            char b[2] = { k.ch, 0 };
            if (sel) {
                fillRR(c, x, STRIP_Y0 + 2, w, STRIP_H - 4, 12, kSel);
                g.textBig(c, GF_OSD_40, x + w / 2, STRIP_Y0 + STRIP_H / 2 - 2, b, kWhite, 2, 2, false);
            } else {
                fillRR(c, x + 3, STRIP_Y0 + 20, w - 6, STRIP_H - 40, 8, kCard);
                g.textBig(c, GF_OSD_40, x + w / 2, STRIP_Y0 + STRIP_H / 2, b, fade(kWhite, pct), 1, 1, false);
            }
        } else {
            Color bg = sel ? (k.kind == CK_DONE ? kSel : kSel) : (k.kind == CK_DONE ? rgb(0x5a, 0x2a, 0x10) : kCard);
            if (sel) fillRR(c, x, STRIP_Y0 + 2, w, STRIP_H - 4, 12, bg);
            else fillRR(c, x + 3, STRIP_Y0 + 20, w - 6, STRIP_H - 40, 8, bg);
            const char *t = cellLabel(k);
            g.text(c, GF_UI_24, x + (w - g.textWidth(GF_UI_24, t)) / 2, STRIP_Y0 + STRIP_H / 2 + 9, t, sel ? kWhite : fade(kAccent, pct + 20 > 100 ? 100 : pct + 20), false);
        }
    }

    void drawStrip(Canvas &c, const Gfx &g) const {
        c.fillRect(0, STRIP_Y0 - 6, PANEL_W, STRIP_H + 12, rgb(0x10, 0x11, 0x13));
        int n = nCells_, p = pos_[group_];
        bool focusStrip = focus_ == 1;
        int selW = cellW(g, cells_[p]); if (cells_[p].kind == CK_CHAR) selW = 84;
        int cx = PANEL_W / 2;
        int sx = cx - selW / 2;
        drawCellFocus(c, g, cells_[p], sx, selW, focusStrip);
        int x = sx;
        for (int d = 1; d <= 6; d++) {                 // right side (wraps)
            const Cell &k = cells_[(p + d) % n];
            int w = cellW(g, k); x += (d == 1 ? selW : cellW(g, cells_[(p + d - 1) % n])) + 2; if (x > PANEL_W) break;
            drawCell(c, g, k, x, w, false, d);
        }
        x = sx;
        for (int d = 1; d <= 6; d++) {                 // left side
            const Cell &k = cells_[((p - d) % n + n) % n];
            int w = cellW(g, k); x -= w + 2; if (x + w < 0) break;
            drawCell(c, g, k, x, w, false, d);
        }
        // scrub position
        char ps[32]; snprintf(ps, sizeof ps, "%d / %d", p + 1, n);
        g.text(c, GF_UI_19, cx - g.textWidth(GF_UI_19, ps) / 2, STRIP_Y0 + STRIP_H + 32, ps, kFaint, false);
        for (int i = 0; i < n; i++) {                  // mini map: one tick per cell (whole group at a glance)
            int tx = 16 + i * (PANEL_W - 32) / n, tw = (PANEL_W - 32) / n - 1; if (tw < 1) tw = 1;
            c.fillRect(tx, STRIP_Y0 + STRIP_H + 40, tw, 4, i == p ? kAccent : (cells_[i].kind == CK_CHAR ? kRule : kCardLn));
        }
    }
    void drawCellFocus(Canvas &c, const Gfx &g, const Cell &k, int x, int w, bool active) const {
        if (active) drawCell(c, g, k, x, w, true, 0);
        else {                                           // assist row has focus: strip shown but dim
            drawCell(c, g, k, x, w, false, 1);
        }
    }

private:
    TextEditRequest req_;
    char text_[MAXT + 1]; int len_, caret_;
    bool selAll_, show_, auto_;
    long lastTypedMs_, lastNow_, lastWheel_; int flashUntil_;
    char flash_[72];
    int group_, pos_[G_N], focus_;
    Cell cells_[48]; int nCells_, nChars_;
    char sug_[MAXS][MAXT + 1]; int nSug_, sugSel_, sugStart_;
    unsigned seed_;
};

} // namespace

// ---------------------------------------------------------------- validation
void textValidate(const TextConstraints &c, const char *t, TextValidity *o) {
    int n = (int)strlen(t);
    o->level = 0; o->strength = 0; o->msg[0] = 0;
    int cls = 0, hasL = 0, hasU = 0, hasD = 0, hasS = 0;
    for (const char *p = t; *p; p++) {
        if (*p >= 'a' && *p <= 'z') hasL = 1; else if (*p >= 'A' && *p <= 'Z') hasU = 1;
        else if (*p >= '0' && *p <= '9') hasD = 1; else hasS = 1;
    }
    cls = hasL + hasU + hasD + hasS;
    if (c.secret) { int s = cls + (n >= 12) + (n >= 16); o->strength = n < 8 ? 1 : (s <= 2 ? 1 : (s == 3 ? 2 : 3)); }
    switch (c.validator) {
        case TV_WIFI_PSK:
            if (n < 8) { o->level = 2; snprintf(o->msg, sizeof o->msg, "비밀번호는 8~63자 (현재 %d자, %d자 더 필요)", n, 8 - n); }
            else snprintf(o->msg, sizeof o->msg, "사용 가능  %d자", n);
            return;
        case TV_SSID:
            if (n < 1) { o->level = 2; snprintf(o->msg, sizeof o->msg, "SSID를 입력하세요 (1~32자)"); }
            else if (t[0] == ' ' || t[n - 1] == ' ') { o->level = 1; snprintf(o->msg, sizeof o->msg, "앞뒤 공백은 연결 문제를 일으킬 수 있음"); }
            else snprintf(o->msg, sizeof o->msg, "사용 가능  %d/32바이트", n);
            return;
        default:
            if (n < c.minLen) { o->level = 2; snprintf(o->msg, sizeof o->msg, "%d자 이상 입력하세요", c.minLen); }
            else snprintf(o->msg, sizeof o->msg, "사용 가능");
            return;
    }
}

// ---------------------------------------------------------------- editText (backend dispatch)
void editText(App &app, const TextField &f, const TextConstraints &c, TextEditDone cb, void *user) {
    TextEditRequest r; r.field = f; r.c = c; r.cb = cb; r.user = user;
    if (c.backend == TB_TOUCH_QWERTY) logf("textinput: touch-QWERTY backend not implemented -> wheel strip");
    // Backend selection hook: a6400 (has_touch) -> createTouchQwerty(r); default = wheel strip.
    app.pushScreen(new TextInputScreen(r));
}

// ---------------------------------------------------------------- TEXT catalog fields (RAM-only host-sim store)
namespace {
const char *const kDevPresets[] = { "ILCE-5000", "a5000-Street", "a5000-Studio", "a5000-Travel", "Camera-A" };
const char *const kSsidPresets[] = { "DIRECT-a5000", "a5000-AP", "ILCE-5000-WiFi" };

enum { F_DEVNAME, F_SSID, F_PSK, F_N };

const TextFieldDef kFields[F_N] = {
    // device name: apply route = CT SET_DEVICE_NAME (NATIVE_PORT_PLAN section 5/9). Max length [?] confirm on device.
    { "devname", "sys-devicename", "장치 이름 편집",
      { "devname", "장치 이름 편집", 0 },
      { 1, 16, TG_ALL, false, true, true, false, TV_NAME, "카메라가 Wi-Fi/PC에 보이는 이름",
        kDevPresets, 5, 0, 0, TB_AUTO, 0 },
      "ILCE-5000", "CT SET_DEVICE_NAME (host-sim: RAM only)", -1, -1 },
    // Wi-Fi SSID: chained into password. Route = CT/Direct (section 5 W3, [?] not confirmed).
    { "wifi.ssid", "info-na-wifi-config", "SSID/PW 재설정",
      { "wifi.ssid", "Wi-Fi SSID", 0 },
      { 1, 32, TG_ALL, false, true, false, false, TV_SSID, "다음 단계에서 비밀번호를 설정합니다",
        kSsidPresets, 3, 0, 0, TB_AUTO, 0 },
      "DIRECT-a5000", "CT/Direct W3 (host-sim: RAM only)", F_PSK, -1 },
    { "wifi.psk", 0, "",
      { "wifi.psk", "Wi-Fi 비밀번호", 0 },
      { 8, 63, TG_ALL, true, true, false, true, TV_WIFI_PSK, "8~63자 (생성 추천 사용 가능)",
        0, 0, 0, 0, TB_AUTO, 0 },
      "a5000wifi", "CT/Direct W3 (host-sim: RAM only)", -1, F_SSID },
};

struct Store { char val[F_N][MAXT + 1]; char recent[F_N][4][MAXT + 1]; const char *rp[F_N][4]; int nRecent[F_N]; bool init; };
Store g_store;

void storeInit() {
    if (g_store.init) return;
    for (int i = 0; i < F_N; i++) { snprintf(g_store.val[i], sizeof g_store.val[i], "%s", kFields[i].defVal); g_store.nRecent[i] = 0; }
    g_store.init = true;
}
int fieldIndex(const char *id) { for (int i = 0; i < F_N; i++) if (!strcmp(kFields[i].id, id)) return i; return -1; }


void fillRequest(int i, TextEditRequest *r);
bool onDone(void *user, bool committed, const char *text, TextEditRequest *next) {
    int i = (int)(long)user;
    if (!committed) return false;
    storeInit();
    snprintf(g_store.val[i], sizeof g_store.val[i], "%s", text);
    const TextFieldDef &d = kFields[i];
    if (!d.c.secret) {                                       // recent list (MRU, dedup)
        int n = g_store.nRecent[i], j = 0;
        char tmp[4][MAXT + 1]; int tn = 0;
        snprintf(tmp[tn++], sizeof tmp[0], "%s", text);
        for (j = 0; j < n && tn < 4; j++) if (strcmp(g_store.recent[i][j], text)) snprintf(tmp[tn++], sizeof tmp[0], "%s", g_store.recent[i][j]);
        for (j = 0; j < tn; j++) snprintf(g_store.recent[i][j], sizeof g_store.recent[0][0], "%s", tmp[j]);
        g_store.nRecent[i] = tn;
    }
    logf("textinput: commit %s = %s  [route: %s]", d.id, d.c.secret ? "(masked)" : text, d.route);
    if (d.next >= 0) { fillRequest(d.next, next); return true; }
    return false;
}
void fillRequest(int i, TextEditRequest *r) {
    storeInit();
    const TextFieldDef &d = kFields[i];
    r->field = d.field; r->field.initial = g_store.val[i];
    r->c = d.c;
    for (int j = 0; j < g_store.nRecent[i]; j++) g_store.rp[i][j] = g_store.recent[i][j];
    r->c.recent = g_store.rp[i]; r->c.nRecent = g_store.nRecent[i];
    r->cb = onDone; r->user = (void *)(long)i;
}
} // namespace

const TextFieldDef *findTextField(const char *key, const char *label) {
    for (int i = 0; i < F_N; i++)
        if (kFields[i].menuKey && !strcmp(kFields[i].menuKey, key) && !strcmp(kFields[i].menuLabel, label)) return &kFields[i];
    return 0;
}
void openTextField(App &app, const TextFieldDef &d) {
    int i = fieldIndex(d.id); if (i < 0) return;
    TextEditRequest r; fillRequest(i, &r);
    editText(app, r.field, r.c, r.cb, r.user);
}
void textFieldSummary(const TextFieldDef &d, char *out, int n) {
    storeInit();
    int i = d.summaryOf >= 0 ? d.summaryOf : fieldIndex(d.id);
    snprintf(out, n, "%s", g_store.val[i]);
}
const char *textFieldValue(const char *id) { storeInit(); int i = fieldIndex(id); return i < 0 ? 0 : g_store.val[i]; }

} // namespace uicore
