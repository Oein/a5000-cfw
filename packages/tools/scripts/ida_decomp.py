# IDA headless: decompile functions named in env DECOMP_FUNCS (comma sep) -> DECOMP_OUT
import os, idaapi, idc, ida_hexrays, ida_auto, ida_name
ida_auto.auto_wait()
out = open(os.environ['DECOMP_OUT'], 'w', encoding='utf-8')
ok = ida_hexrays.init_hexrays_plugin()
for name in os.environ['DECOMP_FUNCS'].split(','):
    ea = ida_name.get_name_ea(idaapi.BADADDR, name)
    out.write('// ==== %s @ %x\n' % (name, ea))
    if ea == idaapi.BADADDR: out.write('// not found\n'); continue
    try:
        out.write(str(ida_hexrays.decompile(ea)) if ok else '// no hexrays\n')
    except Exception as e:
        out.write('// fail %s\n' % e)
    out.write('\n')
out.close()
idc.qexit(0)
