"""string_<lang>.uxc 파서: 리소스 ID(u32) -> UTF-8 문자열

레코드 형식: [u32 id][UTF-8 문자열 \\0][4바이트 정렬 패딩]. 레코드 영역은 첫 레코드(id 하위 16bit == 0)부터 EOF까지.
사용법: python -I scripts/uxc_strings.py string_english.uxc > out.tsv
"""
import struct
import sys


def find_start(d):
    # 첫 레코드: 이어지는 레코드 3개의 id가 1씩 증가하는 첫 위치
    def nxt(off):
        end = d.find(b'\0', off + 4)
        return -1 if end < 0 else (end + 1 + 3) & ~3

    for off in range(0x10, len(d) - 8, 4):
        i0 = struct.unpack_from('<I', d, off)[0]
        o1 = nxt(off)
        if o1 < 0 or o1 + 4 > len(d) or struct.unpack_from('<I', d, o1)[0] != i0 + 1:
            continue
        o2 = nxt(o1)
        if 0 < o2 and o2 + 4 <= len(d) and struct.unpack_from('<I', d, o2)[0] == i0 + 2:
            return off
    raise ValueError('record start not found')


def parse(d):
    off = find_start(d)
    out = []
    while off + 4 <= len(d):
        rid = struct.unpack_from('<I', d, off)[0]
        end = d.find(b'\0', off + 4)
        if end < 0:
            break
        out.append((rid, d[off + 4:end].decode('utf-8', 'replace')))
        off = (end + 1 + 3) & ~3
    return out


if __name__ == '__main__':
    data = open(sys.argv[1], 'rb').read()
    for rid, s in parse(data):
        print('%08x\t%s' % (rid, s.replace('\n', '\\n')))
