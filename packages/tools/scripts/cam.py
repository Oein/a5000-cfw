"""카메라 telnet(busybox telnetd, 무인증 root 셸)에 명령을 보내고 출력을 받는 최소 클라이언트.

사용법: python -I scripts/cam.py [-t timeout] '<shell command>'
환경변수 CAM_HOST (기본 192.168.10.181), CAM_PORT (기본 23)
출력 끝을 알기 위해 명령 뒤에 고유 마커를 echo 한다.
"""
import os
import socket
import sys
import time
import uuid

IAC, DONT, DO, WONT, WILL, SB, SE = 255, 254, 253, 252, 251, 250, 240


def strip_iac(sock, data):
    """telnet 협상: 모든 옵션 거절, 데이터만 반환"""
    out = bytearray()
    i = 0
    while i < len(data):
        b = data[i]
        if b == IAC and i + 1 < len(data):
            cmd = data[i + 1]
            if cmd in (DO, DONT, WILL, WONT) and i + 2 < len(data):
                opt = data[i + 2]
                if cmd == DO:
                    sock.sendall(bytes([IAC, WONT, opt]))
                elif cmd == WILL:
                    sock.sendall(bytes([IAC, DONT, opt]))
                i += 3
                continue
            if cmd == SB:
                j = data.find(bytes([IAC, SE]), i)
                i = len(data) if j < 0 else j + 2
                continue
            i += 2
            continue
        out.append(b)
        i += 1
    return bytes(out)


def run(cmd, timeout=60):
    host = os.environ.get('CAM_HOST', '192.168.10.181')
    port = int(os.environ.get('CAM_PORT', '23'))
    s = socket.create_connection((host, port), timeout=10)
    s.settimeout(1.0)
    marker = '__END_%s__' % uuid.uuid4().hex[:8]
    buf = b''
    t0 = time.time()
    # 프롬프트 대기(짧게)
    while time.time() - t0 < 3:
        try:
            d = s.recv(4096)
            if not d:
                break
            buf += strip_iac(s, d)
        except socket.timeout:
            break
    s.sendall(('%s; echo %s $?\n' % (cmd, marker)).encode())
    buf = b''
    t0 = time.time()
    while time.time() - t0 < timeout:
        try:
            d = s.recv(65536)
            if not d:
                break
            buf += strip_iac(s, d)
            text = buf.decode('utf-8', 'replace')
            # 에코된 명령줄 이후, 마커 출력 줄이 나오면 종료
            idx = text.rfind(marker + ' ')
            if idx >= 0 and text.find('\n', idx) >= 0 and text.count(marker) >= 2:
                break
        except socket.timeout:
            continue
    try:
        s.sendall(b'exit\n')  # 원격 셸을 남기지 않도록 종료
        time.sleep(0.3)
    except OSError:
        pass
    s.close()
    text = buf.decode('utf-8', 'replace').replace('\r', '')
    lines = text.split('\n')
    # 첫 줄(명령 에코) 제거, 마커 줄에서 종료 코드 추출
    out, code = [], None
    for ln in lines[1:]:
        if ln.startswith(marker):
            code = ln.split()[-1]
            break
        out.append(ln)
    return '\n'.join(out), code


if __name__ == '__main__':
    args = sys.argv[1:]
    to = 60
    if args and args[0] == '-t':
        to = int(args[1])
        args = args[2:]
    o, c = run(' '.join(args), to)
    print(o)
    print('[exit=%s]' % c)
