// Wi-Fi settings / connection screen -- our own design (OpenMemories is unmaintained).
//
//   MASTER   Wi-Fi on/off switch + connection mode (STA "네트워크 연결" | AP "스마트폰 연결"), live status
//            strip, entries to the mode's pages.
//   STA      scan list (signal bars, lock, saved/connected marks). CONFIRM = the obvious action:
//            saved -> connect, open -> connect, secured -> password via the text-input widget,
//            connected -> options. FN = options (connect / disconnect / change password / forget).
//            Wrong password -> toast + FN "비밀번호 입력". Last row = rescan. Auto rescan every ~15 s.
//   SAVED    saved-networks manager (connect / change password / forget with confirm; trash key = forget).
//   AP       "send to smartphone": our AP name + password (editable through the text-input widget,
//            AP restarts with the new credentials), live state (starting / waiting / phone joined),
//            a 3-step hint for the phone. Transfer protocol itself = follow-up.
// The OSD Wi-Fi icon follows the backend state via CameraModel (see camera_model.cpp syncWifi).
// No platform headers: only the uicore::Wifi interface, canvas/gfx and the text-input widget.
#include "wifi.h"
#include "uicore/screen.h"
#include "uicore/app.h"
#include "uicore/canvas.h"
#include "uicore/gfx.h"
#include "uicore/log.h"
#include "../widgets/text_input.h"
#include <math.h>
#include <stdio.h>
#include <string.h>

namespace uicore {
namespace {

const Color kBg      = { 0, 0, 0, 255 };
const Color kSel     = { 0xD3, 0x41, 0x00, 255 };
const Color kAccent  = { 0xff, 0x9a, 0x2e, 255 };
const Color kDim     = { 0xb9, 0xbc, 0xc0, 255 };
const Color kDisab   = { 0x5a, 0x5d, 0x62, 255 };
const Color kRule    = { 0x3a, 0x3d, 0x42, 255 };
const Color kWhite   = { 255, 255, 255, 255 };
const Color kCard    = { 0x20, 0x22, 0x26, 255 };
const Color kCardLn  = { 0x44, 0x47, 0x4c, 255 };
const Color kGreen   = { 0x3c, 0x8a, 0x5f, 255 };
const Color kGreenTx = { 0x6f, 0xd0, 0x98, 255 };
const Color kBlue    = { 0x3a, 0x6e, 0xa5, 255 };
const Color kRed     = { 0xe0, 0x5a, 0x4a, 255 };
const Color kOff     = { 0x6a, 0x6d, 0x72, 255 };

enum { ROW_TOP = 106, ROW_PITCH = 48, ROW_H = 46, VIS = 7, MAXN = 24 };
enum Opt { O_CONNECT, O_DISCONNECT, O_PASSWORD, O_FORGET, O_CANCEL, O_FORGET_YES };
enum Modal { M_NONE, M_OPTS, M_CONFIRM };

struct Row { char ssid[33]; int signal; bool secured, saved, connected, connecting; };

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
// Wi-Fi fan glyph: base point (cx,by), outer radius R, `lit` arcs (0..3) in `on`, rest in `off`.
void glyph(Canvas &c, int cx, int by, int R, int lit, Color on, Color off, bool slash) {
    for (int y = by - R - 3; y <= by + 3; y++) for (int x = cx - R - 3; x <= cx + R + 3; x++) {
        float dx = x + 0.5f - cx, dy = by - (y + 0.5f), r = sqrtf(dx * dx + dy * dy);
        float a = 0; Color col = on;
        if (r <= 2.8f) a = 1;
        else if (dy > 0 && fabsf(dx) <= dy * 1.05f)
            for (int i = 1; i <= 3; i++) {
                float cov = 2.4f - fabsf(r - R * i / 3.0f); if (cov <= 0) continue; if (cov > 1) cov = 1;
                if (cov > a) { a = cov; col = i <= lit ? on : off; }
            }
        if (a > 0) c.setPixel(x, y, rgba(col.r, col.g, col.b, (u8)(a * 255)));
    }
    if (slash) for (int i = -R; i <= R; i++) { disc(c, cx + i * 0.8f, by - R / 2 + i * 0.8f, 1.6f, kRed); }
}
void bars(Canvas &c, int x, int baseY, int signal, bool dimmed) {      // 4 bars, 5px wide
    int lit = (signal + 24) / 25; if (lit > 4) lit = 4; if (lit < 1) lit = 1;
    for (int i = 0; i < 4; i++) {
        int h = 6 + i * 4;
        Color col = i < lit ? (dimmed ? kDim : kWhite) : kRule;
        c.fillRect(x + i * 8, baseY - h, 5, h, col);
    }
}
void lockIcon(Canvas &c, int x, int y, Color col) {                    // 14x16 at (x,y)
    c.rectOutline(x + 2, y, 10, 9, col); c.rectOutline(x + 3, y + 1, 8, 8, col);
    c.fillRect(x, y + 7, 14, 9, col);
}
void checkIcon(Canvas &c, int x, int y, Color col) {                   // 14x10
    for (int i = 0; i < 5; i++) c.fillRect(x + i, y + 4 + i, 3, 3, col);
    for (int i = 0; i < 9; i++) c.fillRect(x + 4 + i, y + 8 - i, 3, 3, col);
}

const char *errText(int e) {
    switch (e) {
        case WE_AUTH: return "비밀번호가 올바르지 않습니다";
        case WE_NOT_FOUND: return "네트워크를 찾을 수 없습니다";
        case WE_TIMEOUT: return "응답 시간이 초과되었습니다";
        default: return "연결에 실패했습니다";
    }
}

class WifiScreen : public Screen {
public:
    WifiScreen(int pageArg) : page_(pageArg & 0xff), direct_((pageArg & 0xff) != WP_MASTER), autoStart_((pageArg & WP_AUTOSTART) != 0),
        started_(false), nRows_(0), scanReq_(false), wasScanning_(false), nextScan_(0), modal_(M_NONE), mSel_(0), mN_(0),
        toastEnd_(0), prevState_(-1), prevErrSeq_(-1), showPw_(false), apTry_(0), app_(0) {
        for (int i = 0; i < 4; i++) sel_[i] = 0;
        toast_[0] = 0; target_[0] = 0; pending_[0] = 0; apSsid_[0] = 0; apPw_[0] = 0; pskTitle_[0] = 0; pskPrompt_[0] = 0;
    }
    const char *title() const { return "Wi-Fi"; }
    Context context() const { return CTX_MENU; }

    // ------------------------------------------------------------------ logic
    void onAction(Action a, int, App &app) {
        app_ = &app;
        Wifi *w = app.wifi();
        if (!w) { if (a == ACT_BACK || a == ACT_NAV_LEFT || a == ACT_MENU || a == ACT_CONFIRM) app.popScreen(); return; }
        begin(app);
        if (modal_ != M_NONE) { modalAction(a, app); return; }
        switch (page_) {
            case WP_MASTER: masterAction(a, app); break;
            case WP_STA:    staAction(a, app); break;
            case WP_SAVED:  savedAction(a, app); break;
            case WP_AP:     apAction(a, app); break;
        }
    }

    void begin(App &app) {            // first touch: apply the menu entry's auto-start intent
        if (started_) return;
        started_ = true;
        Wifi *w = app.wifi(); if (!w) return;
        if (autoStart_) {
            int want = page_ == WP_AP ? WM_AP : WM_STA;
            if (!w->enabled()) w->setEnabled(true);
            if (w->mode() != want) w->setMode(want);
            if (want == WM_AP) ensureAp(app);
        }
        if (page_ == WP_STA) requestScan(app);
    }

    void goBack(App &app) {
        if (page_ == WP_MASTER || direct_) { app.popScreen(); return; }
        page_ = WP_MASTER;
    }
    void go(int p, App &app) { page_ = p; modal_ = M_NONE; if (p == WP_STA) requestScan(app); if (p == WP_AP) ensureAp(app); }

    void say(App &app, const char *s) { snprintf(toast_, sizeof toast_, "%s", s); toastEnd_ = app.nowMs() + 3200; }

    void requestScan(App &app) {
        Wifi *w = app.wifi();
        if (w && w->enabled() && w->mode() == WM_STA && w->beginScan()) { scanReq_ = true; nextScan_ = app.nowMs() + 15000; }
    }
    void apCreds(const char **ssid, const char **pw) const { wifiApCredentials(ssid, pw); }
    void ensureAp(App &app) {
        Wifi *w = app.wifi();
        if (!w || !w->enabled() || w->mode() != WM_AP) return;
        WifiStatusInfo st; w->status(&st);
        if (st.state >= WS_AP_STARTING) return;
        const char *s, *p; apCreds(&s, &p);
        w->startAp(s, p);
        snprintf(apSsid_, sizeof apSsid_, "%s", s); snprintf(apPw_, sizeof apPw_, "%s", p);
    }

    // ---- master
    int masterCount(Wifi *w) const { return w->mode() == WM_STA ? 5 : 4; }
    void masterAction(Action a, App &app) {
        Wifi *w = app.wifi();
        int n = masterCount(w);
        int &sel = sel_[WP_MASTER];
        if (sel >= n) sel = n - 1;
        switch (a) {
            case ACT_NAV_UP:   sel = (sel + n - 1) % n; break;
            case ACT_NAV_DOWN: sel = (sel + 1) % n; break;
            case ACT_NAV_LEFT: case ACT_BACK: case ACT_MENU: goBack(app); break;
            case ACT_NAV_RIGHT: case ACT_CONFIRM: masterActivate(app, sel); break;
            default: break;
        }
    }
    void masterActivate(App &app, int row) {
        Wifi *w = app.wifi();
        if (row == 0) {
            bool on = !w->enabled();
            w->setEnabled(on);
            if (on) ensureAp(app);
            say(app, on ? "Wi-Fi를 켰습니다" : "Wi-Fi를 껐습니다");
            return;
        }
        if (row == 1) {
            w->setMode(w->mode() == WM_STA ? WM_AP : WM_STA);
            if (w->enabled()) ensureAp(app);
            say(app, w->mode() == WM_AP ? "스마트폰 연결 모드" : "네트워크 연결 모드");
            return;
        }
        if (!w->enabled()) { say(app, "Wi-Fi가 꺼져 있습니다. 먼저 켜 주세요"); return; }
        if (w->mode() == WM_STA) {
            if (row == 2) go(WP_STA, app);
            else if (row == 3) go(WP_SAVED, app);
            else if (row == 4) manualAdd(app);
        } else {
            if (row == 2) go(WP_AP, app);
            else if (row == 3) editAp(app);
        }
    }

    // ---- text-input chains
    static bool pskDone(void *u, bool ok, const char *text, TextEditRequest *) {
        WifiScreen *s = (WifiScreen *)u;
        if (ok && s->app_ && s->app_->wifi()) {
            s->app_->wifi()->connect(s->pending_, text);
            s->say(*s->app_, "연결 중...");
        }
        return false;
    }
    static bool ssidDone(void *u, bool ok, const char *text, TextEditRequest *next) {
        WifiScreen *s = (WifiScreen *)u;
        if (!ok) return false;
        snprintf(s->pending_, sizeof s->pending_, "%s", text);
        s->fillPsk(next, false);
        return true;
    }
    void fillPsk(TextEditRequest *r, bool retry) {
        snprintf(pskTitle_, sizeof pskTitle_, "%s 비밀번호", pending_);
        snprintf(pskPrompt_, sizeof pskPrompt_, "%s", retry ? "비밀번호가 올바르지 않습니다. 다시 입력하세요" : "8~63자");
        memset(r, 0, sizeof *r);
        r->field.id = "wifi.sta.psk"; r->field.title = pskTitle_; r->field.initial = 0;
        r->c.minLen = 8; r->c.maxLen = 63; r->c.groups = TG_ALL; r->c.secret = true; r->c.allowSpace = false;
        r->c.autoCap = false; r->c.suggestGenerated = false; r->c.validator = TV_WIFI_PSK; r->c.prompt = pskPrompt_;
        r->c.backend = TB_AUTO;
        r->cb = pskDone; r->user = this;
    }
    void askPassword(App &app, const char *ssid, bool retry) {
        snprintf(pending_, sizeof pending_, "%s", ssid);
        TextEditRequest r; fillPsk(&r, retry);
        editText(app, r.field, r.c, r.cb, r.user);
    }
    void manualAdd(App &app) {
        static const char *const kPresets[] = { "iptime", "KT_GiGA", "AndroidHotspot" };
        pending_[0] = 0;
        TextField f = { "wifi.sta.ssid", "네트워크 이름(SSID)", 0 };
        TextConstraints c; memset(&c, 0, sizeof c);
        c.minLen = 1; c.maxLen = 32; c.groups = TG_ALL; c.allowSpace = true; c.autoCap = false;
        c.validator = TV_SSID; c.prompt = "다음 단계에서 비밀번호를 입력합니다"; c.presets = kPresets; c.nPresets = 3; c.backend = TB_AUTO;
        editText(app, f, c, ssidDone, this);
    }
    void editAp(App &app) {
        const TextFieldDef *d = findTextField("info-na-wifi-config", "SSID/PW 재설정");
        if (d) openTextField(app, *d);
    }

    // ---- STA
    void staAction(Action a, App &app) {
        Wifi *w = app.wifi();
        if (!w->enabled() || w->mode() != WM_STA) { if (a == ACT_CONFIRM || a == ACT_BACK || a == ACT_NAV_LEFT || a == ACT_MENU) goBack(app); return; }
        int n = nRows_ + 1;                                     // + rescan row
        int &sel = sel_[WP_STA];
        if (sel >= n) sel = n - 1;
        switch (a) {
            case ACT_NAV_UP:   sel = (sel + n - 1) % n; break;
            case ACT_NAV_DOWN: sel = (sel + 1) % n; break;
            case ACT_NAV_LEFT: case ACT_BACK: case ACT_MENU: goBack(app); break;
            case ACT_CONFIRM: case ACT_NAV_RIGHT:
                if (sel == nRows_) { requestScan(app); say(app, "다시 검색합니다"); }
                else networkDefault(app, rows_[sel]);
                break;
            case ACT_FN:
                if (sel < nRows_) openOpts(rows_[sel].ssid, rows_[sel].secured, rows_[sel].saved, rows_[sel].connected);
                break;
            case ACT_DELETE:
                if (sel < nRows_ && rows_[sel].saved) { snprintf(target_, sizeof target_, "%s", rows_[sel].ssid); openConfirm(); }
                break;
            default: break;
        }
    }
    void networkDefault(App &app, const Row &r) {
        Wifi *w = app.wifi();
        if (r.connected) { openOpts(r.ssid, r.secured, r.saved, true); return; }
        if (r.connecting) return;
        if (r.saved) { w->connect(r.ssid, 0); say(app, "연결 중..."); }
        else if (!r.secured) { w->connect(r.ssid, ""); say(app, "연결 중..."); }
        else askPassword(app, r.ssid, false);
    }

    // ---- saved
    int savedCount(Wifi *w) const { return w->savedCount(); }
    void savedAction(Action a, App &app) {
        Wifi *w = app.wifi();
        int n = w->savedCount();
        int &sel = sel_[WP_SAVED];
        if (n > 0 && sel >= n) sel = n - 1;
        switch (a) {
            case ACT_NAV_UP:   if (n) sel = (sel + n - 1) % n; break;
            case ACT_NAV_DOWN: if (n) sel = (sel + 1) % n; break;
            case ACT_NAV_LEFT: case ACT_BACK: case ACT_MENU: goBack(app); break;
            case ACT_CONFIRM: case ACT_NAV_RIGHT: case ACT_FN:
                if (n) { char s[33]; bool sec = true; if (w->savedAt(sel, s, &sec)) openOpts(s, sec, true, isConnected(w, s)); }
                break;
            case ACT_DELETE:
                if (n) { char s[33]; bool sec; if (w->savedAt(sel, s, &sec)) { snprintf(target_, sizeof target_, "%s", s); openConfirm(); } }
                break;
            default: break;
        }
    }
    bool isConnected(Wifi *w, const char *ssid) const {
        WifiStatusInfo st; w->status(&st); return st.state == WS_CONNECTED && !strcmp(st.ssid, ssid);
    }

    // ---- AP
    void apAction(Action a, App &app) {
        Wifi *w = app.wifi();
        int &sel = sel_[WP_AP];
        switch (a) {
            case ACT_NAV_UP:   sel = (sel + 2) % 3; break;
            case ACT_NAV_DOWN: sel = (sel + 1) % 3; break;
            case ACT_NAV_LEFT: case ACT_BACK: case ACT_MENU: goBack(app); break;
            case ACT_FN: showPw_ = !showPw_; break;
            case ACT_CONFIRM: case ACT_NAV_RIGHT:
                if (sel < 2) editAp(app);
                else { w->setEnabled(false); say(app, "공유를 중지하고 Wi-Fi를 껐습니다"); sel = 0; goBack(app); }
                break;
            default: break;
        }
    }

    // ---- modals
    void openOpts(const char *ssid, bool secured, bool saved, bool connected) {
        snprintf(target_, sizeof target_, "%s", ssid);
        mN_ = 0;
        mItem_[mN_++] = connected ? O_DISCONNECT : O_CONNECT;
        if (secured && !connected) mItem_[mN_++] = O_PASSWORD;
        if (saved) mItem_[mN_++] = O_FORGET;
        mItem_[mN_++] = O_CANCEL;
        mSel_ = 0; modal_ = M_OPTS;
    }
    void openConfirm() { mN_ = 2; mItem_[0] = O_CANCEL; mItem_[1] = O_FORGET_YES; mSel_ = 0; modal_ = M_CONFIRM; }
    const char *optLabel(int o) const {
        switch (o) {
            case O_CONNECT: return "연결"; case O_DISCONNECT: return "연결 해제"; case O_PASSWORD: return "비밀번호 입력";
            case O_FORGET: return "저장 삭제"; case O_FORGET_YES: return "삭제"; default: return "취소";
        }
    }
    void modalAction(Action a, App &app) {
        Wifi *w = app.wifi();
        switch (a) {
            case ACT_NAV_UP:   mSel_ = (mSel_ + mN_ - 1) % mN_; break;
            case ACT_NAV_DOWN: mSel_ = (mSel_ + 1) % mN_; break;
            case ACT_NAV_LEFT: case ACT_BACK: case ACT_MENU: modal_ = M_NONE; break;
            case ACT_CONFIRM: {
                int o = mItem_[mSel_]; modal_ = M_NONE;
                if (o == O_CONNECT) { if (w->connect(target_, 0)) say(app, "연결 중..."); }
                else if (o == O_DISCONNECT) { w->disconnect(); say(app, "연결을 해제했습니다"); }
                else if (o == O_PASSWORD) askPassword(app, target_, false);
                else if (o == O_FORGET) openConfirm();
                else if (o == O_FORGET_YES) { w->forget(target_); say(app, "저장된 네트워크를 삭제했습니다"); }
                break;
            }
            default: break;
        }
    }

    // ------------------------------------------------------------------ per-frame update
    void update(App &app) {
        Wifi *w = app.wifi(); if (!w) return;
        app_ = &app;
        begin(app);
        long now = app.nowMs();
        WifiStatusInfo st; w->status(&st);
        if (st.errSeq != prevErrSeq_) {
            if (prevErrSeq_ >= 0 && st.lastError != WE_NONE) {
                char b[96]; snprintf(b, sizeof b, "%s", errText(st.lastError));
                say(app, b);
            }
            prevErrSeq_ = st.errSeq;
        }
        if (st.state != prevState_) {
            if (prevState_ >= 0) {
                if (st.state == WS_CONNECTED) { char b[96]; snprintf(b, sizeof b, "연결됨: %s", st.ssid); say(app, b); }
                else if (st.state == WS_AP_CLIENT) say(app, "스마트폰이 연결되었습니다");
                else if (st.state == WS_AP_WAITING) say(app, "스마트폰 연결을 기다립니다");
            }
            prevState_ = st.state;
        }
        // scan bookkeeping
        bool sc = w->scanning();
        if (wasScanning_ && !sc) scanReq_ = false;
        wasScanning_ = sc;
        if (page_ == WP_STA && w->enabled() && w->mode() == WM_STA) {
            if (!sc && nextScan_ && now >= nextScan_ && st.state != WS_CONNECTING) requestScan(app);
            rebuildRows(w, st);
        }
        // AP: follow credential edits, (re)start when idle
        if (w->enabled() && w->mode() == WM_AP && (page_ == WP_AP || page_ == WP_MASTER)) {
            const char *s, *p; apCreds(&s, &p);
            if (st.state >= WS_AP_STARTING) {
                if (strcmp(s, apSsid_) || strcmp(p, apPw_)) {
                    w->stopAp(); w->startAp(s, p);
                    snprintf(apSsid_, sizeof apSsid_, "%s", s); snprintf(apPw_, sizeof apPw_, "%s", p);
                    say(app, "새 AP 설정을 적용했습니다");
                }
            } else if (st.state == WS_IDLE && now >= apTry_) { apTry_ = now + 1000; ensureAp(app); }
        }
    }

    void rebuildRows(Wifi *w, const WifiStatusInfo &st) {
        char keep[33]; keep[0] = 0;
        if (sel_[WP_STA] < nRows_) snprintf(keep, sizeof keep, "%s", rows_[sel_[WP_STA]].ssid);
        WifiNetwork nets[MAXN];
        int n = w->scanResults(nets, MAXN);
        if (n == 0 && nRows_ > 0 && wasScanning_) return;       // keep the old list while a rescan runs
        nRows_ = 0;
        for (int i = 0; i < n; i++) {
            Row &r = rows_[nRows_++];
            snprintf(r.ssid, sizeof r.ssid, "%s", nets[i].ssid);
            r.signal = nets[i].signal; r.secured = nets[i].secured; r.saved = nets[i].saved;
            r.connected  = st.state == WS_CONNECTED  && !strcmp(st.ssid, r.ssid);
            r.connecting = st.state == WS_CONNECTING && !strcmp(st.ssid, r.ssid);
        }
        for (int i = 0; i < nRows_; i++)                         // connected, saved, then signal (stable)
            for (int j = i + 1; j < nRows_; j++)
                if (rank(rows_[j]) > rank(rows_[i])) { Row t = rows_[i]; rows_[i] = rows_[j]; rows_[j] = t; }
        if (keep[0]) for (int i = 0; i < nRows_; i++) if (!strcmp(rows_[i].ssid, keep)) sel_[WP_STA] = i;
        if (sel_[WP_STA] > nRows_) sel_[WP_STA] = nRows_;
    }
    static int rank(const Row &r) { return (r.connected ? 4000 : 0) + (r.connecting ? 3000 : 0) + (r.saved ? 1000 : 0) + r.signal; }

    // ------------------------------------------------------------------ drawing
    void draw(Canvas &c, App &app) {
        const Gfx &g = app.gfx();
        Wifi *w = app.wifi();
        c.clear(kBg);
        if (!w) { header(c, g, "Wi-Fi", 0, 0); center(c, g, "이 기기에서는 Wi-Fi를 사용할 수 없습니다", 240, kDim); rules(c); hint(c, g, "◀ 닫기"); return; }
        update(app);
        WifiStatusInfo st; w->status(&st);
        switch (page_) {
            case WP_MASTER: header(c, g, "Wi-Fi", sel_[0] + 1, masterCount(w)); strip(c, g, app, w, st); drawMaster(c, g, w, st); break;
            case WP_STA:    header(c, g, "Wi-Fi", 0, 0, "네트워크 연결"); strip(c, g, app, w, st); drawSta(c, g, w, st); break;
            case WP_SAVED:  header(c, g, "Wi-Fi", 0, 0, "저장된 네트워크"); strip(c, g, app, w, st); drawSaved(c, g, w, st); break;
            case WP_AP:     header(c, g, "Wi-Fi", 0, 0, "스마트폰 연결"); strip(c, g, app, w, st); drawAp(c, g, w, st); break;
        }
        rules(c);
        const char *h = "▲▼ 선택   ● 확인   ◀ 이전";
        if (page_ == WP_MASTER) h = "▲▼ 선택   ● 변경/열기   ◀ 닫기";
        else if (page_ == WP_STA) h = "▲▼ 선택   ● 연결   FN 옵션   ◀ 이전";
        else if (page_ == WP_SAVED) h = "▲▼ 선택   ● 관리   삭제키 삭제   ◀ 이전";
        else if (page_ == WP_AP) h = "▲▼ 선택   ● 편집   FN 비밀번호 표시   ◀ 이전";
        hint(c, g, h);
        if (toastEnd_ > app.nowMs()) drawToast(c, g);
        if (modal_ != M_NONE) drawModal(c, g);
    }

    void rules(Canvas &c) const { c.fillRect(0, 50, PANEL_W, 2, kRule); c.fillRect(0, 443, PANEL_W, 2, kRule); }
    void hint(Canvas &c, const Gfx &g, const char *h) const {
        int w = g.textWidth(GF_UI_20, h);
        g.text(c, GF_UI_20, LX(821) - w + 1, 468, h, kDim, false);
    }
    void center(Canvas &c, const Gfx &g, const char *s, int baseline, Color col) const {
        int w = g.textWidth(GF_UI_26, s);
        g.text(c, GF_UI_26, (PANEL_W - w) / 2, baseline, s, col, false);
    }
    void header(Canvas &c, const Gfx &g, const char *t1, int pos, int total, const char *t2 = 0) const {
        int x = LX(31);
        g.text(c, GF_UI_27, x, 34, t1, kAccent, false);
        if (t2) {
            x += g.textWidth(GF_UI_27, t1) + 10;
            g.text(c, GF_UI_27, x, 34, ">", kDim, false);
            g.text(c, GF_UI_27, x + 22, 34, t2, kAccent, false);
        }
        if (total) {
            char b[24]; snprintf(b, sizeof b, "%d / %d", pos, total);
            int w = g.textWidth(GF_UI_24, b);
            g.text(c, GF_UI_24, LX(822) - w, 32, b, kDim, false);
        }
    }

    // status strip: y 55..102
    void strip(Canvas &c, const Gfx &g, App &app, Wifi *w, const WifiStatusInfo &st) const {
        (void)w;
        Color accent = kOff; char l1[96], l2[128]; l2[0] = 0; int lit = 0; bool slash = false;
        long now = app.nowMs();
        switch (st.state) {
            case WS_OFF:  accent = kOff; slash = true; snprintf(l1, sizeof l1, "Wi-Fi 꺼짐");
                          snprintf(l2, sizeof l2, "켜면 네트워크 또는 스마트폰에 연결할 수 있습니다"); break;
            case WS_IDLE: accent = kOff; snprintf(l1, sizeof l1, "연결 안 됨");
                          snprintf(l2, sizeof l2, st.mode == WM_STA ? "네트워크를 선택하세요" : "AP 준비 중"); lit = 0; break;
            case WS_CONNECTING: accent = kAccent; lit = (int)((now / 350) % 4); snprintf(l1, sizeof l1, "연결 중...");
                          snprintf(l2, sizeof l2, "%s", st.ssid); break;
            case WS_CONNECTED: accent = kGreen; lit = 3; snprintf(l1, sizeof l1, "연결됨");
                          snprintf(l2, sizeof l2, "%s   IP %s", st.ssid, st.ip); break;
            case WS_AP_STARTING: accent = kAccent; lit = (int)((now / 350) % 4); snprintf(l1, sizeof l1, "공유 준비 중...");
                          snprintf(l2, sizeof l2, "%s", st.ssid); break;
            case WS_AP_WAITING: accent = kBlue; lit = (int)((now / 600) % 4); snprintf(l1, sizeof l1, "스마트폰 연결 대기 중");
                          snprintf(l2, sizeof l2, "%s", st.ssid); break;
            default: accent = kBlue; lit = 3; snprintf(l1, sizeof l1, "스마트폰 연결됨");
                          snprintf(l2, sizeof l2, "%s   %d대 접속 중", st.ssid, st.clients); break;
        }
        c.fillRect(LX(14), 55, LX(839) - LX(14), 47, kCard);
        c.fillRect(LX(14), 55, 5, 47, accent);
        glyph(c, LX(52), 90, 18, lit, kWhite, kRule, slash);
        char b[128];
        fitText(g, GF_UI_24, l1, LX(740), b, sizeof b);
        Color tc = st.state == WS_CONNECTED ? kGreenTx : (st.state >= WS_AP_STARTING ? kWhite : (st.state == WS_CONNECTING ? kAccent : kDim));
        g.text(c, GF_UI_24, LX(84), 78, b, tc, false);
        fitText(g, GF_UI_19, l2, LX(740), b, sizeof b);
        g.text(c, GF_UI_19, LX(84), 97, b, kDim, false);
    }

    void rowBg(Canvas &c, int y, bool sel) const { if (sel) fillL(c, 14, y, 839, y + ROW_H, kSel); }

    static int clampStart(int sel, int n, int vis) {
        int s = sel - vis / 2; if (s > n - vis) s = n - vis; if (s < 0) s = 0; return s;
    }
    void scrollbar(Canvas &c, int start, int n) const {
        if (n <= VIS) return;
        int th = VIS * ROW_PITCH, bh = th * VIS / n, by = ROW_TOP + (th - bh) * start / (n - VIS);
        c.fillRect(PANEL_W - 6, ROW_TOP, 2, th, kRule); c.fillRect(PANEL_W - 6, by, 2, bh, kDim);
    }

    void drawMaster(Canvas &c, const Gfx &g, Wifi *w, const WifiStatusInfo &st) {
        int n = masterCount(w); int &sel = sel_[WP_MASTER]; if (sel >= n) sel = n - 1;
        bool on = w->enabled(); bool sta = w->mode() == WM_STA;
        for (int i = 0; i < n; i++) {
            int y = ROW_TOP + i * ROW_PITCH; bool s = i == sel;
            rowBg(c, y, s);
            bool live = i < 2 || on;
            Color tc = s ? kWhite : (live ? kWhite : kDisab);
            Color vc = s ? kWhite : (live ? kDim : kDisab);
            const char *label; char val[96]; val[0] = 0;
            if (i == 0) { label = "Wi-Fi"; }
            else if (i == 1) { label = "연결 방식"; snprintf(val, sizeof val, "%s", sta ? "네트워크 연결" : "스마트폰 연결"); }
            else if (sta && i == 2) { label = "네트워크 선택";
                snprintf(val, sizeof val, "%s", st.state == WS_CONNECTED ? st.ssid : (st.state == WS_CONNECTING ? "연결 중..." : "연결 안 됨")); }
            else if (sta && i == 3) { label = "저장된 네트워크"; snprintf(val, sizeof val, "%d개", w->savedCount()); }
            else if (sta && i == 4) { label = "네트워크 직접 추가"; }
            else if (i == 2) { label = "스마트폰 연결 화면";
                snprintf(val, sizeof val, "%s", st.state == WS_AP_CLIENT ? "접속 중" : (st.state >= WS_AP_STARTING ? "대기 중" : "시작 안 됨")); }
            else { label = "AP 이름 / 비밀번호"; const char *s2, *p2; apCreds(&s2, &p2); snprintf(val, sizeof val, "%s", s2); }
            g.text(c, GF_UI_26, LX(40), y + 31, label, tc, false);
            if (i == 0) {                                    // toggle switch
                int px = LX(790) - 52, py = y + 10;
                pill(c, px, py, 52, 26, on ? (s ? kWhite : kGreen) : kOff);
                disc(c, on ? px + 52 - 13.0f : px + 13.0f, py + 13.0f, 10.0f, on && s ? kSel : kWhite);
                const char *t = on ? "켜짐" : "꺼짐";
                int tw = g.textWidth(GF_UI_24, t);
                g.text(c, GF_UI_24, px - 12 - tw, y + 31, t, vc, false);
            } else if (val[0]) {
                char b[96]; fitText(g, GF_UI_24, val, LX(420), b, sizeof b);
                int tw = g.textWidth(GF_UI_24, b);
                g.text(c, GF_UI_24, LX(810) - tw, y + 31, b, vc, false);
            } else if (live) {
                g.text(c, GF_UI_26, LX(805), y + 31, ">", vc, false);
            }
            if (i == 1) {                                    // mode switch glyph hint
                (void)0;
            }
        }
        int y0 = ROW_TOP + n * ROW_PITCH + 14;
        const char *d1 = sta ? "주변 Wi-Fi 네트워크에 카메라를 연결합니다." : "카메라가 Wi-Fi를 만들고 스마트폰이 접속합니다.";
        const char *d2 = sta ? "저장된 네트워크는 다음에 바로 연결할 수 있습니다." : "스마트폰으로 사진을 보내는 전송 모드입니다.";
        g.text(c, GF_UI_20, LX(40), y0 + 14, d1, kDim, false);
        g.text(c, GF_UI_20, LX(40), y0 + 40, d2, kDim, false);
    }

    void drawSta(Canvas &c, const Gfx &g, Wifi *w, const WifiStatusInfo &st) {
        (void)st;
        if (!w->enabled() || w->mode() != WM_STA) { center(c, g, "Wi-Fi가 꺼져 있습니다", 260, kDim); return; }
        int n = nRows_ + 1; int &sel = sel_[WP_STA]; if (sel >= n) sel = n - 1;
        if (nRows_ == 0) {
            center(c, g, scanning(w) ? "네트워크 검색 중..." : "검색된 네트워크가 없습니다", 230, kDim);
            drawRescanRow(c, g, ROW_TOP + 3 * ROW_PITCH, sel == 0, scanning(w));
            return;
        }
        int start = clampStart(sel, n, VIS), end = start + VIS < n ? start + VIS : n;
        for (int i = start; i < end; i++) {
            int y = ROW_TOP + (i - start) * ROW_PITCH; bool s = i == sel;
            if (i == nRows_) { drawRescanRow(c, g, y, s, scanning(w)); continue; }
            const Row &r = rows_[i];
            rowBg(c, y, s);
            Color tc = s ? kWhite : (r.connected ? kGreenTx : kWhite);
            if (r.secured) lockIcon(c, LX(38), y + 15, s ? kWhite : kDim);
            char b[48]; fitText(g, GF_UI_26, r.ssid, LX(390), b, sizeof b);
            g.text(c, GF_UI_26, LX(70), y + 31, b, tc, false);
            int rx = LX(826) - 28;                           // signal bars (right edge)
            bars(c, rx, y + 33, r.signal, false);
            int tx = rx - 14;
            if (r.connected || r.connecting || r.saved) {
                const char *t = r.connected ? "연결됨" : (r.connecting ? "연결 중..." : "저장됨");
                Color col = s ? kWhite : (r.connected ? kGreenTx : (r.connecting ? kAccent : kDim));
                int tw = g.textWidth(GF_UI_20, t);
                if (r.connected) checkIcon(c, tx - tw - 22, y + 18, col);
                g.text(c, GF_UI_20, tx - tw, y + 30, t, col, false);
            }
        }
        scrollbar(c, start, n);
    }
    bool scanning(Wifi *w) const { return w->scanning(); }
    void drawRescanRow(Canvas &c, const Gfx &g, int y, bool sel, bool busy) const {
        rowBg(c, y, sel);
        g.text(c, GF_UI_26, LX(70), y + 31, busy ? "검색 중..." : "다시 검색", sel ? kWhite : kAccent, false);
        glyph(c, LX(52), y + 34, 12, busy ? 2 : 3, sel ? kWhite : kAccent, kRule, false);
    }

    void drawSaved(Canvas &c, const Gfx &g, Wifi *w, const WifiStatusInfo &st) {
        int n = w->savedCount(); int &sel = sel_[WP_SAVED];
        if (n == 0) { center(c, g, "저장된 네트워크가 없습니다", 250, kDim); return; }
        if (sel >= n) sel = n - 1;
        int start = clampStart(sel, n, VIS), end = start + VIS < n ? start + VIS : n;
        for (int i = start; i < end; i++) {
            int y = ROW_TOP + (i - start) * ROW_PITCH; bool s = i == sel;
            char ssid[33]; bool sec = true; w->savedAt(i, ssid, &sec);
            bool conn = st.state == WS_CONNECTED && !strcmp(st.ssid, ssid);
            rowBg(c, y, s);
            if (sec) lockIcon(c, LX(38), y + 15, s ? kWhite : kDim);
            char b[48]; fitText(g, GF_UI_26, ssid, LX(500), b, sizeof b);
            g.text(c, GF_UI_26, LX(70), y + 31, b, s ? kWhite : (conn ? kGreenTx : kWhite), false);
            const char *t = conn ? "연결됨" : "";
            if (conn) {
                int tw = g.textWidth(GF_UI_20, t);
                checkIcon(c, LX(810) - tw - 24, y + 18, s ? kWhite : kGreenTx);
                g.text(c, GF_UI_20, LX(810) - tw, y + 30, t, s ? kWhite : kGreenTx, false);
            }
        }
        scrollbar(c, start, n);
    }

    void drawAp(Canvas &c, const Gfx &g, Wifi *w, const WifiStatusInfo &st) {
        (void)w;
        const char *s, *p; apCreds(&s, &p);
        int &sel = sel_[WP_AP];
        for (int i = 0; i < 3; i++) {
            int y = ROW_TOP + i * ROW_PITCH; bool sl = i == sel;
            rowBg(c, y, sl);
            Color tc = sl ? kWhite : kWhite, vc = sl ? kWhite : kDim;
            const char *label = i == 0 ? "Wi-Fi 이름 (SSID)" : (i == 1 ? "비밀번호" : "공유 중지 (Wi-Fi 끄기)");
            g.text(c, GF_UI_26, LX(40), y + 31, label, i == 2 && !sl ? kRed : tc, false);
            char val[80]; val[0] = 0;
            if (i == 0) snprintf(val, sizeof val, "%s", s);
            else if (i == 1) {
                if (showPw_) snprintf(val, sizeof val, "%s", p);
                else { int n = (int)strlen(p); if (n > 20) n = 20; for (int k = 0; k < n; k++) val[k] = '*'; val[n] = 0; }
            }
            if (val[0]) {
                char b[80]; fitText(g, GF_UI_24, val, LX(420), b, sizeof b);
                int tw = g.textWidth(GF_UI_24, b);
                g.text(c, GF_UI_24, LX(810) - tw, y + 31, b, vc, false);
            }
        }
        // phone instructions card
        int cy = ROW_TOP + 3 * ROW_PITCH + 6, ch = 443 - 8 - cy;
        c.fillRect(LX(14), cy, LX(839) - LX(14), ch, kCard);
        c.rectOutline(LX(14), cy, LX(839) - LX(14), ch, kCardLn);
        bool joined = st.state == WS_AP_CLIENT;
        g.text(c, GF_UI_24, LX(32), cy + 28, joined ? "스마트폰이 연결되었습니다" : "스마트폰에서 이렇게 연결하세요", joined ? kGreenTx : kAccent, false);
        char l[160];
        snprintf(l, sizeof l, "1  Wi-Fi 설정에서 \"%s\" 선택", s);
        char b[160]; fitText(g, GF_UI_20, l, LX(780), b, sizeof b);
        g.text(c, GF_UI_20, LX(32), cy + 56, b, kWhite, false);
        g.text(c, GF_UI_20, LX(32), cy + 80, "2  위에 표시된 비밀번호 입력", kWhite, false);
        snprintf(l, sizeof l, "3  브라우저에서 http://%s 열기", st.ip[0] ? st.ip : "192.168.122.1");
        g.text(c, GF_UI_20, LX(32), cy + 104, l, kWhite, false);
        if (ch > 120) g.text(c, GF_UI_19, LX(32), cy + ch - 8, joined ? "사진 전송 화면은 스마트폰에서 열립니다 (전송 프로토콜: 후속)" : "사진 전송은 연결 후 스마트폰에서 시작합니다 (후속)", kDim, false);
    }

    void drawToast(Canvas &c, const Gfx &g) const {
        char b[128]; fitText(g, GF_UI_24, toast_, LX(740), b, sizeof b);
        int tw = g.textWidth(GF_UI_24, b), w = tw + 36, x = (PANEL_W - w) / 2, y = 396;
        c.fillRect(x, y, w, 36, rgba(0x10, 0x11, 0x14, 240));
        c.rectOutline(x, y, w, 36, kAccent);
        g.text(c, GF_UI_24, x + 18, y + 26, b, kWhite, false);
    }

    void drawModal(Canvas &c, const Gfx &g) const {
        c.fillRect(0, 0, PANEL_W, PANEL_H, rgba(0, 0, 0, 0xaa));
        int rows = mN_, H = 62 + rows * 48 + 18;
        int x0 = LX(161), x1 = LX(691), top = (PANEL_H - H) / 2;
        c.fillRect(x0, top, x1 - x0, H, kCard); c.rectOutline(x0, top, x1 - x0, H, kCardLn);
        char b[80];
        if (modal_ == M_CONFIRM) snprintf(b, sizeof b, "\"%s\" 삭제?", target_); else snprintf(b, sizeof b, "%s", target_);
        char f[80]; fitText(g, GF_UI_26, b, x1 - x0 - 40, f, sizeof f);
        g.text(c, GF_UI_26, x0 + 20, top + 40, f, kWhite, false);
        for (int i = 0; i < mN_; i++) {
            int y = top + 56 + i * 48; bool s = i == mSel_;
            if (s) c.fillRect(x0 + 10, y, x1 - x0 - 20, 46, kSel);
            bool danger = mItem_[i] == O_FORGET || mItem_[i] == O_FORGET_YES;
            g.text(c, GF_UI_26, x0 + 32, y + 32, optLabel(mItem_[i]), s ? kWhite : (danger ? kRed : kDim), false);
        }
    }

    int  page_; bool direct_, autoStart_, started_;
    int  sel_[4];
    Row  rows_[MAXN]; int nRows_;
    bool scanReq_, wasScanning_; long nextScan_;
    int  modal_, mSel_, mN_; int mItem_[4];
    char toast_[128]; long toastEnd_;
    int  prevState_, prevErrSeq_;
    char target_[33], pending_[33], apSsid_[33], apPw_[64];
    char pskTitle_[64], pskPrompt_[64];
    bool showPw_; long apTry_;
    App *app_;
};

} // namespace

void wifiApCredentials(const char **ssid, const char **pw) {
    const char *s = textFieldValue("wifi.ssid"), *p = textFieldValue("wifi.psk");
    *ssid = s && *s ? s : "DIRECT-a5000"; *pw = p ? p : "";
}

bool wifiStartPhoneAp(App &app) {
    Wifi *w = app.wifi(); if (!w) return false;
    if (!w->enabled()) w->setEnabled(true);
    if (w->mode() != WM_AP) w->setMode(WM_AP);
    WifiStatusInfo st; w->status(&st);
    if (st.state >= WS_AP_STARTING) return true;           // already running: keep it (and its phone)
    const char *s, *p; wifiApCredentials(&s, &p);
    return w->startAp(s, p);
}

bool wifiMenuEntry(const char *key, const char *label, int *page, bool *autoStart) {
    struct E { const char *key, *label; int page; bool autoStart; };
    static const E kE[] = {
        { "info-na-wifi-config", "액세스 지점 설정", WP_MASTER, false },   // the Wi-Fi hub (on/off, mode, networks)
        { "info-na-transfer", "스마트폰으로 전송", WP_AP, true },          // radio on + AP mode + our AP page
        { "info-na-transfer", "PC로 전송", WP_STA, true },                  // radio on + STA mode + network list
    };
    for (unsigned i = 0; i < sizeof kE / sizeof kE[0]; i++)
        if (!strcmp(kE[i].key, key) && !strcmp(kE[i].label, label)) { *page = kE[i].page; *autoStart = kE[i].autoStart; return true; }
    return false;
}

Screen *createWifiScreen(int page) { return new WifiScreen(page); }

} // namespace uicore
