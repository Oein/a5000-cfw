"""ARM 공유 라이브러리에서 심볼/재배치를 해석해 데이터 테이블(포인터 배열, 구조체)을 덤프하는 도구.

사용법:
  python -I scripts/elfsym.py <lib.so> dump <symbol>     # 심볼 내용을 4바이트 단위로 해석
  python -I scripts/elfsym.py <lib.so> tree <symbol> [depth]
"""
import struct
import sys

from elftools.elf.elffile import ELFFile
from elftools.elf.relocation import RelocationSection


class Lib:
    def __init__(self, path):
        self.f = open(path, 'rb')
        self.elf = ELFFile(self.f)
        self.syms = {}      # name -> (addr, size)
        self.byaddr = {}    # addr -> name
        dynsym = self.elf.get_section_by_name('.dynsym')
        for s in dynsym.iter_symbols():
            if s['st_shndx'] != 'SHN_UNDEF' and s.name:
                self.syms[s.name] = (s['st_value'], s['st_size'])
                self.byaddr.setdefault(s['st_value'], s.name)
        # 재배치: 주소 -> ('rel', target_addr) 또는 ('sym', name)
        self.relocs = {}
        symlist = list(dynsym.iter_symbols())
        for sec in self.elf.iter_sections():
            if not isinstance(sec, RelocationSection):
                continue
            for r in sec.iter_relocations():
                t = r['r_info_type']
                off = r['r_offset']
                if t == 23:  # R_ARM_RELATIVE
                    self.relocs[off] = ('rel', self.read32(off))
                elif t in (2, 21):  # R_ARM_ABS32, R_ARM_GLOB_DAT
                    self.relocs[off] = ('sym', symlist[r['r_info_sym']].name)

    def vaddr_to_off(self, addr):
        for seg in self.elf.iter_segments():
            if seg['p_type'] == 'PT_LOAD' and seg['p_vaddr'] <= addr < seg['p_vaddr'] + seg['p_filesz']:
                return addr - seg['p_vaddr'] + seg['p_offset']
        return None

    def read(self, addr, n):
        off = self.vaddr_to_off(addr)
        if off is None:
            return b'\0' * n
        self.f.seek(off)
        return self.f.read(n)

    def read32(self, addr):
        return struct.unpack('<I', self.read(addr, 4))[0]

    def word(self, addr):
        """재배치를 고려해 한 워드를 사람이 읽을 수 있게 해석"""
        r = self.relocs.get(addr)
        if r and r[0] == 'sym':
            return r[1]
        v = self.read32(addr)
        if r and r[0] == 'rel':
            name = self.byaddr.get(v)
            return name or ('&0x%x' % v)
        return v

    def cstr(self, addr, maxlen=128):
        b = self.read(addr, maxlen)
        return b.split(b'\0')[0].decode('latin1')

    def dump(self, name):
        addr, size = self.syms[name]
        print('%s @0x%x size=%d' % (name, addr, size))
        for i in range(0, size, 4):
            print('  +%02x: %s' % (i, self.word(addr + i)))


if __name__ == '__main__':
    lib = Lib(sys.argv[1])
    cmd = sys.argv[2]
    if cmd == 'dump':
        for n in sys.argv[3:]:
            lib.dump(n)
