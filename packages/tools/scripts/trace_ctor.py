"""정적 초기화 함수를 Thumb-2 미니 에뮬레이션으로 따라가며 생성자 호출 인자를 복원.

지원: LDR(literal), ADD Rd,PC, LDR Rd,[Rn,Rm] / [Rn,#imm] (GOT 읽기 → 재배치 해석),
      MOV/MOVS/MOVW/MOVT imm, MOV Rd,Rm, ADD/ADDS/SUB imm, ADD.W Rd,Rn,#imm, BL/BLX
호출 대상 이름은 PLT/thunk 주소 → 심볼 매핑(IDA funcs.tsv)으로 판별.

사용법: python -I scripts/trace_ctor.py <lib.so> <ida funcs.tsv> <func_start_hex> <func_size> [<funcname_substr>...]
출력: 호출 하나당 한 줄: callee | r0 | r1 | r2 | r3
"""
import struct
import sys

from capstone import CS_ARCH_ARM, CS_MODE_THUMB, Cs
from capstone.arm import ARM_OP_IMM, ARM_OP_MEM, ARM_OP_REG

sys.path.insert(0, __file__.rsplit('/', 1)[0])
from elfsym import Lib  # noqa: E402


class Sym:
    """심볼 값(GOT에서 읽은 import 심볼 등)"""
    def __init__(self, name, off=0):
        self.name, self.off = name, off

    def __add__(self, o):
        return Sym(self.name, self.off + o) if isinstance(o, int) else None

    def __repr__(self):
        return self.name + ('+0x%x' % self.off if self.off else '')


def main():
    lib = Lib(sys.argv[1])
    fnames = {}
    funcs = []
    for line in open(sys.argv[2]):
        p = line.rstrip('\n').split('\t')
        fnames[int(p[0], 16)] = p[2]
        funcs.append((int(p[0], 16), int(p[1])))
    filt = sys.argv[5:]
    if sys.argv[3] == 'all':
        for st, sz in funcs:
            trace(lib, fnames, st, sz, filt)
    else:
        trace(lib, fnames, int(sys.argv[3], 16), int(sys.argv[4]), filt)


def trace(lib, fnames, start, size, filt):
    md = Cs(CS_ARCH_ARM, CS_MODE_THUMB)
    md.detail = True
    code = lib.read(start, size)
    regs = {}

    def rname(r):
        return md.reg_name(r)

    def val(name):
        return regs.get(name)

    def mem_read(addr):
        if isinstance(addr, int):
            r = lib.relocs.get(addr)
            if r:
                if r[0] == 'sym':
                    return Sym(r[1])
                t = r[1]
                return Sym(lib.byaddr[t]) if t in lib.byaddr else t
            return lib.read32(addr)
        return None

    def show(v):
        if isinstance(v, int):
            n = lib.byaddr.get(v)
            return n if n else '0x%x' % v
        return repr(v)

    def stream():
        # 리터럴 풀 등 해석 불가 바이트에서 멈추지 않도록 2바이트씩 건너뛰며 계속
        off = 0
        while off < len(code):
            last = None
            for ins in md.disasm(code[off:], start + off):
                last = ins
                yield ins
            off = (last.address + last.size - start) if last else off + 2
            if last is None or last.address + last.size - start >= len(code):
                if last is None:
                    continue
                break

    for ins in stream():
        ops = ins.operands
        m = ins.mnemonic.split('.')[0]
        try:
            if m == 'ldr' and len(ops) == 2 and ops[1].type == ARM_OP_MEM:
                mem = ops[1].mem
                dst = rname(ops[0].reg)
                if rname(mem.base) == 'pc':
                    lit = ((ins.address + 4) & ~3) + mem.disp
                    regs[dst] = lib.read32(lit)
                else:
                    b = val(rname(mem.base))
                    idx = val(rname(mem.index)) if mem.index else mem.disp
                    regs[dst] = mem_read(b + idx) if isinstance(b, int) and isinstance(idx, int) else None
            elif m == 'add' and len(ops) == 2 and ops[1].type == ARM_OP_REG and rname(ops[1].reg) == 'pc':
                d = rname(ops[0].reg)
                v = val(d)
                regs[d] = v + ins.address + 4 if isinstance(v, int) else None
            elif m in ('add', 'adds', 'sub', 'subs') and len(ops) == 3 and ops[2].type == ARM_OP_IMM:
                v = val(rname(ops[1].reg))
                imm = ops[2].imm if m.startswith('add') else -ops[2].imm
                regs[rname(ops[0].reg)] = (v + imm) if v is not None else None
            elif m in ('add', 'adds') and len(ops) == 2 and ops[1].type == ARM_OP_IMM:
                v = val(rname(ops[0].reg))
                regs[rname(ops[0].reg)] = (v + ops[1].imm) if v is not None else None
            elif m in ('mov', 'movs', 'movw') and len(ops) == 2:
                if ops[1].type == ARM_OP_IMM:
                    regs[rname(ops[0].reg)] = ops[1].imm & 0xffffffff
                else:
                    regs[rname(ops[0].reg)] = val(rname(ops[1].reg))
            elif m == 'movt':
                v = val(rname(ops[0].reg)) or 0
                regs[rname(ops[0].reg)] = (v & 0xffff) | (ops[1].imm << 16)
            elif m in ('bl', 'blx') and ops[0].type == ARM_OP_IMM:
                tgt = ops[0].imm
                name = fnames.get(tgt, '0x%x' % tgt)
                if not filt or any(f in name for f in filt):
                    print('%x %s | %s' % (ins.address, name, ' | '.join(show(val(r)) for r in ('r0', 'r1', 'r2', 'r3'))))
                for r in ('r0', 'r1', 'r2', 'r3', 'r12', 'lr'):
                    regs.pop(r, None)
            elif ops and ops[0].type == ARM_OP_REG and m not in ('cmp', 'cmn', 'tst', 'str', 'strb', 'strh', 'push', 'pop', 'b', 'cbz', 'cbnz', 'it'):
                regs.pop(rname(ops[0].reg), None)
        except Exception:
            if ops and ops[0].type == ARM_OP_REG:
                regs.pop(rname(ops[0].reg), None)


if __name__ == '__main__':
    main()
