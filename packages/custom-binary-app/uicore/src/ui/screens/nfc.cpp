// NFC one-touch screen. Layout: header, status strip (what is happening right now), two rows
// (NFC on/off, open the Wi-Fi smartphone page [+ cancel while connecting]), and a guidance card with the
// N-mark drawing. The one-touch session logic lives here; the Wi-Fi AP itself is wifi.cpp's job.
// No platform headers: only uicore::Nfc / uicore::Wifi interfaces, canvas/gfx.
#include "nfc.h"
#include "wifi.h"
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

const Color kBg      = { 0, 0, 0, 255 };
const Color kSel     = { 0xD3, 0x41, 0x00, 255 };
const Color kAccent  = { 0xff, 0x9a, 0x2e, 255 };
const Color kDim     = { 0xb9, 0xbc, 0xc0, 255 };
const Color kRule    = { 0x3a, 0x3d, 0x42, 255 };
const Color kWhite   = { 255, 255, 255, 255 };
const Color kCard    = { 0x20, 0x22, 0x26, 255 };
const Color kCardLn  = { 0x44, 0x47, 0x4c, 255 };
const Color kGreen   = { 0x3c, 0x8a, 0x5f, 255 };
const Color kGreenTx = { 0x6f, 0xd0, 0x98, 255 };
const Color kBlue    = { 0x3a, 0x6e, 0xa5, 255 };
const Color kRed     = { 0xe0, 0x5a, 0x4a, 255 };
const Color kOff     = { 0x6a, 0x6d, 0x72, 255 };

enum { ROW_TOP = 106, ROW_PITCH = 48, ROW_H = 46, JOIN_TIMEOUT_MS = 30000 };
enum Sess { S_NONE, S_JOINING, S_CONNECTED, S_FAILED };
enum Fail { F_NONE, F_WIFI, F_TIMEOUT, F_LOST };

void fillL(Canvas &c, int x0, int y0, int x1, int y1, Color col) {
    int p0 = LX(x0), p1 = LX(x1);
    if (p1 > p0 && y1 > y0) c.fillRect(p0, y0, p1 - p0, y1 - y0, col);
}
void fitText(const Gfx &g, int font, const char *s, int maxPx, char *out, int outN) {
    int n = (int)strlen(s); if (n >= outN) n = outN - 1;
    memcpy(out, s, n); out[n] = 0;
    while (n > 0 && g.textWidth(font, out) > maxPx) {
        n--; while (n > 0 && (out[n] & 0xC0) == 0x80) n--;
        out[n] = 0;
    }
}
// anti-aliased ring (r0..r1) with alpha
void ring(Canvas &c, float cx, float cy, float r, float th, Color col, int alpha) {
    for (int y = (int)(cy - r - th - 1); y <= (int)(cy + r + th + 1); y++)
        for (int x = (int)(cx - r - th - 1); x <= (int)(cx + r + th + 1); x++) {
            float d = sqrtf((x + 0.5f - cx) * (x + 0.5f - cx) + (y + 0.5f - cy) * (y + 0.5f - cy));
            float a = th * 0.5f + 0.5f - fabsf(d - r); if (a <= 0) continue; if (a > 1) a = 1;
            c.setPixel(x, y, rgba(col.r, col.g, col.b, (u8)(a * alpha)));
        }
}
void disc(Canvas &c, float cx, float cy, float r, Color col) {
    for (int y = (int)(cy - r - 1); y <= (int)(cy + r + 1); y++)
        for (int x = (int)(cx - r - 1); x <= (int)(cx + r + 1); x++) {
            float d = sqrtf((x + 0.5f - cx) * (x + 0.5f - cx) + (y + 0.5f - cy) * (y + 0.5f - cy));
            float a = r + 0.5f - d; if (a <= 0) continue; if (a > 1) a = 1;
            c.setPixel(x, y, rgba(col.r, col.g, col.b, (u8)(a * col.a)));
        }
}
void pill(Canvas &c, int x, int y, int w, int h, Color col) {
    c.fillRect(x + h / 2, y, w - h, h, col);
    disc(c, x + h / 2.0f, y + h / 2.0f, h / 2.0f, col); disc(c, x + w - h / 2.0f, y + h / 2.0f, h / 2.0f, col);
}

class NfcScreen : public Screen {
public:
    NfcScreen() : sel_(0), sess_(S_NONE), fail_(F_NONE), sessStart_(0), seenErr_(-1), errUntil_(0), toastEnd_(0), app_(0) {
        toast_[0] = 0; cs_[0] = 0; cp_[0] = 0;
    }
    ~NfcScreen() {}
    const char *title() const { return "NFC"; }
    Context context() const { return CTX_MENU; }

    // ------------------------------------------------------------------ logic
    int rowCount() const { return sess_ == S_JOINING ? 3 : 2; }
    void say(App &app, const char *s) { snprintf(toast_, sizeof toast_, "%s", s); toastEnd_ = app.nowMs() + 3200; }

    void onAction(Action a, int, App &app) {
        app_ = &app;
        Nfc *n = app.nfc();
        if (!n) { if (a == ACT_BACK || a == ACT_NAV_LEFT || a == ACT_MENU || a == ACT_CONFIRM) app.popScreen(); return; }
        update(app);
        int cnt = rowCount(); if (sel_ >= cnt) sel_ = cnt - 1;
        switch (a) {
            case ACT_NAV_UP:   sel_ = (sel_ + cnt - 1) % cnt; break;
            case ACT_NAV_DOWN: sel_ = (sel_ + 1) % cnt; break;
            case ACT_NAV_LEFT: case ACT_BACK: case ACT_MENU:
                if (sess_ == S_JOINING) cancel(app);
                app.popScreen(); break;
            case ACT_NAV_RIGHT: case ACT_CONFIRM: activate(app, sel_); break;
            default: break;
        }
    }
    void activate(App &app, int row) {
        Nfc *n = app.nfc();
        if (row == 0) {
            bool on = !n->enabled();
            n->setEnabled(on);
            if (!on && sess_ == S_JOINING) cancel(app);
            sess_ = S_NONE; fail_ = F_NONE;
            say(app, on ? "NFC를 켰습니다" : "NFC를 껐습니다");
        } else if (row == 1) {
            app.pushScreen(createWifiScreen(WP_AP));
        } else if (row == 2) {
            cancel(app); say(app, "연결을 취소했습니다");
        }
    }
    void cancel(App &app) {
        if (app.wifi()) app.wifi()->stopAp();
        if (app.nfc()) app.nfc()->finishPairing(false);
        sess_ = S_NONE; fail_ = F_NONE;
    }
    void fail(App &app, int why) {
        if (app.wifi()) app.wifi()->stopAp();
        if (app.nfc()) app.nfc()->finishPairing(false);
        sess_ = S_FAILED; fail_ = why;
        logf("nfc: one-touch failed (%d)", why);
    }

    // called every frame while on top (and before each action)
    void update(App &app) {
        Nfc *n = app.nfc(); if (!n) return;
        long now = app.nowMs();
        // keep the tag content == the AP credentials the phone will be told to join
        const char *s, *p; wifiApCredentials(&s, &p);
        if (strcmp(s, cs_) || strcmp(p, cp_)) {
            snprintf(cs_, sizeof cs_, "%s", s); snprintf(cp_, sizeof cp_, "%s", p);
            n->setPayload(s, p);
        }
        NfcStatusInfo ns; n->status(&ns);
        if (seenErr_ < 0) seenErr_ = ns.errSeq;
        if (ns.errSeq != seenErr_) { seenErr_ = ns.errSeq; errUntil_ = now + 3500; sess_ = S_NONE; }

        NfcTap tap;
        if (n->enabled() && sess_ != S_JOINING && n->pollTap(&tap)) {     // a phone read our tag: one-touch starts
            logf("nfc: tap -> AP '%s'", tap.ssid);
            sess_ = S_NONE; fail_ = F_NONE; errUntil_ = 0;
            if (wifiStartPhoneAp(app)) { sess_ = S_JOINING; sessStart_ = now; sel_ = 0; }
            else fail(app, F_WIFI);
        }
        if (sess_ == S_JOINING) {
            Wifi *w = app.wifi(); WifiStatusInfo st; if (w) w->status(&st); else st.state = WS_OFF;
            if (st.state == WS_AP_CLIENT) { sess_ = S_CONNECTED; n->finishPairing(true); say(app, "스마트폰이 연결되었습니다"); }
            else if (st.state < WS_AP_STARTING) fail(app, F_WIFI);
            else if (now - sessStart_ > JOIN_TIMEOUT_MS) fail(app, F_TIMEOUT);
        } else if (sess_ == S_CONNECTED) {
            Wifi *w = app.wifi(); WifiStatusInfo st; if (w) w->status(&st); else st.state = WS_OFF;
            if (st.state != WS_AP_CLIENT) { sess_ = S_NONE; }              // phone left: back to waiting
        }
    }

    // ------------------------------------------------------------------ drawing
    void draw(Canvas &c, App &app) {
        const Gfx &g = app.gfx();
        Nfc *n = app.nfc();
        c.clear(kBg);
        g.text(c, GF_UI_27, LX(31), 34, "NFC 원터치", kAccent, false);
        rules(c);
        if (!n) { center(c, g, "이 기기에서는 NFC를 사용할 수 없습니다", 240, kDim); hint(c, g, "◀ 닫기"); return; }
        update(app);
        NfcStatusInfo ns; n->status(&ns);
        bool on = n->enabled(); int cnt = rowCount(); if (sel_ >= cnt) sel_ = cnt - 1;
        {   char b[24]; snprintf(b, sizeof b, "%d / %d", sel_ + 1, cnt);
            int w = g.textWidth(GF_UI_24, b); g.text(c, GF_UI_24, LX(822) - w, 32, b, kDim, false); }
        WifiStatusInfo st; memset(&st, 0, sizeof st); if (app.wifi()) app.wifi()->status(&st);
        strip(c, g, app, on, ns, st);
        for (int i = 0; i < cnt; i++) {
            int y = ROW_TOP + i * ROW_PITCH; bool s = i == sel_;
            if (s) fillL(c, 14, y, 839, y + ROW_H, kSel);
            const char *label = i == 0 ? "NFC 원터치 연결" : (i == 1 ? "스마트폰 Wi-Fi 연결 화면" : "연결 취소");
            Color tc = (i == 2 && !s) ? kRed : kWhite;
            g.text(c, GF_UI_26, LX(40), y + 31, label, tc, false);
            if (i == 0) {
                int px = LX(790) - 52, py = y + 10;
                pill(c, px, py, 52, 26, on ? (s ? kWhite : kGreen) : kOff);
                disc(c, on ? px + 52 - 13.0f : px + 13.0f, py + 13.0f, 10.0f, on && s ? kSel : kWhite);
                const char *t = on ? "켜짐" : "꺼짐"; int tw = g.textWidth(GF_UI_24, t);
                g.text(c, GF_UI_24, px - 12 - tw, y + 31, t, s ? kWhite : kDim, false);
            } else if (i == 1) {
                g.text(c, GF_UI_26, LX(805), y + 31, ">", s ? kWhite : kDim, false);
            }
        }
        card(c, g, app, on, ns, st, ROW_TOP + cnt * ROW_PITCH + 8);
        rules(c);
        hint(c, g, sess_ == S_JOINING ? "▲▼ 선택   ● 확인   ◀ 취소하고 닫기" : "▲▼ 선택   ● 변경/열기   ◀ 닫기");
        if (toastEnd_ > app.nowMs()) toast(c, g);
    }

    void rules(Canvas &c) const { c.fillRect(0, 50, PANEL_W, 2, kRule); c.fillRect(0, 443, PANEL_W, 2, kRule); }
    void hint(Canvas &c, const Gfx &g, const char *h) const {
        int w = g.textWidth(GF_UI_20, h); g.text(c, GF_UI_20, LX(821) - w + 1, 468, h, kDim, false);
    }
    void center(Canvas &c, const Gfx &g, const char *s, int baseline, Color col) const {
        int w = g.textWidth(GF_UI_26, s); g.text(c, GF_UI_26, (PANEL_W - w) / 2, baseline, s, col, false);
    }
    void toast(Canvas &c, const Gfx &g) const {
        char b[128]; fitText(g, GF_UI_24, toast_, LX(740), b, sizeof b);
        int tw = g.textWidth(GF_UI_24, b), w = tw + 36, x = (PANEL_W - w) / 2, y = 396;
        c.fillRect(x, y, w, 36, rgba(0x10, 0x11, 0x14, 240));
        c.rectOutline(x, y, w, 36, kAccent);
        g.text(c, GF_UI_24, x + 18, y + 26, b, kWhite, false);
    }

    // N-mark badge + radio waves; `pulse` animates the waves, `phone` draws the phone sliding toward the mark.
    void nmark(Canvas &c, const Gfx &g, int cx, int cy, long now, int mode) const {
        // mode 0 idle (waves slow), 1 tag detected (fast, phone touching), 2 joining, 3 done, 4 off/fail
        Color col = mode == 3 ? kGreen : (mode == 4 ? kOff : (mode == 0 ? kBlue : kAccent));
        if (mode != 4 && mode != 3) {
            int period = mode == 0 ? 1800 : 900;
            for (int k = 0; k < 3; k++) {
                float ph = (float)((now + k * period / 3) % period) / period;
                ring(c, (float)cx, (float)cy, 30.0f + ph * 34.0f, 3.0f, col, (int)(200 * (1.0f - ph)));
            }
        }
        disc(c, (float)cx, (float)cy, 30.0f, col);
        const char *N = "N"; int tw = g.textWidth(GF_UI_27, N);
        g.text(c, GF_UI_27, cx - tw / 2, cy + 10, N, kWhite, false);
        // phone (rounded outline) to the right; slides in when idle, touches when detected
        float off = mode == 0 ? 14.0f + 8.0f * sinf(now / 450.0f) : 0.0f;
        int px = cx + 40 + (int)off, py = cy - 34;
        c.fillRect(px, py, 34, 68, kCard);
        c.rectOutline(px, py, 34, 68, mode == 4 ? kOff : kWhite);
        c.fillRect(px + 12, py + 5, 10, 2, mode == 4 ? kOff : kWhite);
        if (mode == 3) { c.fillRect(px + 8, py + 30, 6, 3, kGreenTx); c.fillRect(px + 12, py + 33, 14, 3, kGreenTx); }
    }

    void strip(Canvas &c, const Gfx &g, App &app, bool on, const NfcStatusInfo &ns, const WifiStatusInfo &st) const {
        Color accent = kOff; char l1[96], l2[128]; l2[0] = 0;
        long now = app.nowMs();
        if (now < errUntil_ && on && sess_ == S_NONE) {
            accent = kRed; snprintf(l1, sizeof l1, "스마트폰을 읽지 못했습니다");
            snprintf(l2, sizeof l2, "떨어지지 않게 N 마크에 다시 대 주세요");
        } else if (!on) {
            snprintf(l1, sizeof l1, "NFC 꺼짐"); snprintf(l2, sizeof l2, "켜면 스마트폰을 대는 것만으로 연결할 수 있습니다");
        } else if (sess_ == S_JOINING) {
            accent = kAccent; snprintf(l1, sizeof l1, "스마트폰 연결 중...");
            snprintf(l2, sizeof l2, st.state == WS_AP_STARTING ? "Wi-Fi 준비 중  %s" : "스마트폰이 접속하기를 기다리는 중  %s", st.ssid);
        } else if (sess_ == S_CONNECTED) {
            accent = kBlue; snprintf(l1, sizeof l1, "스마트폰 연결됨");
            snprintf(l2, sizeof l2, "%s   %d대 접속 중", st.ssid, st.clients);
        } else if (sess_ == S_FAILED) {
            accent = kRed; snprintf(l1, sizeof l1, "연결하지 못했습니다");
            snprintf(l2, sizeof l2, "%s", fail_ == F_TIMEOUT ? "시간이 초과되었습니다. 다시 대 주세요" : (fail_ == F_LOST ? "연결이 끊어졌습니다" : "Wi-Fi를 시작하지 못했습니다"));
        } else if (ns.state == NFS_TAG) {
            accent = kAccent; snprintf(l1, sizeof l1, "스마트폰 감지됨");
            snprintf(l2, sizeof l2, "떼지 말고 잠시 그대로 대 주세요");
        } else if (ns.state == NFS_PAIRING) {
            accent = kAccent; snprintf(l1, sizeof l1, "스마트폰 연결 중...");
            snprintf(l2, sizeof l2, "정보를 전달했습니다");
        } else {
            accent = kBlue; snprintf(l1, sizeof l1, "스마트폰 대기 중");
            snprintf(l2, sizeof l2, "스마트폰을 카메라의 N 마크에 대 주세요");
        }
        c.fillRect(LX(14), 55, LX(839) - LX(14), 47, kCard);
        c.fillRect(LX(14), 55, 5, 47, accent);
        disc(c, (float)LX(46), 78.0f, 11.0f, accent);
        const char *N = "N"; int tw = g.textWidth(GF_UI_19, N);
        g.text(c, GF_UI_19, LX(46) - tw / 2, 85, N, kWhite, false);
        char b[128];
        fitText(g, GF_UI_24, l1, LX(740), b, sizeof b);
        Color tc = sess_ == S_CONNECTED ? kGreenTx : (accent.r == kRed.r && accent.g == kRed.g ? kRed : (accent.r == kAccent.r ? kAccent : (on ? kWhite : kDim)));
        g.text(c, GF_UI_24, LX(84), 78, b, tc, false);
        fitText(g, GF_UI_19, l2, LX(740), b, sizeof b);
        g.text(c, GF_UI_19, LX(84), 97, b, kDim, false);
    }

    void card(Canvas &c, const Gfx &g, App &app, bool on, const NfcStatusInfo &ns, const WifiStatusInfo &st, int cy) const {
        int ch = 443 - 8 - cy; if (ch < 80) return;
        c.fillRect(LX(14), cy, LX(839) - LX(14), ch, kCard);
        c.rectOutline(LX(14), cy, LX(839) - LX(14), ch, kCardLn);
        long now = app.nowMs();
        int mode;
        if (!on) mode = 4;
        else if (sess_ == S_CONNECTED) mode = 3;
        else if (sess_ == S_JOINING || ns.state == NFS_PAIRING) mode = 2;
        else if (ns.state == NFS_TAG) mode = 1;
        else if (sess_ == S_FAILED) mode = 4;
        else mode = 0;
        nmark(c, g, LX(120), cy + ch / 2, now, mode);
        int x = LX(250);
        const char *h; const char *l1 = 0, *l2 = 0, *l3 = 0; char b[128]; Color hc = kAccent;
        if (!on) { h = "NFC가 꺼져 있습니다"; hc = kDim; l1 = "위에서 켜면 스마트폰을 대어 바로 연결합니다"; }
        else if (sess_ == S_CONNECTED) { h = "연결이 완료되었습니다"; hc = kGreenTx; l1 = "스마트폰 앱에서 사진을 전송하세요"; l2 = "(전송 프로토콜은 후속 작업)"; }
        else if (sess_ == S_JOINING || ns.state == NFS_PAIRING) { h = "스마트폰에서 연결하는 중"; l1 = "스마트폰이 Wi-Fi 연결 허용을 물으면 승인하세요"; snprintf(b, sizeof b, "AP  %s", st.ssid); l2 = b; }
        else if (sess_ == S_FAILED) { h = "다시 시도하세요"; hc = kRed; l1 = "스마트폰의 NFC를 켜고 N 마크에 다시 대 주세요"; }
        else { h = "이렇게 연결하세요"; l1 = "1  스마트폰의 NFC를 켭니다"; l2 = "2  카메라의 N 마크에 스마트폰을 댑니다"; l3 = "3  Wi-Fi 연결이 자동으로 시작됩니다"; }
        int y = cy + 34;
        g.text(c, GF_UI_24, x, y, h, hc, false);
        const char *ls[3] = { l1, l2, l3 };
        for (int i = 0; i < 3; i++) if (ls[i]) {
            char t[160]; fitText(g, GF_UI_20, ls[i], LX(839) - x - 20, t, sizeof t);
            g.text(c, GF_UI_20, x, y + 30 + i * 26, t, kWhite, false);
        }
    }

    int  sel_, sess_, fail_; long sessStart_; int seenErr_; long errUntil_;
    char toast_[128]; long toastEnd_;
    char cs_[33], cp_[64];
    App *app_;
};

} // namespace

bool nfcMenuEntry(const char *key, const char *label) {
    return !strcmp(key, "info-na-transfer") && !strcmp(label, "원터치(NFC)");
}

Screen *createNfcScreen() { return new NfcScreen(); }

// A phone reached the antenna (tapSeq changed): bring the one-touch screen up from anywhere.
void nfcService(App &app) {
    static int lastSeq = -1;
    Nfc *n = app.nfc(); if (!n) return;
    NfcStatusInfo ns; n->status(&ns);
    if (lastSeq < 0) { lastSeq = ns.tapSeq; return; }
    if (ns.tapSeq != lastSeq) {
        lastSeq = ns.tapSeq;
        if (n->enabled() && strcmp(app.topTitle(), "NFC")) { logf("nfc: phone detected -> one-touch screen"); app.pushScreen(createNfcScreen()); }
    }
}

} // namespace uicore
