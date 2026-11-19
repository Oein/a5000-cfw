"""a5000 메인 메뉴(ViewSettingMenu)의 패치 지점을 JSON으로 출력.

메뉴 = Node(root) -> Node(tab) -> Node(page) -> CautionConfig 설정 노드(import 심볼)
각 Node는 정적 초기화 함수(sub_152964)에서
    CmnViewSettingNode::CmnViewSettingNode(this, children**, count, props*)
로 생성된다. count는 호출 직전의 Thumb `MOVS r2,#imm8`(0x22nn) 즉시값,
페이지 배열 원소는 .rel.dyn의 R_ARM_ABS32 엔트리(심볼 인덱스 = 설정 노드)이다.

사용법: python -I scripts/menu_patchpoints.py <viewUnified2.so> <trace.txt> <root_hex> > layout.json
"""
import json
import struct
import sys

from capstone import CS_ARCH_ARM, CS_MODE_THUMB, Cs
from elftools.elf.relocation import RelocationSection

sys.path.insert(0, __file__.rsplit('/', 1)[0])
from elfsym import Lib  # noqa: E402


def main():
    path = sys.argv[1]
    lib = Lib(path)
    md = Cs(CS_ARCH_ARM, CS_MODE_THUMB)

    nodes = {}
    for line in open(sys.argv[2]):
        a, rest = line.split(' ', 1)
        f = [x.strip() for x in rest.split('|')]
        if 'SettingNodeC1' in f[0]:
            nodes[int(f[1], 16)] = (int(a, 16), int(f[2], 16) if f[2].startswith('0x') else 0,
                                    int(f[3], 16) if f[3].startswith('0x') else 0)

    # .rel.dyn 엔트리 파일 오프셋 맵 (r_offset -> (file_off, sym_index, type))
    dynsym = lib.elf.get_section_by_name('.dynsym')
    rel_at = {}
    for sec in lib.elf.iter_sections():
        if isinstance(sec, RelocationSection) and sec.name == '.rel.dyn':
            base = sec['sh_offset']
            for i, r in enumerate(sec.iter_relocations()):
                rel_at[r['r_offset']] = (base + i * 8, r['r_info_sym'], r['r_info_type'])

    def count_insn(call_addr):
        """호출 직전 16개 명령 안에서 r2 즉시값을 설정하는 MOVS 위치"""
        code = lib.read(call_addr - 48, 48)
        hit = None
        for ins in md.disasm(code, call_addr - 48):
            if ins.mnemonic in ('movs', 'mov') and ins.op_str.startswith('r2, #'):
                hit = ins
        if hit and hit.size == 2:
            return {'vaddr': hit.address, 'file_off': lib.vaddr_to_off(hit.address),
                    'imm': int(hit.op_str.split('#')[1], 0), 'encoding': 'thumb16 movs r2,#imm8'}
        return None

    def node(addr, depth):
        call, arr, n = nodes[addr]
        d = {'node': '0x%x' % addr, 'ctor_call': '0x%x' % call, 'count': n,
             'count_patch': count_insn(call), 'children_array': '0x%x' % arr, 'children': []}
        for i in range(n):
            slot = arr + i * 4
            r = lib.relocs.get(slot)
            ent = rel_at.get(slot)
            if r and r[0] == 'rel' and r[1] in nodes and depth < 2:
                d['children'].append(node(r[1], depth + 1))
            elif r and r[0] == 'sym':
                d['children'].append({'setting': r[1].replace('cmnViewSettingNodeRoot', ''),
                                      'slot_vaddr': '0x%x' % slot,
                                      'rel_file_off': ent[0] if ent else None,
                                      'sym_index': ent[1] if ent else None})
            else:
                d['children'].append({'unknown': str(r)})
        return d

    root = int(sys.argv[3], 16)
    out = node(root, 0)
    # 이 라이브러리가 import한 설정 노드 심볼(교체 가능한 후보) 목록
    out['available_settings'] = sorted({
        s.name.replace('cmnViewSettingNodeRoot', ''): i
        for i, s in enumerate(dynsym.iter_symbols())
        if s.name.startswith('cmnViewSettingNodeRoot')}.items())
    json.dump(out, sys.stdout, indent=1, ensure_ascii=False)


if __name__ == '__main__':
    main()
