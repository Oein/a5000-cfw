"""firmware.tar 재생성기: 원본 tar의 멤버 순서·헤더(소유자/권한/시간/포맷)를 그대로 유지하고,
지정한 멤버의 내용만 교체한다. 교체된 멤버가 속한 *_sum 파일의 CRC32/크기도 자동 갱신한다.

사용법:
  python -I cfw/scripts/rebuild_tar.py <orig.tar> <out.tar> [member=path/to/newfile ...]
  예) 0700_part_image/dev/nflasha15=cfw/build/nflasha15.img
인자가 없으면 원본과 바이트 동일한 tar가 나와야 한다(라운드트립 검증용).
"""
import io
import sys
import tarfile
import zlib


def crc32_bytes(b):
    return zlib.crc32(b) & 0xffffffff


def main():
    src, dst = sys.argv[1], sys.argv[2]
    repl = {}
    for a in sys.argv[3:]:
        k, v = a.split('=', 1)
        repl[k] = open(v, 'rb').read()

    tin = tarfile.open(src, 'r:')
    members = tin.getmembers()
    data = {}
    for m in members:
        if m.isfile():
            data[m.name] = tin.extractfile(m).read()

    # *_sum 갱신: "<sec>_sum/<x>.sum" 안의 경로는 "<sec>" 섹션 기준 상대 경로
    for name, newb in repl.items():
        if name not in data:
            raise SystemExit('unknown member: ' + name)
        data[name] = newb
        sec, rel = name.split('/', 1)
        num, key = sec.split('_', 1)
        sum_sec = '%04d_%s_sum' % (int(num) + 1, key)
        sum_name = '%s/%s.sum' % (sum_sec, key)
        if sum_name not in data:
            raise SystemExit('sum file not found for ' + name)
        lines = data[sum_name].decode('latin1').split('\n')
        hit = False
        for i, l in enumerate(lines):
            f = l.split(',')
            if len(f) > 10 and f[10] == rel:
                f[1] = '%08x' % crc32_bytes(newb)
                f[2] = '%08x' % len(newb)
                lines[i] = ','.join(f)
                hit = True
        if not hit:
            raise SystemExit('entry %s not in %s' % (rel, sum_name))
        data[sum_name] = '\n'.join(lines).encode('latin1')

    # 원본은 레코드(10KB) 단위 패딩이 없음 → 512B 단위로 끝내도록 맞춤
    tarfile.RECORDSIZE = tarfile.BLOCKSIZE
    tout = tarfile.open(dst, 'w:', format=tin.format)
    for m in members:
        if m.isfile():
            b = data[m.name]
            m.size = len(b)
            tout.addfile(m, io.BytesIO(b))
        else:
            tout.addfile(m)
    tout.close()


if __name__ == '__main__':
    main()
