"""Extract FirmwareData_*.dat (magic 89 55 46 55 ...) embedded uncompressed in newer Sony updater exes."""
import struct, sys
MAGIC = b'\x89\x55\x46\x55\x0d\x0a\x1a\x0a'
src, dst = sys.argv[1], sys.argv[2]
data = open(src, 'rb').read()
start = data.find(MAGIC)
assert start >= 0, 'dat magic not found'
off = start + 8
while True:
    size, typ = struct.unpack('>I4s', data[off:off+8])
    print(f'{typ.decode()} @0x{off:x} size=0x{size:x}')
    off += 8 + size
    if typ == b'DEND':
        break
open(dst, 'wb').write(data[start:off])
print(f'wrote {off-start} bytes from 0x{start:x}')
