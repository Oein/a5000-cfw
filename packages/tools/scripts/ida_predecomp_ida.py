# IDA headless (runs INSIDE idat): full export of one binary in a single pass.
# env PRE_OUT = output path prefix (no extension). Writes:
#   <PRE_OUT>.c            Hex-Rays pseudocode of every non-thunk function
#   <PRE_OUT>.funcs.tsv    addr \t size \t name \t demangled
#   <PRE_OUT>.names.tsv    addr \t segment \t name \t demangled
#   <PRE_OUT>.strings.tsv  addr \t first-xref \t string
#   <PRE_OUT>.ok           written last = export complete
# Used by ida_predecomp.py (idle-time pre-decompile queue). Never executes the target.
import os, idaapi, idc, ida_auto, ida_hexrays, ida_funcs, ida_name, idautils, ida_segment, ida_xref

ida_auto.auto_wait()
pre = os.environ['PRE_OUT']

def dem(n):
    d = idc.demangle_name(n, idc.get_inf_attr(idc.INF_SHORT_DEMNAMES)) if n else None
    return d or ''

with open(pre + '.funcs.tsv', 'w', encoding='utf-8') as o:
    for ea in idautils.Functions():
        f = ida_funcs.get_func(ea); n = idc.get_func_name(ea)
        o.write('%08x\t%x\t%s\t%s\n' % (ea, f.end_ea - f.start_ea, n, dem(n)))

with open(pre + '.names.tsv', 'w', encoding='utf-8') as o:
    for ea, n in idautils.Names():
        s = ida_segment.getseg(ea)
        o.write('%08x\t%s\t%s\t%s\n' % (ea, ida_segment.get_segm_name(s) if s else '', n, dem(n)))

with open(pre + '.strings.tsv', 'w', encoding='utf-8') as o:
    for s in idautils.Strings():
        x = ida_xref.get_first_dref_to(s.ea)
        txt = str(s).replace('\\', '\\\\').replace('\t', '\\t').replace('\n', '\\n').replace('\r', '\\r')
        o.write('%08x\t%x\t%s\n' % (s.ea, x if x != idaapi.BADADDR else 0, txt))

ok = ida_hexrays.init_hexrays_plugin()
nf = nfail = 0
with open(pre + '.c', 'w', encoding='utf-8') as o:
    for ea in idautils.Functions():
        f = ida_funcs.get_func(ea)
        if f.flags & ida_funcs.FUNC_THUNK:
            continue
        nf += 1
        o.write('// ==== %s @ %x\n' % (idc.get_func_name(ea), ea))
        if not ok:
            o.write('// no hexrays\n'); continue
        try:
            o.write(str(ida_hexrays.decompile(ea)))
        except Exception as e:
            nfail += 1
            o.write('// fail %s\n' % e)
        o.write('\n')

with open(pre + '.ok', 'w') as o:
    o.write('funcs=%d fail=%d hexrays=%d\n' % (nf, nfail, int(bool(ok))))
idc.qexit(0)
