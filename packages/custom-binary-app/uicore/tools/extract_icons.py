#!/usr/bin/env python3
"""extract_icons.py -- framework-res.apk 아이콘 -> RGBA 아틀라스(.bin) + gen/icons.h

Track A Java 앱이 이름으로 그리는 소니 framework-res drawable 만 골라
하나의 RGBA8888 아틀라스로 묶는다.  펌웨어 파일은 신뢰불가 데이터이므로
반드시 `python3 -I` 로 실행하고 경로는 인자로 넘긴다.

  python3 -I extract_icons.py \
      --apk  <.../nflasha16_unpacked_unpacked/framework/framework-res.apk> \
      --java <app/camera-ui/app/src/main/java/dev/oein/ilceremote> \
      --out  <app/native/uicore/build/assets> \
      --gen  <app/native/uicore/gen>

이름 수집 규칙 (Java 소스에서 주석 제외한 문자열 리터럴만):
  * 리터럴 자체가 drawable 이름이면 포함.
  * `"s_16_..._" + x (+ "_normal")` 같은 문자열 연결을 정적으로 풀기 위해,
    drawable 이름 D 가  P + M [+ S]  로 분해되면 포함한다.
      P = '_' 로 끝나는 리소스 접두 리터럴 (s_16_/p_16_ 로 시작)
      M = 임의 리터럴 1개, 또는 리터럴 2개 연결(첫째가 '_' 로 끝남)
      S = '_' 로 시작하는 리터럴 (예: "_normal") 또는 없음
  * --extra 파일(한 줄에 이름 하나)로 수동 추가 가능.

밀도 선택 우선순위: drawable-nodpi > drawable-mdpi > drawable > drawable-normal
  > 그 외 drawable-* (land/en 등 한정자 붙은 것은 마지막).  기기 = mdpi(밀도 1.0).
.9.png 는 9-patch 테두리 1px 를 잘라 내고 기록(늘이기 정보는 버림, 보고서에 표시).
XML drawable(selector 등)만 있는 이름은 '미발견(xml)' 으로 보고.

출력 포맷 icons.bin (리틀엔디언) -- 자세한 설명은 tools/README.md:
  struct Header {            // 32 bytes
    char  magic[4];          // "UIIA"
    u16   version;           // 1
    u16   format;            // 0 = raw RGBA8888 (R,G,B,A byte order, straight alpha)
                             // 1 = RLE (아래)
    u16   atlas_w, atlas_h;
    u32   count;
    u32   names_off;         // 파일 시작 기준 문자열 풀 오프셋
    u32   pixels_off;        // 파일 시작 기준 픽셀 데이터 오프셋 (4바이트 정렬)
    u32   pixels_size;       // 픽셀 데이터 바이트 수 (raw = w*h*4)
    u32   checksum;          // FNV-1a32(엔트리 테이블) -- gen/icons.h 와 일치 확인용
  };
  struct Entry { u16 x, y, w, h; u32 name_off; } [count];   // 12 bytes, id 순서
  names: NUL 종료 문자열들 (name_off 는 names_off 기준)
  pixels: raw 또는 RLE.  RLE = 행 구분 없이 아틀라스 전체를 픽셀(u32) 단위로:
     ctrl u8; ctrl&0x80 -> (ctrl&0x7f)+1 개 반복, 뒤에 RGBA 4바이트 1개
              else      -> ctrl+1 개 리터럴 RGBA 가 뒤따름
"""
import argparse
import io
import os
import re
import struct
import sys
import zipfile

try:
    from PIL import Image
except ImportError:  # pragma: no cover
    sys.stderr.write("Pillow 필요: python3 -m venv <dir> && <dir>/bin/pip install pillow "
                     "(tools/README.md 참고)\n")
    sys.exit(2)

DENSITY_ORDER = ["drawable-nodpi", "drawable-mdpi", "drawable", "drawable-normal"]
RES_PREFIX = re.compile(r"^[sp]_16_[a-z0-9_]*_$")
NAME_RE = re.compile(r"^[a-z0-9_]+$")


# ---------------------------------------------------------------- Java 스캔
def java_literals(src):
    """주석을 건너뛰고 문자열 리터럴(이스케이프 해제)만 돌려준다."""
    out = []
    i, n = 0, len(src)
    while i < n:
        c = src[i]
        if c == "/" and i + 1 < n and src[i + 1] == "/":
            j = src.find("\n", i)
            i = n if j < 0 else j
        elif c == "/" and i + 1 < n and src[i + 1] == "*":
            j = src.find("*/", i + 2)
            i = n if j < 0 else j + 2
        elif c == "'":
            j = i + 1
            while j < n and src[j] != "'":
                j += 2 if src[j] == "\\" else 1
            i = j + 1
        elif c == '"':
            j = i + 1
            buf = []
            while j < n and src[j] != '"':
                if src[j] == "\\" and j + 1 < n:
                    e = src[j + 1]
                    if e == "u":
                        k = j + 1
                        while k < n and src[k] == "u":
                            k += 1
                        try:
                            buf.append(chr(int(src[k:k + 4], 16)))
                        except ValueError:
                            pass
                        j = k + 4
                        continue
                    buf.append({"n": "\n", "t": "\t", "r": "\r", "0": "\0"}.get(e, e))
                    j += 2
                else:
                    buf.append(src[j])
                    j += 1
            out.append("".join(buf))
            i = j + 1
        else:
            i += 1
    return out


def scan_java(java_dir):
    lits = set()
    for root, _dirs, files in os.walk(java_dir):
        for f in sorted(files):
            if f.endswith(".java"):
                with open(os.path.join(root, f), encoding="utf-8", errors="replace") as fh:
                    lits.update(java_literals(fh.read()))
    return lits


def select_names(drawables, lits):
    lits = {l for l in lits if l and NAME_RE.match(l)}
    prefixes = [l for l in lits if RES_PREFIX.match(l)]
    suffixes = [""] + [l for l in lits if l.startswith("_")]
    firsts = [l for l in lits if l.endswith("_")]
    chosen = {}
    for d in drawables:
        if d in lits:
            chosen[d] = "literal"
            continue
        for p in prefixes:
            if not d.startswith(p):
                continue
            rest = d[len(p):]
            hit = None
            for s in suffixes:
                if s and not rest.endswith(s):
                    continue
                mid = rest[:len(rest) - len(s)] if s else rest
                if mid in lits:
                    hit = (p, mid, s)
                    break
                for f in firsts:
                    if mid.startswith(f) and mid[len(f):] in lits:
                        hit = (p, f + "+" + mid[len(f):], s)
                        break
                if hit:
                    break
            if hit:
                chosen[d] = "concat %s+%s%s" % (hit[0], hit[1], ("+" + hit[2]) if hit[2] else "")
                break
    return chosen


# ---------------------------------------------------------------- APK
def index_apk(zf):
    """name -> {dir: zip path}  (drawable* 디렉터리만)"""
    idx = {}
    for zi in zf.infolist():
        parts = zi.filename.split("/")
        if len(parts) != 3 or parts[0] != "res" or not parts[1].startswith("drawable"):
            continue
        fn = parts[2]
        if fn.endswith(".9.png"):
            name = fn[:-6]
        else:
            name = fn.rsplit(".", 1)[0]
        idx.setdefault(name, {})[parts[1]] = zi.filename
    return idx


def pick_density(variants):
    def rank(item):
        d, path = item
        r = DENSITY_ORDER.index(d) if d in DENSITY_ORDER else len(DENSITY_ORDER) + 1
        return (path.endswith(".xml"), r, d)
    return sorted(variants.items(), key=rank)[0][1]


def load_png(zf, path):
    data = zf.read(path)
    if len(data) > 16 * 1024 * 1024:
        raise ValueError("too large")
    im = Image.open(io.BytesIO(data))
    im.load()
    if im.width > 2048 or im.height > 2048:
        raise ValueError("unreasonable size %dx%d" % im.size)
    im = im.convert("RGBA")
    ninepatch = path.endswith(".9.png")
    if ninepatch and im.width > 2 and im.height > 2:
        im = im.crop((1, 1, im.width - 1, im.height - 1))
    return im, ninepatch


# ---------------------------------------------------------------- 패킹
def shelf_pack(sizes, width, pad):
    """sizes: [(w,h)] -> positions [(x,y)], used height.  높이 내림차순 shelf."""
    order = sorted(range(len(sizes)), key=lambda i: (-sizes[i][1], -sizes[i][0]))
    pos = [None] * len(sizes)
    x = y = shelf_h = 0
    for i in order:
        w, h = sizes[i]
        if w > width:
            raise ValueError("item wider than atlas (%d > %d)" % (w, width))
        if x + w > width:
            x, y, shelf_h = 0, y + shelf_h + pad, 0
        pos[i] = (x, y)
        x += w + pad
        shelf_h = max(shelf_h, h)
    return pos, y + shelf_h


def choose_width(sizes, pad):
    area = sum((w + pad) * (h + pad) for w, h in sizes)
    maxw = max(w for w, _ in sizes)
    width = 64
    while width < maxw or width * width < area:
        width *= 2
    return width


def rle_encode(px):
    """px: bytes RGBA.  위 포맷의 RLE."""
    n = len(px) // 4
    pixels = [px[i * 4:i * 4 + 4] for i in range(n)]
    out = bytearray()
    i = 0
    while i < n:
        j = i + 1
        while j < n and j - i < 128 and pixels[j] == pixels[i]:
            j += 1
        if j - i >= 2:
            out.append(0x80 | (j - i - 1))
            out += pixels[i]
            i = j
            continue
        j = i + 1
        while j < n and j - i < 128 and not (j + 1 < n and pixels[j] == pixels[j + 1]):
            j += 1
        out.append(j - i - 1)
        for k in range(i, j):
            out += pixels[k]
        i = j
    return bytes(out)


def fnv1a(data):
    h = 0x811C9DC5
    for b in data:
        h = ((h ^ b) * 0x01000193) & 0xFFFFFFFF
    return h


def c_ident(name):
    return "ICON_" + re.sub(r"[^A-Za-z0-9]", "_", name).upper()


# ---------------------------------------------------------------- main
def main():
    ap = argparse.ArgumentParser(description=__doc__.split("\n")[0])
    ap.add_argument("--apk", required=True, help="framework-res.apk (nflasha16)")
    ap.add_argument("--java", required=True, help="Track A Java 소스 디렉터리")
    ap.add_argument("--out", required=True, help="출력 디렉터리 (build/assets)")
    ap.add_argument("--gen", required=True, help="헤더 출력 디렉터리 (uicore/gen)")
    ap.add_argument("--extra", action="append", default=[], help="추가 이름 목록 파일(줄당 1개)")
    ap.add_argument("--rle", action="store_true", help="픽셀을 RLE 로 저장 (기본 raw)")
    ap.add_argument("--pad", type=int, default=1, help="아이콘 사이 여백 px (기본 1)")
    ap.add_argument("--dump", help="선택된 PNG 를 이 디렉터리에 풀어 확인용으로 저장")
    a = ap.parse_args()

    lits = scan_java(a.java)
    extra = set()
    for path in a.extra:
        with open(path, encoding="utf-8") as fh:
            extra.update(l.strip() for l in fh if l.strip() and not l.startswith("#"))

    with zipfile.ZipFile(a.apk) as zf:
        idx = index_apk(zf)
        chosen = select_names(idx.keys(), lits)
        for e in extra:
            chosen.setdefault(e, "extra")
        # 리터럴/접두처럼 보이지만 APK 에 없는 이름 (보고용)
        direct_missing = sorted(l for l in lits
                                if NAME_RE.match(l) and re.match(r"^[sp]_16_", l)
                                and not l.endswith("_") and l not in idx)
        missing = sorted(e for e in extra if e not in idx)

        names = sorted(n for n in chosen if n in idx)
        images, report, xml_only = [], [], []
        for name in names:
            path = pick_density(idx[name])
            if path.endswith(".xml"):
                xml_only.append(name)
                continue
            try:
                im, nine = load_png(zf, path)
            except Exception as ex:  # untrusted input: 깨진 PNG 는 건너뜀
                report.append("  ! %s: decode failed (%s)" % (name, ex))
                continue
            images.append((name, im, path, nine))

    if not images:
        sys.stderr.write("선택된 아이콘이 없다\n")
        return 1

    sizes = [im.size for _, im, _, _ in images]
    width = choose_width(sizes, a.pad)
    pos, height = shelf_pack(sizes, width, a.pad)
    height = (height + 3) & ~3
    atlas = Image.new("RGBA", (width, height), (0, 0, 0, 0))
    for (name, im, _p, _n), (x, y) in zip(images, pos):
        atlas.paste(im, (x, y))
    raw = atlas.tobytes()  # R,G,B,A straight alpha

    pixels = rle_encode(raw) if a.rle else raw
    names_blob = bytearray()
    entries = bytearray()
    for (name, im, _p, _n), (x, y) in zip(images, pos):
        entries += struct.pack("<HHHHI", x, y, im.width, im.height, len(names_blob))
        names_blob += name.encode("ascii") + b"\0"
    checksum = fnv1a(entries)
    hdr_size = 32
    names_off = hdr_size + len(entries)
    pixels_off = (names_off + len(names_blob) + 3) & ~3
    hdr = struct.pack("<4sHHHHIIIII", b"UIIA", 1, 1 if a.rle else 0, width, height,
                      len(images), names_off, pixels_off, len(pixels), checksum)
    assert len(hdr) == hdr_size
    os.makedirs(a.out, exist_ok=True)
    os.makedirs(a.gen, exist_ok=True)
    with open(os.path.join(a.out, "icons.bin"), "wb") as fh:
        fh.write(hdr + entries + names_blob)
        fh.write(b"\0" * (pixels_off - names_off - len(names_blob)))
        fh.write(pixels)

    if a.dump:
        os.makedirs(a.dump, exist_ok=True)
        atlas.save(os.path.join(a.dump, "icons_atlas.png"))

    # gen/icons.h
    L = ["// Generated by uicore/tools/extract_icons.py -- do not edit.",
         "// Source: framework-res.apk (firmware) -- the pixels live in build/assets/icons.bin,",
         "// never in this header.  Names are Sony resource names (facts), not images.",
         "#pragma once",
         "#include <stdint.h>",
         "",
         "namespace uicore {",
         "",
         "enum { ICONS_ATLAS_W = %d, ICONS_ATLAS_H = %d, ICONS_FORMAT = %d };"
         % (width, height, 1 if a.rle else 0),
         "static const uint32_t ICONS_CHECKSUM = 0x%08xu;  // must equal icons.bin header" % checksum,
         "",
         "enum IconId {"]
    for i, (name, _im, _p, _n) in enumerate(images):
        L.append("    %s = %d," % (c_ident(name), i))
    L += ["    ICON_COUNT = %d" % len(images), "};", "",
          "struct IconRect { uint16_t x, y, w, h; };",
          "static const IconRect kIconRects[ICON_COUNT] = {"]
    for (name, im, _p, _n), (x, y) in zip(images, pos):
        L.append("    { %4d, %4d, %3d, %3d },  // %s" % (x, y, im.width, im.height, name))
    L += ["};", "", "static const char *const kIconNames[ICON_COUNT] = {"]
    for name, _im, _p, _n in images:
        L.append('    "%s",' % name)
    L += ["};", "", "} // namespace uicore", ""]
    with open(os.path.join(a.gen, "icons.h"), "w") as fh:
        fh.write("\n".join(L))

    # 보고
    print("icons: %d selected, atlas %dx%d RGBA = %d bytes in RAM, file %d bytes (%s)"
          % (len(images), width, height, width * height * 4,
             pixels_off + len(pixels), "rle" if a.rle else "raw"))
    for name, im, path, nine in images:
        print("  %-64s %3dx%-3d %s%s  [%s]" % (name, im.width, im.height, path.split("/")[1],
                                             " 9patch" if nine else "", chosen[name]))
    for line in report:
        print(line)
    if xml_only:
        print("not found (xml drawable only): " + ", ".join(xml_only))
    if missing:
        print("not found (extra list): " + ", ".join(missing))
    if direct_missing:
        print("not found (literal names absent from apk): " + ", ".join(direct_missing))
    return 0


if __name__ == "__main__":
    sys.exit(main())
