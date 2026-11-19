"""CmnViewMenuItemStruct(40B) 배열 탐색 및 메뉴 트리 덤프.

CmnViewMenuItemStruct (libObj.so CmnViewMenuData 접근자 디스어셈블 결과):
  +0x00  CmnViewMenuItemStruct* subItems   (재배치 포인터, 없으면 0)
  +0x04  u8   countAll                     (하위 항목 개수)
  +0x08  int  id
  +0x1d  u8   execType
  +0x20  int  optionNum
  +0x24  const char* optionString          (재배치 포인터)
  (나머지 필드는 아직 미확인 – 원시값으로 출력)

사용법: python -I scripts/find_menu_tables.py <lib.so> <english.tsv>
"""
import struct
import sys

sys.path.insert(0, __file__.rsplit('/', 1)[0])
from elfsym import Lib  # noqa: E402

ITEM = 0x28


def main():
    lib = Lib(sys.argv[1])
    en = {}
    for line in open(sys.argv[2], encoding='utf-8'):
        k, _, v = line.rstrip('\n').partition('\t')
        en[int(k, 16) & 0xffffff] = v

    rel = {a: t for a, (k, t) in lib.relocs.items() if k == 'rel'}

    def is_str(addr):
        b = lib.read(addr, 64)
        s = b.split(b'\0')[0]
        return len(s) >= 2 and all(32 <= c < 127 for c in s)

    # 후보: +0x24가 문자열을 가리키는 재배치인 레코드
    cands = set()
    for a, t in rel.items():
        if is_str(t):
            cands.add(a - 0x24)

    def valid(base):
        if base not in cands:
            return False
        sub = rel.get(base)
        cnt = lib.read(base + 4, 1)[0]
        if sub is None and struct.unpack('<I', lib.read(base, 4))[0] != 0:
            return False
        if sub is not None and cnt == 0:
            return False
        return True

    # 배열 묶기: 연속된 valid 레코드
    starts = sorted(b for b in cands if valid(b) and not valid(b - ITEM))
    referenced = set(rel.values())

    def fmt(base):
        sub = rel.get(base)
        cnt = lib.read(base + 4, 1)[0]
        rid = struct.unpack('<i', lib.read(base + 8, 4))[0]
        raw = struct.unpack('<5I', lib.read(base + 0xc, 20))
        ex = lib.read(base + 0x1d, 1)[0]
        opt = struct.unpack('<i', lib.read(base + 0x20, 4))[0]
        s = lib.cstr(rel[base + 0x24])
        names = []
        for v in raw:
            if (v >> 24) == 2 and (v & 0xffffff) in en:
                names.append('"%s"' % en[v & 0xffffff].replace('\\n', ' '))
        return sub, cnt, '%-34s id=%-5d exec=%d opt=%d sub=%s cnt=%d raw=%s %s' % (
            s, rid, ex, opt, ('0x%x' % sub) if sub else '-', cnt,
            ','.join('%x' % v for v in raw), ' '.join(names))

    def walk(base, n, depth, seen):
        for i in range(n):
            b = base + i * ITEM
            if b in seen:
                continue
            seen.add(b)
            sub, cnt, line = fmt(b)
            print('  ' * depth + line)
            if sub and depth < 6:
                walk(sub, cnt, depth + 1, seen)

    # 루트: 다른 곳에서 subItems로 참조되지 않는 배열
    subs = set()
    for b in cands:
        if valid(b) and rel.get(b):
            subs.add(rel[b])
    seen = set()
    for st in starts:
        if st in subs:
            continue
        n = 0
        while valid(st + n * ITEM):
            n += 1
        print('==== root array @0x%x (%d items)%s' % (st, n, ' [referenced]' if st in referenced else ''))
        walk(st, n, 1, seen)


if __name__ == '__main__':
    main()
