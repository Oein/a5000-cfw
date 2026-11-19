"""CmnViewMenuItemStruct 트리 덤프.

CmnViewMenuItemStruct (40B):
  +0x00 subItems*   (재배치 포인터)
  +0x04 u8 countAll
  +0x08 int id
  +0x0c resid title     (0x02xxxxxx 문자열 / -1)
  +0x10 resid icon/sub
  +0x14 resid
  +0x18 resid
  +0x1c u8 ?  +0x1d u8 execType  +0x1e u16 ?
  +0x20 int optionNum
  +0x24 const char* optionString

사용법: python -I scripts/dump_menu.py <lib.so> <english.tsv> <korean.tsv> <root_addr_hex>...
"""
import struct
import sys

sys.path.insert(0, __file__.rsplit('/', 1)[0])
from elfsym import Lib  # noqa: E402


def load(path):
    d = {}
    for line in open(path, encoding='utf-8'):
        k, _, v = line.rstrip('\n').partition('\t')
        d[int(k, 16) & 0xffffff] = v.replace('\\n', ' ')
    return d


def main():
    lib = Lib(sys.argv[1])
    en, ko = load(sys.argv[2]), load(sys.argv[3])

    def rs(v):
        if v == 0xffffffff:
            return '-'
        if (v >> 24) == 2 and (v & 0xffffff) in en:
            k = ko.get(v & 0xffffff, '')
            return '"%s"%s' % (en[v & 0xffffff], (' (%s)' % k) if k and k != en[v & 0xffffff] else '')
        return '%08x' % v

    def ptr(addr):
        r = lib.relocs.get(addr)
        if r and r[0] == 'rel':
            return r[1]
        return None

    def item(a):
        sub = ptr(a)
        cnt = lib.read(a + 4, 1)[0]
        iid, r0, r1, r2, r3 = struct.unpack('<i4I', lib.read(a + 8, 20))
        b1c, ex, w1e = struct.unpack('<BBH', lib.read(a + 0x1c, 4))
        opt = struct.unpack('<i', lib.read(a + 0x20, 4))[0]
        sp = ptr(a + 0x24)
        s = lib.cstr(sp) if sp else ''
        return sub, cnt, iid, (r0, r1, r2, r3), b1c, ex, w1e, opt, s

    seen = set()

    def walk(a, depth):
        if a in seen:
            print('  ' * depth + '(loop @%x)' % a)
            return
        seen.add(a)
        sub, cnt, iid, rr, b1c, ex, w1e, opt, s = item(a)
        print('%s[id=%d exec=%d f=%d/%d opt=%d%s] %s | %s' % (
            '  ' * depth, iid, ex, b1c, w1e, opt, (' str=%s' % s) if s else '',
            rs(rr[0]), ' '.join(rs(x) for x in rr[1:] if x != 0xffffffff)))
        if sub and cnt:
            for i in range(cnt):
                walk(sub + i * 0x28, depth + 1)

    for root in sys.argv[4:]:
        a = int(root, 16)
        print('==== root @0x%x' % a)
        walk(a, 0)


if __name__ == '__main__':
    main()
