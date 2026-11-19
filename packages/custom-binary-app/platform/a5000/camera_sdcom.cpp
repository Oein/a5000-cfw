// a5000 CameraControl adapter over SDCom (libsdcom.so -> ScalarDaemon). M0 = read-only.
// Component id 2, queue 0x1540432 (TRACKB_RUNTIME §3). connect() returns 0 only in app
// mode (-5 on the stock shooting screen), so the core retries. readInfo reads two
// side-effect-free values: 0x000d GetBodyFirmwareVersion, 0x0009 GetSystemState.
//
// readState (M1-lite) additionally issues 0x1002 CameraGetParameters (pure read) and takes
// id 267 (Int, apertureX100: f/1.8 -> 180). OpenCamera 0x1000 / any state-changing message is
// NEVER sent: the stock shooting screen already has the camera open. Brick-safety (CLAUDE.md §1): disconnect() is called from the crash
// handler; object sizes are statically checked.
#include "uicore/platform.h"
#include "uicore/log.h"
#include <stdlib.h>

using namespace uicore;

static const unsigned SDCOM_COMPONENT = 2;

#ifdef A5K_FAKE_CAMERA
// Standalone build without libsdcom (SD_DRYRUN style). Canned values.
namespace {
class FakeCamera : public CameraControl {
public:
    bool connect() { return false; }
    void disconnect() {}
    bool connected() const { return false; }
    bool dryrun() const { return true; }
    int  readInfo(int *ids, int *vals, int max) {
        int n = 0;
        if (n < max) { ids[n] = 97; vals[n] = 0x1000a; n++; }
        if (n < max) { ids[n] = 69; vals[n] = 2;       n++; }
        return n;
    }
};
}
namespace a5000 { uicore::CameraControl *createCamera(bool) { return new FakeCamera(); } }

#else
#include "sdcom.h"   // app/native/sdcom via -I

typedef char check_intparam_size[(sizeof(SDComMsgIntParam) == 0x10) ? 1 : -1];

// Bionic libsdcom.so lacks these header-inline accessors; glibc copy exports them.
#ifdef A5K_SDCOM_INLINE_FALLBACK
SDComIPCListener::~SDComIPCListener() {}
unsigned short SDComMsgParam::getId() const {
    return *reinterpret_cast<const unsigned short *>(reinterpret_cast<const char *>(this) + 4);
}
int SDComMsgIntParam::getData() const {
    return *reinterpret_cast<const int *>(reinterpret_cast<const char *>(this) + 0x0c);
}
#endif

namespace {
class QuietListener : public SDComIPCListener {
public: void onEventReceived(SDComMsg *) {}
};

class SdcomCamera : public CameraControl {
public:
    SdcomCamera(bool dryrun) : dryrun_(dryrun), connected_(false) {
        ipc_ = new SDComIPC(&listener_);
    }
    ~SdcomCamera() { disconnect(); delete ipc_; }

    bool connect() {
        if (dryrun_) return false;
        int rc = ipc_->connect(SCALARDAEMON_QUEUE, SDCOM_COMPONENT);
        connected_ = (rc == 0);
        logf("camera: connect = %d%s", rc, rc == -5 ? " (not app mode)" : "");
        return connected_;
    }
    void disconnect() {
        if (connected_) { ipc_->disconnect(); logf("camera: disconnect"); }
        connected_ = false;
    }
    bool connected() const { return connected_; }
    bool dryrun() const { return dryrun_; }

    int readInfo(int *ids, int *vals, int max) {
        if (!connected_) return 0;
        int n = getInts(0x000d, ids, vals, max);
        if (n < 0) n = 0;
        int m = getInts(0x0009, ids + n, vals + n, max - n);
        if (m > 0) n += m;
        return n;
    }

    // Pure-read snapshot: only the live aperture for now (SDCom param 267 in 0x1002, Int,
    // raw = F*100, 0 = unknown -> OSD hides F). Shutter (id 268) is an IntArray [num,den]
    // and is not read yet (follow-up). Settings (has[]) are left untouched.
    bool readState(CameraState *o) {
        if (!connected_ || !o) return false;
        enum { MAXP = 256, ID_APERTURE = 267 };
        int ids[MAXP], vals[MAXP];
        int n = getInts(0x1002, ids, vals, MAXP);
        if (n < 0) return false;
        o->apertureX100 = 0;
        for (int i = 0; i < n; i++)
            if (ids[i] == ID_APERTURE) { o->apertureX100 = vals[i]; break; }
        return true;
    }

private:
    int getInts(unsigned msgId, int *ids, int *vals, int max) {
        unsigned tx = 0; ipc_->createNewTransactionId(&tx);
        SDComMsg *msg = SDComMsgUtil::createNewMessage(SCALARDAEMON_QUEUE, SDCOM_MSG_REQUEST, tx);
        SDComMsgUtil::setMessageId(msg, (unsigned short)msgId);
        SDComMsg *rep = 0;
        int rc = ipc_->sendSync(msg, &rep, 5), n = 0;
        if (rc == 0 && rep) {
            int cnt = SDComMsgUtil::getNumOfParams(rep);
            for (int i = 0; i < cnt && n < max; i++) {
                SDComMsgParam *p = (i == 0) ? SDComMsgUtil::getFirstParam(rep, true)
                                            : SDComMsgUtil::getNextParam(rep, true);
                if (!p) break;
                if (p->getType() == 1) { ids[n] = p->getId(); vals[n] = static_cast<SDComMsgIntParam *>(p)->getData(); n++; }
                delete p;
            }
            SDComMsgUtil::freeMessage(rep, false);
        }
        SDComMsgUtil::freeMessage(msg, false);
        logf("camera: get 0x%04x rc=%d params=%d", msgId, rc, n);
        return rc == 0 ? n : -1;
    }

    QuietListener listener_;
    SDComIPC     *ipc_;
    bool          dryrun_, connected_;
};
}

namespace a5000 { uicore::CameraControl *createCamera(bool dryrun) { return new SdcomCamera(dryrun); } }
#endif
