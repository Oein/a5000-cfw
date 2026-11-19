# IDA headless export: 함수 목록, 문자열, 데이터 심볼, 전체 Hex-Rays 디컴파일 결과를 텍스트로 저장
# 실행: idat.exe -A -o<db> -S"ida_export.py <outdir>" -L<log> <binary>
import os
import sys
import time

import ida_auto
import ida_bytes
import ida_funcs
import ida_hexrays
import ida_nalt
import ida_name
import ida_pro
import ida_segment
import idautils
import idc

outdir = idc.ARGV[1] if len(idc.ARGV) > 1 else os.path.dirname(idc.get_idb_path())
base = os.path.basename(ida_nalt.get_root_filename())
os.makedirs(outdir, exist_ok=True)


def out(suffix):
    return open(os.path.join(outdir, base + suffix), 'w', encoding='utf-8', errors='replace')


ida_auto.auto_wait()

with out('.funcs.tsv') as f:
    for ea in idautils.Functions():
        fn = ida_funcs.get_func(ea)
        f.write('%08x\t%d\t%s\t%s\n' % (ea, fn.size(), idc.get_func_name(ea),
                                        idc.demangle_name(idc.get_func_name(ea), idc.get_inf_attr(idc.INF_SHORT_DN)) or ''))

with out('.names.tsv') as f:
    for ea, name in idautils.Names():
        seg = ida_segment.getseg(ea)
        f.write('%08x\t%s\t%s\t%s\n' % (ea, ida_segment.get_segm_name(seg) if seg else '', name,
                                        idc.demangle_name(name, idc.get_inf_attr(idc.INF_SHORT_DN)) or ''))

with out('.strings.tsv') as f:
    for s in idautils.Strings():
        try:
            txt = str(s).replace('\n', '\\n').replace('\t', '\\t')
        except Exception:
            continue
        xrefs = ','.join('%x' % x.frm for x in idautils.XrefsTo(s.ea))
        f.write('%08x\t%s\t%s\n' % (s.ea, xrefs, txt))

if ida_hexrays.init_hexrays_plugin():
    t0 = time.time()
    ok = fail = 0
    with out('.c') as f:
        for ea in idautils.Functions():
            name = idc.get_func_name(ea)
            try:
                cf = ida_hexrays.decompile(ea)
                f.write('// ---- %08x %s\n%s\n\n' % (ea, name, str(cf)))
                ok += 1
            except Exception as e:
                f.write('// ---- %08x %s  DECOMPILE FAILED: %s\n\n' % (ea, name, e))
                fail += 1
    with out('.done') as f:
        f.write('ok=%d fail=%d sec=%d\n' % (ok, fail, time.time() - t0))
else:
    with out('.done') as f:
        f.write('hexrays unavailable\n')

ida_pro.qexit(0)
