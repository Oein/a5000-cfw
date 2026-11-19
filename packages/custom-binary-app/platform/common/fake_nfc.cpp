// Fake NFC backend for the host simulator (platform helper, not core). Models the a5000 chip's visible
// behaviour (emulator/docs/RE_NFC.md): a phone reaching the antenna (RFDET) -> NFS_TAG while the phone reads
// the NDEF image (~0.8 s) -> handover event = one NfcTap (the tag content == the AP SSID/password set via
// setPayload) and NFS_PAIRING until the app calls finishPairing().
// Triggers: Nfc::injectTap() (a5kctl `nfc` / ctl "nfc"), env A5K_FAKE_NFC_TAP=<ms after start> (one auto tap),
// A5K_FAKE_NFC_FAIL=1 (the phone pulls away mid-read: NE_READ). Time scale shares A5K_FAKE_WIFI_MS.
// REAL BACKEND = FOLLOW-UP: nfc.ko / libInfraNfc handover (0x104 / 0x3A4D2001) + emulator a5k_nfc model.
#include "uicore/platform.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>

using namespace uicore;

namespace {

class FakeNfc : public Nfc {
public:
    explicit FakeNfc(Clock *clk) : clk_(clk), on_(true), state_(NFS_IDLE), tapSeq_(0), lastErr_(NE_NONE), errSeq_(0),
        tagEnd_(0), pairEnd_(0), tapReady_(false), autoAt_(0), fail_(false) {
        int scale = 100; const char *e = getenv("A5K_FAKE_WIFI_MS"); if (e && *e) scale = atoi(e);
        if (scale < 1) scale = 1;
        readMs_ = 800 * (scale > 100 ? 100 : scale) / 100; if (readMs_ < 50) readMs_ = 50;
        e = getenv("A5K_FAKE_NFC_TAP"); if (e && *e) autoAt_ = clk_->nowMs() + atoi(e) + 1;
        e = getenv("A5K_FAKE_NFC_FAIL"); fail_ = e && *e == '1';
        ssid_[0] = pw_[0] = 0;
    }
    bool enabled() { return on_; }
    void setEnabled(bool on) { if (on == on_) return; on_ = on; tapReady_ = false; state_ = on ? NFS_IDLE : NFS_OFF; }
    void status(NfcStatusInfo *o) {
        service();
        o->state = on_ ? state_ : NFS_OFF; o->tapSeq = tapSeq_; o->lastError = lastErr_; o->errSeq = errSeq_;
    }
    void setPayload(const char *s, const char *p) {
        snprintf(ssid_, sizeof ssid_, "%s", s ? s : ""); snprintf(pw_, sizeof pw_, "%s", p ? p : "");
    }
    bool pollTap(NfcTap *o) {
        service();
        if (!tapReady_) return false;
        tapReady_ = false;
        if (o) { snprintf(o->ssid, sizeof o->ssid, "%s", ssid_); snprintf(o->password, sizeof o->password, "%s", pw_); }
        return true;
    }
    void finishPairing(bool) { if (on_ && state_ == NFS_PAIRING) state_ = NFS_IDLE; pairEnd_ = 0; }
    bool injectTap() {
        service();
        if (!on_ || state_ != NFS_IDLE) return false;
        state_ = NFS_TAG; tapSeq_++; tagEnd_ = clk_->nowMs() + readMs_; return true;
    }
private:
    void service() {
        long now = clk_->nowMs();
        if (autoAt_ && now >= autoAt_) { autoAt_ = 0; injectTapRaw(); }
        if (state_ == NFS_TAG && now >= tagEnd_) {
            if (fail_) { state_ = NFS_IDLE; lastErr_ = NE_READ; errSeq_++; fail_ = false; }   // one-shot failure
            else { state_ = NFS_PAIRING; tapReady_ = true; pairEnd_ = now + 60000; }
        }
        if (state_ == NFS_PAIRING && pairEnd_ && now >= pairEnd_) { state_ = NFS_IDLE; pairEnd_ = 0; tapReady_ = false; }   // app never answered
    }
    void injectTapRaw() { if (on_ && state_ == NFS_IDLE) { state_ = NFS_TAG; tapSeq_++; tagEnd_ = clk_->nowMs() + readMs_; } }
    Clock *clk_; bool on_; int state_, tapSeq_, lastErr_, errSeq_; long tagEnd_, pairEnd_; bool tapReady_; long autoAt_; bool fail_; int readMs_;
    char ssid_[33], pw_[64];
};

} // namespace

namespace platform { Nfc *createFakeNfc(Clock *clk) { return new FakeNfc(clk); } }
