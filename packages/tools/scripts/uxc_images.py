"""image_*.uxc (uxd\\x07) 이미지 번들 색인/추출.

컨테이너 포맷 (getImageSize/ResourceManager RE로 확정):
  헤더: magic 'uxd\\x07'(4) · u32=0 · u16=8 · u16 moduleId · u32 packed
        슬롯 수 n = packed & 0x1fff
  인덱스표 A: 0x10부터 u16[n]. 값 = 레코드 워드오프셋(0xffff=빈 슬롯). 레코드는 20바이트(=5워드)라 인접 레코드 간 값 차이는 5.
  레코드(20B): u32 rid(0x1aXXXXXX) · u32 fmt(0xe0000000 등) · u16 w · u16 h · u32 compSize · u32 dataOff
        rec_base = 0x10 + n*2.  픽셀 데이터 베이스 data_base = rec_base + (최대 워드오프셋+5)*4 를 정렬.
        단순히 dataOff는 data_base 기준 상대. 보수적으로: data_base = 레코드영역 끝(마지막 레코드+20)을 8정렬.
  fmt 상위 니블(0xe=압축 등)은 픽셀 코덱 태그. 픽셀 압축 해제(코덱)는 별도 (uxc_decode.py).

사용법:
  python -I scripts/uxc_images.py index  <image_*.uxc> ...      # TSV: file,module,rid,fmt,w,h,compSize,fileOff
  python -I scripts/uxc_images.py blob    <uxc> <rid-hex> <out> # 압축 블롭 그대로 저장
"""
import struct
import sys


def parse(path):
    d = open(path, 'rb').read()
    magic, z, v8, mod, packed = struct.unpack_from('<4sIHHI', d, 0)
    if magic != b'uxd\x07':
        return None
    n = packed & 0x1fff
    tblA = struct.unpack_from('<%dH' % n, d, 0x10)
    rec_base = 0x10 + n * 2
    recs = []
    maxoff = 0
    for slot, wv in enumerate(tblA):
        if wv == 0xffff:
            continue
        off = rec_base + wv * 4
        rid, fmt, w, h, csize, doff = struct.unpack_from('<IIHHII', d, off)
        recs.append((slot, rid, fmt, w, h, csize, doff))
        if wv > maxoff:
            maxoff = wv
    data_base = (rec_base + (maxoff + 5) * 4 + 7) & ~7
    return d, mod, recs, data_base


def main():
    cmd = sys.argv[1]
    if cmd == 'index':
        print('file\tmodule\trid\tfmt\tw\th\tcompSize\tfileOff')
        for p in sys.argv[2:]:
            r = parse(p)
            if not r:
                continue
            d, mod, recs, data_base = r
            name = p.rsplit('/', 1)[-1]
            for slot, rid, fmt, w, h, csize, doff in recs:
                print('%s\t%02x\t%08x\t%08x\t%d\t%d\t%d\t0x%x'
                      % (name, mod, rid, fmt, w, h, csize, data_base + doff))
    elif cmd == 'blob':
        p, ridhex, out = sys.argv[2], int(sys.argv[3], 16), sys.argv[4]
        d, mod, recs, data_base = parse(p)
        for slot, rid, fmt, w, h, csize, doff in recs:
            if rid == ridhex:
                open(out, 'wb').write(d[data_base + doff:data_base + doff + csize])
                print('wrote', out, csize, 'bytes  w=%d h=%d fmt=%08x off=0x%x' % (w, h, fmt, data_base + doff))
                return
        print('rid not found')


if __name__ == '__main__':
    main()
