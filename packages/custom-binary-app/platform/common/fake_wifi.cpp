// Fake Wi-Fi backend for the host simulator (platform helper, not core). A handful of invented
// networks, time-based transitions (scan ~0.9 s, connect ~1.5 s, AP start ~0.8 s, a fake phone joins the
// AP after ~6 s) so the Wi-Fi UI can be exercised without radio hardware. Timings can be shortened for
// tests with A5K_FAKE_WIFI_MS (scale percent, default 100).
// REAL BACKEND = FOLLOW-UP: a5000 ar6003 / wpa_supplicant-like stack + emulator Wi-Fi model behind the
// same uicore::Wifi interface. Nothing here touches any device.
#include "uicore/platform.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>

using namespace uicore;

namespace {

struct Ap { const char *ssid; int signal; bool secured; const char *pw; };
const Ap kAir[] = {                       // what "is in the air"
    { "HomeWiFi_5G",      88, true,  "homewifi1" },
    { "AndroidHotspot",   72, true,  "hotspot99" },
    { "Cafe Free WiFi",   64, false, "" },
    { "iptime-A402",      45, true,  "aaaaaaaa" },
    { "KT_GiGA_2G_8F31",  30, true,  "giga0000" },
    { "Studio-Guest",     21, true,  "guest2024" },
};
const int kAirN = (int)(sizeof kAir / sizeof kAir[0]);

class FakeWifi : public Wifi {
public:
    explicit FakeWifi(Clock *clk) : clk_(clk), on_(false), mode_(WM_STA), scanEnd_(0), scanned_(false),
        state_(WS_OFF), tState_(0), nSaved_(0), lastErr_(WE_NONE), errSeq_(0), pending_(-1), ap_(false), phoneMs_(6000) {
        scale_ = 100; const char *e = getenv("A5K_FAKE_WIFI_MS"); if (e && *e) scale_ = atoi(e);
        if (scale_ < 1) scale_ = 1;
        phoneMs_ = 6000 * scale_ / 100;
        target_[0] = 0; apSsid_[0] = 0;
        saveNet("HomeWiFi_5G", "homewifi1", true);    // pre-saved: one in range, one not
        saveNet("Office-LAN", "office-lan1", true);
    }

    bool enabled() { return on_; }
    void setEnabled(bool on) {
        if (on == on_) return;
        on_ = on;
        scanned_ = false;
        if (!on) { ap_ = false; pending_ = -1; setState(WS_OFF); }
        else setState(WS_IDLE);
    }
    int  mode() { return mode_; }
    void setMode(int m) {
        if (m == mode_) return;
        mode_ = m; ap_ = false; pending_ = -1; target_[0] = 0;
        if (on_) setState(WS_IDLE);
    }

    bool beginScan() {
        if (!on_ || mode_ != WM_STA) return false;
        scanEnd_ = now() + ms(900); scanned_ = false; return true;
    }
    bool scanning() { service(); return scanEnd_ != 0; }
    int  scanResults(WifiNetwork *out, int max) {
        service();
        if (!on_ || !scanned_) return 0;
        int n = 0;
        for (int i = 0; i < kAirN && n < max; i++) {
            snprintf(out[n].ssid, sizeof out[n].ssid, "%s", kAir[i].ssid);
            // tiny deterministic wobble so a rescan visibly refreshes
            int sg = kAir[i].signal + (int)((now() / 1000 + i * 7) % 5) - 2;
            out[n].signal = sg < 1 ? 1 : (sg > 100 ? 100 : sg);
            out[n].secured = kAir[i].secured; out[n].saved = findSaved(kAir[i].ssid) >= 0;
            n++;
        }
        return n;
    }
    bool connect(const char *ssid, const char *pw) {
        if (!on_ || mode_ != WM_STA || !ssid || !*ssid) return false;
        service();
        snprintf(target_, sizeof target_, "%s", ssid);
        int s = findSaved(ssid);
        if (pw) snprintf(tryPw_, sizeof tryPw_, "%s", pw);
        else if (s >= 0) snprintf(tryPw_, sizeof tryPw_, "%s", saved_[s].pw);
        else tryPw_[0] = 0;
        pending_ = 1; lastErr_ = WE_NONE; ip_[0] = 0;
        setState(WS_CONNECTING);
        return true;
    }
    void disconnect() { if (on_ && mode_ == WM_STA) { pending_ = -1; target_[0] = 0; setState(WS_IDLE); } }
    int  savedCount() { return nSaved_; }
    bool savedAt(int i, char ssid[33], bool *secured) {
        if (i < 0 || i >= nSaved_) return false;
        snprintf(ssid, 33, "%s", saved_[i].ssid); if (secured) *secured = saved_[i].secured; return true;
    }
    bool forget(const char *ssid) {
        int i = findSaved(ssid); if (i < 0) return false;
        for (int k = i; k + 1 < nSaved_; k++) saved_[k] = saved_[k + 1];
        nSaved_--;
        if (state_ == WS_CONNECTED && !strcmp(target_, ssid)) disconnect();     // forgetting the live one drops it
        return true;
    }

    bool startAp(const char *ssid, const char *pw) {
        if (!on_ || mode_ != WM_AP || !ssid || !*ssid) return false;
        (void)pw;
        snprintf(apSsid_, sizeof apSsid_, "%s", ssid);
        ap_ = true; setState(WS_AP_STARTING); return true;
    }
    void stopAp() { if (ap_) { ap_ = false; if (on_) setState(WS_IDLE); } }

    void status(WifiStatusInfo *o) {
        service();
        memset(o, 0, sizeof *o);
        o->state = state_; o->mode = mode_; o->lastError = lastErr_; o->errSeq = errSeq_;
        if (state_ == WS_CONNECTING || state_ == WS_CONNECTED) {
            snprintf(o->ssid, sizeof o->ssid, "%s", target_);
            if (state_ == WS_CONNECTED) { o->signal = airSignal(target_); snprintf(o->ip, sizeof o->ip, "%s", ip_); }
        } else if (state_ >= WS_AP_STARTING) {
            snprintf(o->ssid, sizeof o->ssid, "%s", apSsid_);
            snprintf(o->ip, sizeof o->ip, "192.168.122.1");
            o->clients = state_ == WS_AP_CLIENT ? 1 : 0;
        }
    }

private:
    struct Saved { char ssid[33]; char pw[64]; bool secured; };

    long now() { return clk_ ? clk_->nowMs() : 0; }
    long ms(long v) { return v * scale_ / 100; }
    void setState(int s) { state_ = s; tState_ = now(); }
    int  findSaved(const char *ssid) { for (int i = 0; i < nSaved_; i++) if (!strcmp(saved_[i].ssid, ssid)) return i; return -1; }
    void saveNet(const char *ssid, const char *pw, bool secured) {
        int i = findSaved(ssid);
        if (i < 0) { if (nSaved_ >= 8) return; i = nSaved_++; }
        snprintf(saved_[i].ssid, sizeof saved_[i].ssid, "%s", ssid);
        snprintf(saved_[i].pw, sizeof saved_[i].pw, "%s", pw); saved_[i].secured = secured;
    }
    int airSignal(const char *ssid) { for (int i = 0; i < kAirN; i++) if (!strcmp(kAir[i].ssid, ssid)) return kAir[i].signal; return 0; }

    void fail(int err) { lastErr_ = err; errSeq_++; pending_ = -1; target_[0] = 0; setState(WS_IDLE); }

    void service() {
        long t = now();
        if (scanEnd_ && t >= scanEnd_) { scanEnd_ = 0; scanned_ = true; }
        if (state_ == WS_CONNECTING && pending_ > 0 && t - tState_ >= ms(1500)) {
            int a = -1; for (int i = 0; i < kAirN; i++) if (!strcmp(kAir[i].ssid, target_)) a = i;
            if (a < 0) fail(WE_NOT_FOUND);
            else if (kAir[a].secured && strcmp(kAir[a].pw, tryPw_)) fail(WE_AUTH);
            else {
                saveNet(kAir[a].ssid, tryPw_, kAir[a].secured);
                snprintf(ip_, sizeof ip_, "192.168.1.%d", 20 + a);
                pending_ = -1; setState(WS_CONNECTED);
            }
        }
        if (ap_ && state_ == WS_AP_STARTING && t - tState_ >= ms(800)) setState(WS_AP_WAITING);
        if (ap_ && state_ == WS_AP_WAITING && t - tState_ >= phoneMs_) setState(WS_AP_CLIENT);   // the fake phone joins
    }

    Clock *clk_;
    bool on_; int mode_;
    long scanEnd_; bool scanned_;
    int state_; long tState_;
    Saved saved_[8]; int nSaved_;
    char target_[33], tryPw_[64], ip_[16], apSsid_[33];
    int lastErr_, errSeq_, pending_;
    bool ap_;
    long phoneMs_; int scale_;
};

} // namespace

namespace platform { uicore::Wifi *createFakeWifi(uicore::Clock *clk) { return new FakeWifi(clk); } }
