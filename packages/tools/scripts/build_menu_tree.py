"""viewUnified2.so의 메인 메뉴(ViewSettingMenu) 트리 재구성.

입력: trace_ctor.py 출력(Node/Properties 생성자 호출 인자 목록)
  Node ctor : this | children** | count | props*
  Props ctor: this | _settingProperty* list | count | -

자식 배열의 원소는 (a) 이 라이브러리의 다른 Node(.bss, R_ARM_RELATIVE)
또는 (b) CautionConfig.so의 cmnViewSettingNodeRoot* (import, R_ARM_ABS32)

사용법: python -I scripts/build_menu_tree.py <viewUnified2.so> <trace.txt> <english.tsv> <korean.tsv> [root_hex...]
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


def num(s):
    s = s.strip()
    return int(s, 16) if s.startswith('0x') else s


def main():
    lib = Lib(sys.argv[1])
    en, ko = load(sys.argv[3]), load(sys.argv[4])
    nodes, props = {}, {}
    for line in open(sys.argv[2]):
        _, rest = line.split(' ', 1)
        f = [x.strip() for x in rest.split('|')]
        if 'SettingNodeC1' in f[0]:
            nodes[num(f[1])] = (num(f[2]), num(f[3]), num(f[4]))
        elif 'PropertiesC1' in f[0]:
            props[num(f[1])] = (num(f[2]), num(f[3]))

    def rs(v):
        if (v >> 24) == 2 and (v & 0xffffff) in en:
            e = en[v & 0xffffff]
            k = ko.get(v & 0xffffff)
            return e + (' / ' + k if k and k != e else '')
        return None

    def prop_text(p):
        if p not in props or not isinstance(props[p][0], int):
            return '', {}
        lst, n = props[p]
        kv = {}
        for i in range(n):
            pid, typ, v = struct.unpack('<III', lib.read(lst + i * 12, 12))
            kv[pid] = (typ, v)
        title = rs(kv[0][1]) if 0 in kv and kv[0][0] == 0 else None
        extra = ' '.join('%d=%s' % (k, ('%08x' % v if t == 0 else v)) for k, (t, v) in sorted(kv.items()) if k != 0)
        return (title or ''), extra

    def children(arr, n):
        out = []
        if not isinstance(arr, int) or not isinstance(n, int):
            return out
        for i in range(n):
            a = arr + i * 4
            r = lib.relocs.get(a)
            if r and r[0] == 'sym':
                out.append(('ext', r[1]))
            elif r and r[0] == 'rel':
                out.append(('loc', r[1]))
            else:
                out.append(('raw', lib.read32(a)))
        return out

    referenced = set()
    for (arr, n, _p) in nodes.values():
        for k, v in children(arr, n):
            if k == 'loc':
                referenced.add(v)

    def walk(addr, depth, seen):
        arr, n, p = nodes[addr]
        n = n if isinstance(n, int) else 0
        title, extra = prop_text(p)
        print('%s[%x] %s  (%d items) {%s}' % ('    ' * depth, addr, title or '?', n, extra))
        if addr in seen:
            return
        seen.add(addr)
        for k, v in children(arr, n):
            if k == 'loc' and v in nodes:
                walk(v, depth + 1, seen)
            elif k == 'ext':
                print('%s- %s' % ('    ' * (depth + 1), v.replace('cmnViewSettingNodeRoot', '')))
            else:
                print('%s- ?%s %x' % ('    ' * (depth + 1), k, v))

    if len(sys.argv) > 5:
        roots = [int(x, 16) for x in sys.argv[5:]]
    else:
        roots = [a for a in nodes if a not in referenced and isinstance(nodes[a][1], int) and nodes[a][1] > 0]
    for r in roots:
        print('=' * 60, '%x' % r)
        walk(r, 0, set())


if __name__ == '__main__':
    main()
