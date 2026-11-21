// sdctl — ScalarDaemon 디버그 제어 CLI (sdprobe의 SET 가능 상위호환).
//
// 목적: 실기에서 카메라 모드/파라미터를 SDCom으로 바꿔가며 RE/검증하기 위한 디버그 도구.
// 조회(get/msg)는 항상 가능. **설정 변경(set/shift/named)은 전부 #ifdef A5K_DEBUG_API로 게이팅** →
// 정의 없이 빌드하면 query 전용(=sdprobe)이 되어 배포본에서 자연히 빠진다.
//
// 안전 (CLAUDE.md §1 벽돌위험0, docs/RE/sdcom/0005):
//   - 여기서 보내는 set은 전부 **휘발 엔진 상태**(0x1003 CameraSetParameters / Shift). 재부팅=원복, 플래시 미기록.
//   - libsdcom 객체 크기 정적검증(0x10). 비정상 종료 시 반드시 disconnect(osal 큐 방치=카메라 정지).
//   - storage format/펌웨어/Backup raw 류 **위험 msgId는 넣지 않는다**. 임의 msg는 generic 'msg'로만(사용자 책임).
//   - set 후 2초 대기 → 0x1002 read-back으로 반영값을 되읽어 **정직하게 확인**(비동기 반영, 0003 §2).
//
// 사용법:
//   sdctl <comp> get                      # 0x1002 현재 파라미터 전체
//   sdctl <comp> msg <id>[:p=v,...] ...    # 임의 메시지(sdprobe 호환, 조회용)
//   sdctl <comp> list                      # 알려진 named set 키 + 측정 enum 치트시트
//   [A5K_DEBUG_API] sdctl <comp> set <paramId> <value>   # 0x1003 {52=0, paramId=value}
//   [A5K_DEBUG_API] sdctl <comp> <name> <value>          # mode/iso/wb/afarea/flash/... (아래 표)
//   [A5K_DEBUG_API] sdctl <comp> shift-shutter|shift-aperture|shift-pl|shift-focus <n>
//   예) sdctl 12 mode 1   sdctl 12 iso 400   sdctl 12 shift-shutter -1
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <csignal>
#include <climits>
#include <unistd.h>
#include "sdcom.h"

// 정적 크기 검증: 라이브러리 객체 크기와 다르면 컴파일 실패 (sdcom/0005 사고 재발 방지)
typedef char check_intparam_size[(sizeof(SDComMsgIntParam) == 0x10) ? 1 : -1];

// ---- SDCom 메시지 id (docs/RE/sdcom/0002 카탈로그) ----
enum {
    MSG_GET_SYSTEM_STATE = 0x0009,
    MSG_GET_FW_VERSION   = 0x000D,
    MSG_GET_PARAMS       = 0x1002,  // CameraGetParameters
    MSG_SET_PARAMS       = 0x1003,  // CameraSetParameters { 52=0, paramId=value }
    MSG_SHIFT_APERTURE   = 0x1019,  // IntParam id=5 = step
    MSG_SHIFT_SHUTTER    = 0x101A,  // IntParam id=5 = step
    MSG_SHIFT_FOCUS      = 0x1013,  // IntParam id=12 = step
    MSG_SHIFT_PROGRAMLINE= 0x102D,  // IntParam id=5 = step
    SETPARAMS_FIRST_ID   = 52,      // 0x1003 첫 Int param(고정 0) — SdcomClient::setParam과 동일
};

static SDComIPC *g_ipc = 0;
static void onFatal(int sig) {
    if (g_ipc) { g_ipc->disconnect(); g_ipc = 0; }   // 큐 방치 방지
    signal(sig, SIG_DFL);
    raise(sig);
}

class Listener : public SDComIPCListener {
public:
    void onEventReceived(SDComMsg *m) {
        unsigned short id = 0;
        SDComMsgUtil::getMessageId(m, &id);
        printf("  [event] msgId=0x%04x params=%d\n", id, SDComMsgUtil::getNumOfParams(m));
        fflush(stdout);
    }
};

// 응답을 출력. want>=0이면 그 paramId의 값만 강조(read-back 확인용).
static void printParams(SDComMsg *m, int want = -1) {
    int n = SDComMsgUtil::getNumOfParams(m);
    printf("  params=%d\n", n);
    for (int i = 0; i < n; i++) {
        SDComMsgParam *p = (i == 0) ? SDComMsgUtil::getFirstParam(m, true) : SDComMsgUtil::getNextParam(m, true);
        if (!p) break;
        int type = p->getType();
        int pid = p->getId();
        bool hl = (want >= 0 && pid == want);
        if (want < 0 || hl) {
            printf("  %s#%d id=%u type=%d len=%d", hl ? ">>" : "  ", i, pid, type, p->getDataLength());
            if (type == 1) printf(" int=%d (0x%x)", static_cast<SDComMsgIntParam *>(p)->getData(),
                                  static_cast<SDComMsgIntParam *>(p)->getData());
            if (type == 5) {
                SDComMsgIntArrayParam *ap = static_cast<SDComMsgIntArrayParam *>(p);
                int n2 = ap->getArrayLength();
                printf(" arr[%d]=", n2);
                for (int k = 0; k < n2 && k < 64; k++) { int v = 0; ap->getDataAt(k, &v); printf("%s%d", k ? "," : "", v); }
            }
            printf("\n");
            fflush(stdout);
        }
        delete p;
    }
}

// (id,val) IntParam 배열을 실은 메시지 하나를 sendSync. reply를 받으면 printParams(want).
static int sendMsg(SDComIPC *ipc, unsigned short msgId,
                   const unsigned short *pids, const int *vals, int nparams,
                   int wantReadback = -1) {
    unsigned int tx = 0;
    ipc->createNewTransactionId(&tx);
    SDComMsg *msg = SDComMsgUtil::createNewMessage(SCALARDAEMON_QUEUE, SDCOM_MSG_REQUEST, tx);
    SDComMsgUtil::setMessageId(msg, msgId);
    for (int i = 0; i < nparams; i++) {
        SDComMsgIntParam *ip = new SDComMsgIntParam(pids[i], vals[i]);  // sizeof==0x10 (정적검증)
        printf("  + param id=%u int=%d -> addParam=%d\n", pids[i], vals[i], SDComMsgUtil::addParam(msg, ip));
        delete ip;                                 // addParam은 복사본 보관
    }
    SDComMsg *rep = NULL;
    int rc = ipc->sendSync(msg, &rep, 5);
    printf("msg 0x%04x tx=%u -> sendSync=%d\n", msgId, tx, rc);
    if (rc == 0 && rep) {
        unsigned short rid = 0;
        SDComMsgUtil::getMessageId(rep, &rid);
        printf("  reply msgId=0x%04x\n", rid);
        printParams(rep, wantReadback);
        SDComMsgUtil::freeMessage(rep, false);
    }
    SDComMsgUtil::freeMessage(msg, false);
    return rc;
}

// 0x1002 전체 조회(옵션: 특정 paramId 강조).
static void doGet(SDComIPC *ipc, int want = -1) {
    sendMsg(ipc, MSG_GET_PARAMS, 0, 0, 0, want);
}

// 0x1002에서 paramId 하나의 Int 값만 조용히 읽음. 없거나 비-Int면 INT_MIN.
static int getParamValue(SDComIPC *ipc, int paramId) {
    unsigned int tx = 0;
    ipc->createNewTransactionId(&tx);
    SDComMsg *msg = SDComMsgUtil::createNewMessage(SCALARDAEMON_QUEUE, SDCOM_MSG_REQUEST, tx);
    SDComMsgUtil::setMessageId(msg, MSG_GET_PARAMS);
    SDComMsg *rep = NULL;
    int out = INT_MIN;
    if (ipc->sendSync(msg, &rep, 5) == 0 && rep) {
        int n = SDComMsgUtil::getNumOfParams(rep);
        for (int i = 0; i < n; i++) {
            SDComMsgParam *p = (i == 0) ? SDComMsgUtil::getFirstParam(rep, true) : SDComMsgUtil::getNextParam(rep, true);
            if (!p) break;
            if ((int)p->getId() == paramId && p->getType() == 1)
                out = static_cast<SDComMsgIntParam *>(p)->getData();
            delete p;
        }
        SDComMsgUtil::freeMessage(rep, false);
    }
    SDComMsgUtil::freeMessage(msg, false);
    return out;
}

// ---- named set 키 → 0x1003 paramId (docs/RE/sdcom/0003, 0006 측정값) ----
struct NamedParam { const char *name; int paramId; const char *note; };
static const NamedParam kNamed[] = {
    { "mode",    263, "exposureMode (측정 지원 {0,1,2}, 앱컨텍스트)" },
    { "iso",     269, "isoGain (측정 사다리 0=AUTO,100..16000)" },
    { "wb",      260, "wbMode (측정 {0,1,4})" },
    { "wbtemp",  280, "wbColorTemperature" },
    { "afarea",  271, "afAreaMode (측정 {0,2,3})" },
    { "focus",   265, "focusMode" },
    { "flash",   264, "strobeModeType" },
    { "flashev", 278, "flash exposure comp" },
    { "redeye",  277, "redEyeReduction" },
    { "meter",   270, "meterMode" },
    { "drive",   318, "driveMode" },
    { "burst",   320, "burstSpeed (측정 {0,2})" },
    { "self",    328, "selfTimer" },
    { "effect",  286, "pictureEffect" },
    { "color",   261, "colorModeType (측정 지원=빈배열 → set 불가 가능)" },
    { "dro",     272, "droLevel" },
    { "hdr",     274, "hdrLevel" },
    { "aspect",  352, "imageAspectRatio (측정 {1,2,3})" },
    { "cspace",  356, "colorSpace (측정 {0,1})" },
    { "quality", 259, "stillFormatQuality" },
    { "fmttype", 257, "stillFormatType" },
    { 0, 0, 0 },
};

static void doList() {
    printf("named set 키 (sdctl <comp> <name> <value> → 0x1003 {52=0, paramId=value}):\n");
    for (const NamedParam *n = kNamed; n->name; n++)
        printf("  %-8s -> param %-4d  %s\n", n->name, n->paramId, n->note);
    printf("\nshift: shift-shutter/-aperture/-pl (id5 step), shift-focus (id12 step)\n");
    printf("generic 조회: get | msg <id>[:p=v,...]\n");
#ifndef A5K_DEBUG_API
    printf("\n*** SET 비활성: -DA5K_DEBUG_API 로 빌드해야 set/shift/named 가 동작 ***\n");
#endif
}

#ifdef A5K_DEBUG_API
// 0x1003 {52=0, paramId=value} 를 조용히(로그 최소) 전송. rc 반환.
static int quietSet(SDComIPC *ipc, int paramId, int value) {
    unsigned int tx = 0;
    ipc->createNewTransactionId(&tx);
    SDComMsg *msg = SDComMsgUtil::createNewMessage(SCALARDAEMON_QUEUE, SDCOM_MSG_REQUEST, tx);
    SDComMsgUtil::setMessageId(msg, MSG_SET_PARAMS);
    unsigned short ids[2] = { (unsigned short)SETPARAMS_FIRST_ID, (unsigned short)paramId };
    int           vs[2]  = { 0,                                   value };
    for (int i = 0; i < 2; i++) {
        SDComMsgIntParam *ip = new SDComMsgIntParam(ids[i], vs[i]);  // sizeof==0x10
        SDComMsgUtil::addParam(msg, ip);
        delete ip;
    }
    SDComMsg *rep = NULL;
    int rc = ipc->sendSync(msg, &rep, 5);
    if (rep) SDComMsgUtil::freeMessage(rep, false);
    SDComMsgUtil::freeMessage(msg, false);
    return rc;
}

// set-value v 를 0..maxVal 까지 쓸어넣고 매번 get read-back → set↔get 매핑표 추출.
// 한 connect로 수행, settle 350ms. 끝나면 원래 값으로 복구. (휘발/브릭0)
static void doSweep(SDComIPC *ipc, int paramId, int maxVal) {
    int orig = getParamValue(ipc, paramId);
    printf("sweep param %d (orig get=%d), set 0..%d:\n", paramId, orig, maxVal);
    for (int v = 0; v <= maxVal; v++) {
        int rc = quietSet(ipc, paramId, v);
        usleep(350000);
        int got = getParamValue(ipc, paramId);
        printf("  set %2d -> rc=%d get=%d%s\n", v, rc, got,
               (rc == 0 && got != INT_MIN && got != v) ? "  (remap)" : (rc != 0 ? "  (reject)" : ""));
        fflush(stdout);
    }
    if (orig != INT_MIN) { quietSet(ipc, paramId, orig); usleep(350000);
        printf("restored param %d -> set %d (get=%d)\n", paramId, orig, getParamValue(ipc, paramId)); }
}

// 0x1003 {52=0, paramId=value} → 2초 대기 → 0x1002로 그 paramId 되읽기(반영 확인).
static int doSet(SDComIPC *ipc, int paramId, int value) {
    printf("SET param %d = %d (0x1003)\n", paramId, value);
    unsigned short pids[2] = { (unsigned short)SETPARAMS_FIRST_ID, (unsigned short)paramId };
    int           vals[2] = { 0,                                   value };
    int rc = sendMsg(ipc, MSG_SET_PARAMS, pids, vals, 2);
    if (rc != 0) { printf("  -> 엔진 거부/실패(rc=%d) — 현재 모드에서 미지원일 수 있음\n", rc); return rc; }
    printf("  반영 대기 2s...\n"); usleep(2000000);
    printf("  read-back:\n"); doGet(ipc, paramId);
    return rc;
}
static int doShift(SDComIPC *ipc, unsigned short msgId, unsigned short pid, int step) {
    printf("SHIFT msg 0x%04x id%u step=%d\n", msgId, pid, step);
    unsigned short pids[1] = { pid };
    int           vals[1] = { step };
    int rc = sendMsg(ipc, msgId, pids, vals, 1);
    if (rc == 0) { printf("  반영 대기 2s...\n"); usleep(2000000); printf("  read-back:\n"); doGet(ipc); }
    return rc;
}
#endif

static void usage() {
    printf("usage: sdctl <componentId> <cmd> [args]\n");
    printf("  get | list | msg <id>[:p=v,...] ...\n");
#ifdef A5K_DEBUG_API
    printf("  set <paramId> <value> | <name> <value> | sweep <paramId> [maxVal] | shift-shutter|shift-aperture|shift-pl|shift-focus <n>\n");
    printf("  (SET 활성: A5K_DEBUG_API)\n");
#else
    printf("  (SET 비활성: query 전용. set을 쓰려면 -DA5K_DEBUG_API)\n");
#endif
}

int main(int argc, char **argv) {
    if (argc < 3) { usage(); return 2; }
    setvbuf(stdout, NULL, _IOLBF, 0);
    unsigned char comp = (unsigned char)strtoul(argv[1], NULL, 0);
    const char *cmd = argv[2];

    if (!strcmp(cmd, "list")) { doList(); return 0; }

    Listener listener;
    SDComIPC *ipc = new SDComIPC(&listener);
    if (getenv("SD_DRYRUN")) {
        SDComMsgIntParam *ip = new SDComMsgIntParam(5, 1);
        printf("dryrun: size=%u ok\n", (unsigned)sizeof(*ip));
        delete ip; return 0;
    }
    signal(SIGSEGV, onFatal); signal(SIGABRT, onFatal); signal(SIGBUS, onFatal);
    signal(SIGTERM, onFatal); signal(SIGINT, onFatal); signal(SIGHUP, onFatal); signal(SIGPIPE, SIG_IGN);

    int rc = ipc->connect(SCALARDAEMON_QUEUE, comp);
    if (rc == 0) g_ipc = ipc;
    printf("connect(0x%x, %u) = %d\n", SCALARDAEMON_QUEUE, comp, rc);
    if (rc != 0) { printf("connect 실패 — D2A 미개방(앱 전경 필요) 또는 큐 불가\n"); return 1; }

    int ret = 0;
    if (!strcmp(cmd, "get")) {
        doGet(ipc);
    } else if (!strcmp(cmd, "msg")) {
        // sdprobe 호환: argv[3..] 각각 <id>[:p=v,...]
        for (int a = 3; a < argc; a++) {
            char *rest = NULL;
            unsigned short id = (unsigned short)strtoul(argv[a], &rest, 0);
            unsigned short pids[16]; int vals[16]; int np = 0;
            if (rest && *rest == ':') {
                char *q = rest + 1;
                while (*q && np < 16) {
                    unsigned short pid = (unsigned short)strtoul(q, &q, 0);
                    if (*q != '=') break;
                    int val = (int)strtol(q + 1, &q, 0);
                    pids[np] = pid; vals[np] = val; np++;
                    if (*q == ',') q++;
                }
            }
            sendMsg(ipc, id, pids, vals, np);
        }
    } else {
#ifdef A5K_DEBUG_API
        if (!strcmp(cmd, "set") && argc >= 5) {
            ret = doSet(ipc, (int)strtol(argv[3], 0, 0), (int)strtol(argv[4], 0, 0));
        } else if (!strcmp(cmd, "sweep") && argc >= 4) {
            int maxv = (argc >= 5) ? (int)strtol(argv[4], 0, 0) : 24;
            doSweep(ipc, (int)strtol(argv[3], 0, 0), maxv);
        } else if (!strcmp(cmd, "shift-shutter") && argc >= 4) {
            ret = doShift(ipc, MSG_SHIFT_SHUTTER, 5, (int)strtol(argv[3], 0, 0));
        } else if (!strcmp(cmd, "shift-aperture") && argc >= 4) {
            ret = doShift(ipc, MSG_SHIFT_APERTURE, 5, (int)strtol(argv[3], 0, 0));
        } else if (!strcmp(cmd, "shift-pl") && argc >= 4) {
            ret = doShift(ipc, MSG_SHIFT_PROGRAMLINE, 5, (int)strtol(argv[3], 0, 0));
        } else if (!strcmp(cmd, "shift-focus") && argc >= 4) {
            ret = doShift(ipc, MSG_SHIFT_FOCUS, 12, (int)strtol(argv[3], 0, 0));
        } else {
            // named: sdctl <comp> <name> <value>
            const NamedParam *found = 0;
            for (const NamedParam *n = kNamed; n->name; n++) if (!strcmp(cmd, n->name)) { found = n; break; }
            if (found && argc >= 4) ret = doSet(ipc, found->paramId, (int)strtol(argv[3], 0, 0));
            else { usage(); ret = 2; }
        }
#else
        printf("SET 비활성 — '%s' 는 -DA5K_DEBUG_API 빌드에서만 동작 (배포본 제외)\n", cmd);
        ret = 3;
#endif
    }

    usleep(200000);
    g_ipc = 0;
    printf("disconnect = %d\n", ipc->disconnect());
    return ret;
}
