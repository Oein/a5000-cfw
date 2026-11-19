// libsdcom.so (카메라 /usr/lib) 최소 선언 — 심볼/vtable 순서는 RE로 확인 (NOTES.md 8.2, 10.7)
#pragma once

class SDComMsg;  // 불투명 (operator new(0x48))

class SDComMsgParam {
public:
    // vtable: [0]D1 [1]D0 [2]operator== [3]operator!= [4]getType [5]getDataLength [6]dump
    virtual ~SDComMsgParam();
    virtual bool operator==(const SDComMsgParam &) const;
    virtual bool operator!=(const SDComMsgParam &) const;
    virtual int getType() const = 0;   // 1 = Int
    virtual int getDataLength() const;
    virtual void dump(int indent) const;
    unsigned short getId() const;
protected:
    SDComMsgParam(unsigned short id, bool flag);
private:
    char base_[8];               // +0x04..+0x0c (id u16 @+4 등) — 실제 객체 레이아웃 크기 맞춤
};

class SDComMsgIntParam : public SDComMsgParam {
public:
    SDComMsgIntParam(unsigned short id, int value);   // 객체 크기 0x10, vtable은 libsdcom 것을 사용
    virtual ~SDComMsgIntParam();
    virtual bool operator==(const SDComMsgParam &) const;
    virtual int getType() const;
    virtual void dump(int indent) const;
    int getData() const;
private:
    int data_;                   // +0x0c → sizeof == 0x10 (libsdcom: operator new(0x10))
};

class SDComMsgIntArrayParam : public SDComMsgParam {
public:
    int getArrayLength() const;
    int getDataAt(int idx, int *out) const;
};

class SDComIPCListener {
public:
    // vtable: D1 D0 onConnectionRequested onDisconnectionRequested onConnected onDisconnected onCommandRequested onEventReceived
    virtual ~SDComIPCListener();
    virtual int onConnectionRequested(unsigned int, unsigned char);
    virtual int onDisconnectionRequested(unsigned int);
    virtual void onConnected();
    virtual void onDisconnected();
    virtual void onCommandRequested(SDComMsg *);
    virtual void onEventReceived(SDComMsg *);
};

class SDComIPC {
public:
    explicit SDComIPC(SDComIPCListener *listener);   // 객체 크기 0x44 (여유 있게 패딩)
    int connect(unsigned int dstQueue, unsigned char componentId);
    int disconnect();
    int sendSync(SDComMsg *msg, SDComMsg **reply, int timeoutSec);
    int createNewTransactionId(unsigned int *txid);
private:
    char storage_[0x100];        // 실제 0x44, 넉넉히
};

class SDComMsgUtil {
public:
    static SDComMsg *createNewMessage(unsigned int dst, unsigned char type, unsigned int txid);
    static int setMessageId(SDComMsg *, unsigned short);
    static int getMessageId(SDComMsg *, unsigned short *);
    static int getNumOfParams(SDComMsg *);
    static SDComMsgParam *getFirstParam(SDComMsg *, bool copy);
    static SDComMsgParam *getNextParam(SDComMsg *, bool copy);
    static int addParam(SDComMsg *, const SDComMsgParam *);
    static int freeMessage(SDComMsg *, bool);
};

enum {
    SCALARDAEMON_QUEUE = 0x1540432,   // 데몬 수신 큐
    SDCOM_MSG_REQUEST = 5,            // Android SDComHelper::sendMsg 와 동일
};
