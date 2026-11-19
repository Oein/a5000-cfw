"""CautionConfig.so 설정 트리 덤프.

구조 (정적 분석 결과):
  CmnViewSettingNode(children**, numChildren, const CmnViewSettingProperties*)   -- 40B, .bss, 생성자에서 초기화
  CmnViewSettingProperties { const _settingProperty* list; int count; }           -- 8B, .bss
  _settingProperty { u32 propId; u32 type(0=resid,1=int,...); u32 value; }        -- 12B, .rodata

이름 규칙:
  노드        cmnViewSettingNodeRoot<X>          -> 속성 cmnViewSettingPropertyList<X>Root
              cmnViewSettingNode<X><Opt>         -> 속성 cmnViewSettingPropertyList<X><Opt>
  자식 배열   cmnViewSettingNodes<X>Default (+ cmnViewSettingNodesNumOf<X>Default)

사용법: python -I scripts/dump_settings.py <CautionConfig.so> <english.tsv> [korean.tsv] > settings.txt
"""
import struct
import sys

sys.path.insert(0, __file__.rsplit('/', 1)[0])
from elfsym import Lib  # noqa: E402


def load_strings(path):
    d = {}
    if path:
        for line in open(path, encoding='utf-8'):
            k, _, v = line.rstrip('\n').partition('\t')
            d[int(k, 16) & 0xffffff] = v
    return d


def main():
    lib = Lib(sys.argv[1])
    en = load_strings(sys.argv[2])
    ko = load_strings(sys.argv[3] if len(sys.argv) > 3 else None)

    def resid(v):
        if (v >> 24) == 2 and (v & 0xffffff) in en:
            s = en[v & 0xffffff]
            if ko.get(v & 0xffffff):
                s += ' / ' + ko[v & 0xffffff]
            return '"%s"' % s.replace('\\n', ' ')
        return '0x%08x' % v

    def props(key):
        name = 'cmnViewSettingPropertyList' + key
        if name not in lib.syms:
            return '(no props)'
        addr, size = lib.syms[name]
        parts = []
        for i in range(0, size, 12):
            pid, typ, val = struct.unpack('<III', lib.read(addr + i, 12))
            parts.append('%d=%s' % (pid, resid(val) if typ == 0 else val))
        return ' '.join(parts)

    def children(key):
        name = 'cmnViewSettingNodes%sDefault' % key
        if name not in lib.syms:
            return []
        addr, size = lib.syms[name]
        out = []
        for i in range(0, size, 4):
            w = lib.word(addr + i)
            if isinstance(w, str):
                out.append(w)
        return out

    root_addr, root_size = lib.syms['cmnViewSettingNodesRootDefault']
    for i in range(0, root_size, 4):
        node = lib.word(root_addr + i)
        if not isinstance(node, str):
            continue
        key = node[len('cmnViewSettingNodeRoot'):]
        print('%s\n    %s' % (key, props(key + 'Root')))
        for ch in children(key):
            opt = ch[len('cmnViewSettingNode'):]
            print('  - %s\n      %s' % (opt, props(opt)))
            # 2단계 자식 (예: CreativeStyle 박스)
            for gch in children(opt):
                print('      * %s: %s' % (gch[len('cmnViewSettingNode'):], props(gch[len('cmnViewSettingNode'):])))


if __name__ == '__main__':
    main()
