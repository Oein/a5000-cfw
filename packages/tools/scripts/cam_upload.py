"""로컬 파일을 telnet 셸의 `echo -ne`(\\xHH 이스케이프, 카메라 셸엔 printf 없음)로 카메라 SD 카드에 업로드하고 md5로 검증.

사용법: python -I scripts/cam_upload.py <local_file> <remote_path(8.3 이름, SD 아래)>
예)    python -I scripts/cam_upload.py native/probe/PROBE /android/mnt/sdcard/A5KDUMP/PROBE
"""
import hashlib
import os
import socket
import sys
import time

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from cam import strip_iac  # noqa: E402

CHUNK = 220  # ash 줄 편집 한계(1024자) 이하: 220B × 4글자(\xHH) + 여유


def main():
    src, dst = sys.argv[1], sys.argv[2]
    if not dst.startswith('/android/mnt/sdcard/'):
        raise SystemExit('안전을 위해 SD 카드 경로만 허용')
    data = open(src, 'rb').read()
    host = os.environ.get('CAM_HOST', '192.168.10.181')
    s = socket.create_connection((host, 23), timeout=10)
    s.settimeout(0.2)

    def drain():
        buf = b''
        while True:
            try:
                d = s.recv(65536)
                if not d:
                    break
                buf += strip_iac(s, d)
            except socket.timeout:
                break
        return buf

    time.sleep(1)
    drain()
    s.sendall(b'stty -echo 2>/dev/null; : > ' + dst.encode() + b'\n')
    time.sleep(0.5)
    drain()
    # pty 입력 버퍼(4KB) 넘침으로 문자가 버려지지 않도록 줄마다 완료 마커를 기다림
    for n, i in enumerate(range(0, len(data), CHUNK)):
        part = data[i:i + CHUNK]
        esc = ''.join('\\x%02x' % b for b in part)
        mk = '__C%d__' % n
        s.sendall(("echo -ne '%s' >> %s; echo %s\n" % (esc, dst, mk)).encode())
        got = b''
        t0 = time.time()
        while (mk + '\r\n').encode() not in got and (mk + '\n').encode() not in got:
            got += drain()
            if b'\n> ' in got or time.time() - t0 > 20:
                s.sendall(b'\x03\n')
                raise SystemExit('chunk %d failed: %r' % (n, got[-200:]))
    time.sleep(1)
    drain()
    s.sendall(('busybox md5sum %s; echo __UPDONE__\n' % dst).encode())
    out = b''
    t0 = time.time()
    while time.time() - t0 < 30 and b'__UPDONE__\r\n' not in out and b'__UPDONE__\n' not in out:
        out += drain()
    s.sendall(b'exit\n')
    s.close()
    remote = out.decode('latin1')
    local = hashlib.md5(data).hexdigest()
    print('local  md5', local)
    ok = local in remote
    print('remote md5 match:', ok)
    if not ok:
        print(remote[-500:])
        sys.exit(1)


if __name__ == '__main__':
    main()
