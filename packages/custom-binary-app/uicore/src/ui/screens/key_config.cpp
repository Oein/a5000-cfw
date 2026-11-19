// Custom Key settings screen (사용자 키 설정): lists each customizable shooting key with its
// currently-assigned function; CONFIRM/RIGHT opens a function picker; choosing one updates the
// shared shooting Custom Key table (keymap_shoot) and persists it (shootKeymapSave -> conf file).
// Reached from the main menu's "custom-key" item (see menu_cascade.cpp). Pure uicore: no platform
// headers. Visual language matches the cascade menu (Sony #D34100 selection, rules, dim rows).
#include "uicore/screen.h"
#include "uicore/app.h"
#include "uicore/canvas.h"
#include "uicore/gfx.h"
#include "uicore/keymap_shoot.h"
#include <stdio.h>
#include <string.h>

namespace uicore {
namespace {

const Color kBg       = { 0, 0, 0, 255 };
const Color kSel      = { 0xD3, 0x41, 0x00, 255 };   // focused selection (Sony active)
const Color kAccent   = { 0xff, 0x9a, 0x2e, 255 };
const Color kWhite    = { 255, 255, 255, 255 };
const Color kDim      = { 0xb9, 0xbc, 0xc0, 255 };
const Color kRule     = { 0x3a, 0x3d, 0x42, 255 };
const Color kCardBg   = { 0x20, 0x22, 0x26, 255 };
const Color kCardLine = { 0x44, 0x47, 0x4c, 255 };

// UTF-8 aware truncation to fit maxPx physical pixels (whole characters).
void fitPx(const Gfx &g, int font, const char *s, int maxPx, char *out, int outN) {
    int n = (int)strlen(s);
    if (n >= outN) n = outN - 1;
    memcpy(out, s, n); out[n] = 0;
    while (n > 0 && g.textWidth(font, out) > maxPx) {
        n--;
        while (n > 0 && (out[n] & 0xC0) == 0x80) n--;
        out[n] = 0;
    }
}

class KeyConfigScreen : public Screen {
public:
    KeyConfigScreen() : sel_(0), modal_(false), pickSel_(0) {}
    const char *title() const { return "KEYCONFIG"; }
    Context context() const { return CTX_MENU; }     // wheel scrolls, OK confirms (menu bindings)

    void onAction(Action a, int value, App &app) {
        if (value == 0 && a != ACT_NONE) return;     // key-down / repeat only
        if (a == ACT_DIAL_MAIN_CW || a == ACT_DIAL_SUB_CW)  a = ACT_NAV_DOWN;
        if (a == ACT_DIAL_MAIN_CCW || a == ACT_DIAL_SUB_CCW) a = ACT_NAV_UP;
        if (modal_) { pickerAction(a); return; }
        switch (a) {
            case ACT_NAV_UP:   sel_ = (sel_ - 1 + SK_COUNT) % SK_COUNT; break;
            case ACT_NAV_DOWN: sel_ = (sel_ + 1) % SK_COUNT; break;
            case ACT_NAV_RIGHT:
            case ACT_CONFIRM:  openPicker(); break;
            case ACT_NAV_LEFT:
            case ACT_BACK:
            case ACT_MENU:     app.popScreen(); break;
            default: break;
        }
    }

    void openPicker() {
        pickSel_ = (int)shootKeymap().func((ShootKey)sel_);
        if (pickSel_ < 0 || pickSel_ >= SF_COUNT) pickSel_ = 0;
        modal_ = true;
    }

    void pickerAction(Action a) {
        switch (a) {
            case ACT_NAV_UP:   pickSel_ = (pickSel_ - 1 + SF_COUNT) % SF_COUNT; break;
            case ACT_NAV_DOWN: pickSel_ = (pickSel_ + 1) % SF_COUNT; break;
            case ACT_CONFIRM:
            case ACT_NAV_RIGHT:
                shootKeymap().set((ShootKey)sel_, (ShootFunc)pickSel_);
                shootKeymapSave();                   // persist (conf file; no-op on host)
                modal_ = false; break;
            case ACT_NAV_LEFT:
            case ACT_BACK:
            case ACT_MENU:     modal_ = false; break;
            default: break;
        }
    }

    void draw(Canvas &c, App &app) {
        const Gfx &g = app.gfx();
        c.clear(kBg);
        // header
        g.text(c, GF_UI_27, 24, 34, "\xec\x82\xac\xec\x9a\xa9\xec\x9e\x90 \xed\x82\xa4 \xec\x84\xa4\xec\xa0\x95", kAccent, false); // 사용자 키 설정
        char pos[24]; snprintf(pos, sizeof pos, "%d / %d", sel_ + 1, SK_COUNT);
        int pw = g.textWidth(GF_UI_24, pos);
        g.text(c, GF_UI_24, PANEL_W - 24 - pw, 32, pos, kDim, false);
        c.fillRect(0, 50, PANEL_W, 2, kRule);

        // key rows: [key label] .......... [assigned function]
        const int rowTop = 66, rowH = 58;
        for (int i = 0; i < SK_COUNT; i++) {
            int y = rowTop + i * rowH;
            bool sel = i == sel_;
            if (sel) c.fillRect(0, y, PANEL_W, rowH, kSel);
            Color lc = sel ? kWhite : kWhite;
            Color vc = sel ? kWhite : kAccent;
            char lb[64]; fitPx(g, GF_UI_26, shootKeyLabel((ShootKey)i), 300, lb, sizeof lb);
            g.text(c, GF_UI_26, 24, y + 38, lb, lc, false);
            const char *fl = shootFuncLabel(shootKeymap().func((ShootKey)i));
            char vb[64]; fitPx(g, GF_UI_26, fl, 320, vb, sizeof vb);
            int vw = g.textWidth(GF_UI_26, vb);
            g.text(c, GF_UI_26, PANEL_W - 24 - vw, y + 38, vb, vc, false);
        }

        c.fillRect(0, PANEL_H - 30, PANEL_W, 2, kRule);
        // hint: "▲▼ 선택   ● 변경   MENU 닫기"
        const char *h = "\xe2\x96\xb2\xe2\x96\xbc \xec\x84\xa0\xed\x83\x9d   \xe2\x97\x8f \xeb\xb3\x80\xea\xb2\xbd   MENU \xeb\x8b\xab\xea\xb8\xb0";
        int hw = g.textWidth(GF_UI_20, h);
        g.text(c, GF_UI_20, PANEL_W - 24 - hw, PANEL_H - 10, h, kDim, false);

        if (modal_) drawPicker(c, g);
    }

private:
    // Function picker: centred dark card listing every assignable function, current one accented.
    void drawPicker(Canvas &c, const Gfx &g) {
        c.fillRect(0, 0, PANEL_W, PANEL_H, rgba(0, 0, 0, 0xaa));   // scrim
        const int VIS = 7;
        int start = pickSel_ - VIS / 2;
        if (start > SF_COUNT - VIS) start = SF_COUNT - VIS;
        if (start < 0) start = 0;
        int end = start + VIS < SF_COUNT ? start + VIS : SF_COUNT;
        const int rowH = 44, titleH = 46, hintH = 40;
        int rows = end - start;
        bool up = start > 0, down = end < SF_COUNT;
        int H = titleH + rows * rowH + (up ? 18 : 0) + (down ? 18 : 0) + hintH;
        int x0 = 120, x1 = PANEL_W - 120;
        int top = (PANEL_H - H) / 2;
        c.fillRect(x0, top, x1 - x0, H, kCardBg);
        c.rectOutline(x0, top, x1 - x0, H, kCardLine);

        char tb[96]; snprintf(tb, sizeof tb, "%s", shootKeyLabel((ShootKey)sel_));
        g.text(c, GF_UI_27, x0 + 22, top + 32, tb, kWhite, false);

        int y = top + titleH;
        if (up) { const char *u = "\xe2\x96\xb2"; int w = g.textWidth(GF_UI_19, u); g.text(c, GF_UI_19, (x0 + x1) / 2 - w / 2, y + 14, u, kDim, false); y += 18; }
        ShootFunc curFn = shootKeymap().func((ShootKey)sel_);
        for (int i = start; i < end; i++) {
            int ry = y + (i - start) * rowH;
            bool sel = i == pickSel_;
            bool isCur = (ShootFunc)i == curFn;
            if (sel) c.fillRect(x0 + 8, ry + 3, x1 - x0 - 16, rowH - 6, kSel);
            Color tc = sel ? kWhite : (isCur ? kAccent : kDim);
            // bullet: filled for the current assignment, ring otherwise
            if (isCur) c.fillRect(x0 + 24, ry + rowH / 2 - 4, 8, 8, tc);
            else       c.rectOutline(x0 + 24, ry + rowH / 2 - 4, 8, 8, tc);
            char lb[64]; fitPx(g, GF_UI_26, shootFuncLabel((ShootFunc)i), x1 - (x0 + 44) - 14, lb, sizeof lb);
            g.text(c, GF_UI_26, x0 + 44, ry + rowH / 2 + 9, lb, sel ? kWhite : (isCur ? kAccent : kWhite), false);
        }
        y += rows * rowH;
        if (down) { const char *d = "\xe2\x96\xbc"; int w = g.textWidth(GF_UI_19, d); g.text(c, GF_UI_19, (x0 + x1) / 2 - w / 2, y + 14, d, kDim, false); y += 18; }
        const char *h = "\xe2\x96\xb2\xe2\x96\xbc \xec\x84\xa0\xed\x83\x9d   \xe2\x97\x8f \xed\x99\x95\xec\xa0\x95   MENU \xeb\x92\xa4\xeb\xa1\x9c"; // 선택 확정 뒤로
        int hw = g.textWidth(GF_UI_20, h);
        g.text(c, GF_UI_20, x1 - 20 - hw, top + H - 14, h, kDim, false);
    }

    int  sel_;        // selected key row
    bool modal_;      // function picker open
    int  pickSel_;    // selected function in the picker
};

} // namespace

Screen *createKeyConfigScreen() { return new KeyConfigScreen(); }

} // namespace uicore
