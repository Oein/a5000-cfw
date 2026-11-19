// ScalarDaemon 조회 전용 프로브: 연결 → 지정한 메시지 ID들을 파라미터 없이 sendSync → 응답 출력 → 해제
// 사용법: sdprobe <componentId> <msgId>[:pid=val[,pid=val]] | s:<ms> ...   (예: 0x101a:5=1 s:2000 0x1002)
// 안전: 조회(Get*) 메시지 ID만 넘길 것. 설정 변경 명령은 보내지 않는다.
#include <cstdio>
#include <cstdlib>
#include <csignal>
#include <unistd.h>
#include "sdcom.h"

// 정적 크기 검증: 라이브러리 객체 크기와 다르면 컴파일 실패 (10.9 사고 재발 방지)
typedef char check_intparam_size[(sizeof(SDComMsgIntParam) == 0x10) ? 1 : -1];

static SDComIPC *g_ipc = 0;
static void onFatal(int sig) {
    // 비정상 종료 시에도 반드시 연결 해제 (osal 큐를 연 채 죽으면 카메라가 멈춤)
    if (g_ipc) { g_ipc->disconnect(); g_ipc = 0; }
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

static void printParams(SDComMsg *m) {
    int n = SDComMsgUtil::getNumOfParams(m);
    printf("  params=%d\n", n);
    for (int i = 0; i < n; i++) {
        SDComMsgParam *p = (i == 0) ? SDComMsgUtil::getFirstParam(m, true) : SDComMsgUtil::getNextParam(m, true);
        if (!p) break;
        int type = p->getType();
        printf("  #%d id=%u type=%d len=%d", i, p->getId(), type, p->getDataLength());
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
        if (getenv("SDDUMP")) p->dump(4);
        delete p;
    }
}

int main(int argc, char **argv) {
    if (argc < 3) { printf("usage: sdprobe <componentId> <msgId>...\n"); return 2; }
    setvbuf(stdout, NULL, _IOLBF, 0);
    unsigned char comp = (unsigned char)strtoul(argv[1], NULL, 0);
    Listener listener;
    SDComIPC *ipc = new SDComIPC(&listener);
    if (getenv("SD_DRYRUN")) {             // 연결 없이 파라미터 생성/해제만 검증
        SDComMsgIntParam *ip = new SDComMsgIntParam(5, 1);
        printf("dryrun: id=%u type=%d data=%d size=%u\n", ip->getId(), ip->getType(), ip->getData(), (unsigned)sizeof(*ip));
        delete ip;
        printf("dryrun ok\n");
        return 0;
    }
    signal(SIGSEGV, onFatal); signal(SIGABRT, onFatal); signal(SIGBUS, onFatal);
    signal(SIGTERM, onFatal); signal(SIGINT, onFatal); signal(SIGHUP, onFatal); signal(SIGPIPE, SIG_IGN);
    int rc = ipc->connect(SCALARDAEMON_QUEUE, comp);
    if (rc == 0) g_ipc = ipc;
    printf("connect(0x%x, %u) = %d\n", SCALARDAEMON_QUEUE, comp, rc);
    if (rc != 0) return 1;
    for (int a = 2; a < argc; a++) {
        if (argv[a][0] == 's' && argv[a][1] == ':') {   // s:<ms> 대기
            int ms = atoi(argv[a] + 2);
            printf("sleep %d ms\n", ms);
            usleep(ms * 1000);
            continue;
        }
        char *rest = NULL;
        unsigned short id = (unsigned short)strtoul(argv[a], &rest, 0);
        unsigned int tx = 0;
        ipc->createNewTransactionId(&tx);
        SDComMsg *msg = SDComMsgUtil::createNewMessage(SCALARDAEMON_QUEUE, SDCOM_MSG_REQUEST, tx);
        SDComMsgUtil::setMessageId(msg, id);
        if (rest && *rest == ':') {           // Int 파라미터 부착: pid=val,pid=val
            char *q = rest + 1;
            while (*q) {
                unsigned short pid = (unsigned short)strtoul(q, &q, 0);
                if (*q != '=') break;
                int val = (int)strtol(q + 1, &q, 0);
                SDComMsgIntParam *ip = new SDComMsgIntParam(pid, val);
                printf("  + param id=%u int=%d -> addParam=%d\n", pid, val, SDComMsgUtil::addParam(msg, ip));
                delete ip;                     // addParam은 복사본 보관 (SDComIPC::connect와 동일 패턴)
                if (*q == ',') q++;
            }
        }
        SDComMsg *rep = NULL;
        rc = ipc->sendSync(msg, &rep, 5);
        printf("msg 0x%04x tx=%u -> sendSync=%d\n", id, tx, rc);
        if (rc == 0 && rep) {
            unsigned short rid = 0;
            SDComMsgUtil::getMessageId(rep, &rid);
            printf("  reply msgId=0x%04x\n", rid);
            printParams(rep);
            SDComMsgUtil::freeMessage(rep, false);
        }
        SDComMsgUtil::freeMessage(msg, false);
    }
    usleep(200000);
    g_ipc = 0;
    printf("disconnect = %d\n", ipc->disconnect());
    return 0;
}
