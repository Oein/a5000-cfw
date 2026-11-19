#!/usr/bin/env python3
"""bake_fonts.py -- 카메라 TTF -> 8bit 알파 글리프 아틀라스(.bin) + gen/fonts.h

Track A 앱이 쓰는 글꼴(SonyFonts.java)을 고정 픽셀 크기로 미리 래스터화한다.
펌웨어 파일은 신뢰불가 데이터이므로 `python3 -I` 로 실행, 경로는 인자로.

  python3 -I bake_fonts.py \
      --fonts <.../nflasha16_unpacked_unpacked/fonts> \
      --java  <app/camera-ui/app/src/main/java/dev/oein/ilceremote> \
      [--text app/camera-ui/app/src/main/res/values/strings.xml ...] \
      --out <app/native/uicore/build/assets> --gen <app/native/uicore/gen>

면(face):
  ui  = MYingHeiC-KSX1001-SJ.ttf  (SonyFonts.ui(): 메뉴/한글/기호)  기본 12,15 px
        문자: ASCII + 소스에서 쓰인 문자 + (기본) KS X 1001 한글 2350자 전부
  osd = UniversOTS-SJ.ttf         (SonyFonts.osd(): 촬영화면 숫자, bold=true 텍스트) 기본 15,16,24 px
        문자: ASCII + 소스 문자 중 글꼴에 있는 것.  없는 글자(한글 등)는 런타임에 ui 로 폴백
  di  = Sony_DI_Icons.ttf         (소니 아이콘 글꼴, PUA U+E000..)  기본 끔 (--di-sizes 로 켬)
        Track A 앱은 이 글꼴을 쓰지 않는다 -- 필요할 때만.
크기 근거: Java 의 text(sizeSp, bold) 호출 빈도 -- bold=false(ui) 12/15 가 대부분,
bold=true(osd) 15/16/24.  기기 밀도 1.0 이므로 sp == px.

가로 압축(--xscale, 기본 0.75): Track A 는 모든 텍스트에 setTextScaleX(0.75)를
건다(640x480 프레임버퍼가 16:9 LCD 로 늘어나는 것 보정).  여기서 미리 압축해 굽고
값은 헤더에 기록한다.  압축 렌더 = --ss 배 슈퍼샘플 후 BOX 축소 (힌팅 없음).
--xscale 1 --ss 1 이면 FreeType 힌팅 렌더 그대로.

출력 font_<face>_<px>.bin (리틀엔디언) -- tools/README.md 참고:
  struct Header {             // 36 bytes
    char magic[4];            // "UIFA"
    u16  version;             // 1
    u16  px;                  // em 픽셀 크기
    u16  atlas_w, atlas_h;    // A8 아틀라스
    i16  ascent;              // 베이스라인 위 (양수)
    i16  descent;             // 베이스라인 아래 (양수)
    i16  line_height;
    u16  xscale_q8;           // 256 = 1.0, 192 = 0.75
    u32  count;               // 글리프 수
    u32  glyphs_off;          // = 36
    u32  pixels_off;          // 4바이트 정렬, A8 w*h 바이트
    u32  checksum;            // FNV-1a32(글리프 테이블)
  };
  struct Glyph {              // 16 bytes, codepoint 오름차순 (이분 탐색)
    u32 cp; u16 x, y; u8 w, h; i8 left; i8 top; u16 advance_q6; u16 reserved;
  };  // 그리기: 펜(px,baseline) -> 비트맵 좌상단 (px+left, baseline-top), 펜 += adv/64
"""
import argparse
import os
import re
import struct
import sys

try:
    from PIL import Image, ImageDraw, ImageFont
except ImportError:  # pragma: no cover
    sys.stderr.write("Pillow 필요: python3 -m venv <dir> && <dir>/bin/pip install pillow "
                     "(tools/README.md 참고)\n")
    sys.exit(2)

FACES = {
    "ui": "MYingHeiC-KSX1001-SJ.ttf",
    "osd": "UniversOTS-SJ.ttf",
    "di": "Sony_DI_Icons.ttf",
}


# ---------------------------------------------------------------- TTF cmap
def ttf_cmap(path):
    """지원 코드포인트 집합 (format 4/12 만).  입력은 신뢰불가 -> 범위 검사."""
    with open(path, "rb") as fh:
        d = fh.read()
    def u16(o): return struct.unpack_from(">H", d, o)[0]
    def u32(o): return struct.unpack_from(">I", d, o)[0]
    tables = {}
    for i in range(min(u16(4), 64)):
        tag, _cs, off, ln = struct.unpack_from(">4sIII", d, 12 + 16 * i)
        tables[tag] = (off, ln)
    off = tables[b"cmap"][0]
    out = set()
    for i in range(min(u16(off + 2), 32)):
        pid, eid, so = struct.unpack_from(">HHI", d, off + 4 + 8 * i)
        so += off
        if so + 2 > len(d) or not ((pid == 3 and eid in (1, 10)) or pid == 0):
            continue
        fmt = u16(so)
        if fmt == 4:
            seg = u16(so + 6) // 2
            ends = struct.unpack_from(">%dH" % seg, d, so + 14)
            starts = struct.unpack_from(">%dH" % seg, d, so + 16 + 2 * seg)
            for s, e in zip(starts, ends):
                if s != 0xFFFF and e >= s:
                    out.update(range(s, e + 1))
        elif fmt == 12:
            for g in range(min(u32(so + 12), 100000)):
                s, e, _ = struct.unpack_from(">III", d, so + 16 + 12 * g)
                if e >= s and e - s < 0x10000:
                    out.update(range(s, e + 1))
    return out


# ---------------------------------------------------------------- 텍스트 스캔
def java_literals(src):
    """주석 제외, 이스케이프 해제한 문자열/문자 리터럴."""
    out = []
    i, n = 0, len(src)
    while i < n:
        c = src[i]
        if c == "/" and src.startswith("//", i):
            j = src.find("\n", i)
            i = n if j < 0 else j
        elif c == "/" and src.startswith("/*", i):
            j = src.find("*/", i + 2)
            i = n if j < 0 else j + 2
        elif c in "\"'":
            q, j, buf = c, i + 1, []
            while j < n and src[j] != q:
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
                    buf.append({"n": "\n", "t": "\t"}.get(e, e))
                    j += 2
                else:
                    buf.append(src[j])
                    j += 1
            out.append("".join(buf))
            i = j + 1
        else:
            i += 1
    return out


def collect_text(java_dir, text_files):
    chars = set()
    for root, _dirs, files in os.walk(java_dir):
        for f in sorted(files):
            if f.endswith(".java"):
                with open(os.path.join(root, f), encoding="utf-8", errors="replace") as fh:
                    for lit in java_literals(fh.read()):
                        chars.update(lit)
    for path in text_files:
        with open(path, encoding="utf-8", errors="replace") as fh:
            t = fh.read()
        t = re.sub(r"&#x([0-9a-fA-F]+);", lambda m: chr(int(m.group(1), 16)), t)
        t = re.sub(r"\\u([0-9a-fA-F]{4})", lambda m: chr(int(m.group(1), 16)), t)
        chars.update(t)
    return {ord(c) for c in chars if ord(c) >= 0x20 and not 0xD800 <= ord(c) <= 0xDFFF}


def ksx1001_hangul():
    out = []
    for hi in range(0xB0, 0xC9):
        for lo in range(0xA1, 0xFF):
            try:
                out.append(ord(bytes([hi, lo]).decode("euc_kr")))
            except UnicodeDecodeError:
                pass
    return set(out)


# ---------------------------------------------------------------- 래스터화
class Raster:
    def __init__(self, path, px, xscale, ss):
        self.px, self.xs = px, xscale
        self.ss = 1 if (xscale == 1.0 and ss <= 1) else max(1, ss)
        self.font = ImageFont.truetype(path, px * self.ss)
        asc, desc = self.font.getmetrics()
        self.ascent = -(-asc // self.ss)
        self.descent = -(-desc // self.ss)

    def glyph(self, ch):
        """-> (Image L w*h, left, top, adv_q6) ; 빈 글리프는 0x0."""
        s, xs, f = self.ss, self.xs, self.font
        adv = f.getlength(ch) * xs / s
        l, t, r, b = f.getbbox(ch, anchor="ls")
        if r <= l or b <= t:
            return None, 0, 0, int(round(adv * 64))
        # 최종 픽셀 격자 (가로는 xscale 적용)
        L = int((l * xs / s) // 1)
        R = -int((-(r * xs / s)) // 1)
        T = int((t / s) // 1)
        B = -int((-(b / s)) // 1)
        w, h = R - L, B - T
        if s == 1 and xs == 1.0:
            im = Image.new("L", (w, h), 0)
            ImageDraw.Draw(im).text((-L, -T), ch, font=f, fill=255, anchor="ls")
        else:
            bw = max(1, int(round(w * s / xs)))
            big = Image.new("L", (bw, h * s), 0)
            ImageDraw.Draw(big).text((-L * s / xs, -T * s), ch, font=f, fill=255, anchor="ls")
            im = big.resize((w, h), Image.BOX)
        bb = im.getbbox()
        if bb is None:
            return None, 0, 0, int(round(adv * 64))
        im = im.crop(bb)
        return im, L + bb[0], -(T + bb[1]), int(round(adv * 64))


def shelf_pack(sizes, width, pad):
    order = sorted(range(len(sizes)), key=lambda i: (-sizes[i][1], -sizes[i][0]))
    pos = [None] * len(sizes)
    x = y = sh = 0
    for i in order:
        w, h = sizes[i]
        if x + w > width:
            x, y, sh = 0, y + sh + pad, 0
        pos[i] = (x, y)
        x += w + pad
        sh = max(sh, h)
    return pos, y + sh


def fnv1a(data):
    h = 0x811C9DC5
    for b in data:
        h = ((h ^ b) * 0x01000193) & 0xFFFFFFFF
    return h


def bake(path, face, px, cps, xscale, ss, out_dir, pad):
    r = Raster(path, px, xscale, ss)
    glyphs = []
    for cp in sorted(cps):
        im, left, top, adv = r.glyph(chr(cp))
        if im is not None and (im.width > 255 or im.height > 255
                               or not -128 <= left <= 127 or not -128 <= top <= 127):
            raise ValueError("glyph U+%04X too large for format at %dpx" % (cp, px))
        glyphs.append((cp, im, left, top, adv))
    sizes = [(g[1].width, g[1].height) if g[1] else (0, 0) for g in glyphs]
    area = sum((w + pad) * (h + pad) for w, h in sizes)
    width = 128
    while width * width < area:
        width *= 2
    pos, height = shelf_pack(sizes, width, pad)
    height = max(4, (height + 3) & ~3)
    atlas = Image.new("L", (width, height), 0)
    table = bytearray()
    for (cp, im, left, top, adv), (x, y) in zip(glyphs, pos):
        w, h = (im.width, im.height) if im else (0, 0)
        if im:
            atlas.paste(im, (x, y))
        else:
            x = y = 0
        table += struct.pack("<IHHBBbbHH", cp, x, y, w, h, left, top, min(adv, 0xFFFF), 0)
    line = r.ascent + r.descent
    xs_q8 = int(round(xscale * 256))
    glyphs_off = 36
    pixels_off = (glyphs_off + len(table) + 3) & ~3
    checksum = fnv1a(table)
    hdr = struct.pack("<4sHHHHhhhHIIII", b"UIFA", 1, px, width, height, r.ascent, r.descent,
                      line, xs_q8, len(glyphs), glyphs_off, pixels_off, checksum)
    assert len(hdr) == 36
    fname = "font_%s_%d.bin" % (face, px)
    with open(os.path.join(out_dir, fname), "wb") as fh:
        fh.write(hdr + table + b"\0" * (pixels_off - glyphs_off - len(table)))
        fh.write(atlas.tobytes())
    return dict(face=face, px=px, file=fname, w=width, h=height, ascent=r.ascent,
                descent=r.descent, line=line, count=len(glyphs), checksum=checksum,
                size=pixels_off + width * height, atlas=atlas)


def parse_sizes(s):
    return [int(x) for x in s.split(",") if x.strip()] if s else []


def main():
    ap = argparse.ArgumentParser(description=__doc__.split("\n")[0])
    ap.add_argument("--fonts", required=True, help="펌웨어 /system/fonts 디렉터리 (nflasha16/fonts)")
    ap.add_argument("--java", required=True, help="Track A Java 소스 디렉터리")
    ap.add_argument("--text", action="append", default=[], help="추가 텍스트 파일 (strings.xml 등)")
    ap.add_argument("--out", required=True)
    ap.add_argument("--gen", required=True)
    ap.add_argument("--ui-sizes", default="12,15")
    ap.add_argument("--osd-sizes", default="15,16,24")
    ap.add_argument("--di-sizes", default="", help="Sony_DI_Icons 크기 (기본 없음)")
    ap.add_argument("--no-ksx1001", action="store_true",
                    help="KS X 1001 한글 2350자 전체 대신 소스에 쓰인 한글만")
    ap.add_argument("--xscale", type=float, default=0.75)
    ap.add_argument("--ss", type=int, default=4, help="압축 렌더 슈퍼샘플 배수")
    ap.add_argument("--pad", type=int, default=1)
    ap.add_argument("--dump", help="아틀라스 PNG 확인용 출력 디렉터리")
    a = ap.parse_args()
    if not 0.25 <= a.xscale <= 1.0:
        ap.error("--xscale must be in [0.25, 1]")

    used = collect_text(a.java, a.text)
    ascii_set = set(range(0x20, 0x7F))
    hangul = ksx1001_hangul()
    os.makedirs(a.out, exist_ok=True)
    os.makedirs(a.gen, exist_ok=True)

    plan = [("ui", parse_sizes(a.ui_sizes)), ("osd", parse_sizes(a.osd_sizes)),
            ("di", parse_sizes(a.di_sizes))]
    results, notes = [], []
    for face, sizes in plan:
        if not sizes:
            continue
        path = os.path.join(a.fonts, FACES[face])
        have = ttf_cmap(path)
        if face == "di":
            want = {c for c in have if c >= 0xE000}
        else:
            want = ascii_set | used
            if face == "ui" and not a.no_ksx1001:
                want |= hangul
        miss = sorted(c for c in want if c not in have and c >= 0x20)
        cps = sorted(c for c in want if c in have)
        if face == "ui":
            notes.append("ui: %d glyphs (ASCII %d, Hangul %d of which %d used by app, other %d);"
                         " missing in font: %s" % (
                             len(cps), len(ascii_set & set(cps)),
                             len([c for c in cps if 0xAC00 <= c <= 0xD7A3]),
                             len([c for c in used if 0xAC00 <= c <= 0xD7A3]),
                             len([c for c in cps if c > 0x7E and not 0xAC00 <= c <= 0xD7A3]),
                             " ".join("U+%04X" % c for c in miss) or "none"))
        elif face == "osd":
            fb = [c for c in miss if 0xAC00 <= c <= 0xD7A3]
            other = [c for c in miss if c not in fb]
            notes.append("osd: %d glyphs; %d Hangul + %d other used chars absent -> runtime fallback to ui (%s)"
                         % (len(cps), len(fb), len(other),
                            "".join(chr(c) for c in other[:60])))
        else:
            notes.append("di: %d PUA glyphs" % len(cps))
        for px in sizes:
            res = bake(path, face, px, cps, a.xscale, a.ss, a.out, a.pad)
            results.append(res)
            if a.dump:
                os.makedirs(a.dump, exist_ok=True)
                res["atlas"].save(os.path.join(a.dump, "font_%s_%d.png" % (face, px)))

    # gen/fonts.h
    L = ["// Generated by uicore/tools/bake_fonts.py -- do not edit.",
         "// Glyph bitmaps live in build/assets/font_*.bin (firmware-derived, not committed).",
         "#pragma once",
         "#include <stdint.h>",
         "",
         "namespace uicore {",
         "",
         "enum FontFace { FACE_UI = 0, FACE_OSD = 1, FACE_DI = 2 };",
         "static const int FONTS_XSCALE_Q8 = %d;  // glyphs pre-squeezed horizontally" % int(round(a.xscale * 256)),
         "",
         "enum FontId {"]
    for i, r in enumerate(results):
        L.append("    FONT_%s_%d = %d," % (r["face"].upper(), r["px"], i))
    L += ["    FONT_COUNT = %d" % len(results), "};", "",
          "struct FontInfo {",
          "    const char *file;      // relative to the assets directory",
          "    uint8_t  face;         // FontFace",
          "    uint8_t  px;",
          "    int16_t  ascent, descent, line_height;",
          "    uint16_t atlas_w, atlas_h;",
          "    uint32_t glyphs;",
          "    uint32_t checksum;     // must equal the .bin header",
          "};",
          "static const FontInfo kFonts[FONT_COUNT] = {"]
    for r in results:
        L.append('    { "%s", FACE_%s, %d, %d, %d, %d, %d, %d, %d, 0x%08xu },'
                 % (r["file"], r["face"].upper(), r["px"], r["ascent"], r["descent"], r["line"],
                    r["w"], r["h"], r["count"], r["checksum"]))
    L += ["};", "", "} // namespace uicore", ""]
    with open(os.path.join(a.gen, "fonts.h"), "w") as fh:
        fh.write("\n".join(L))

    total_ram = total_file = 0
    for r in results:
        print("%-16s %4d glyphs  atlas %4dx%-4d A8 = %7d B  file %7d B  asc %d desc %d"
              % (r["file"], r["count"], r["w"], r["h"], r["w"] * r["h"], r["size"],
                 r["ascent"], r["descent"]))
        total_ram += r["w"] * r["h"] + r["count"] * 16
        total_file += r["size"]
    print("total: %d files, %d bytes (~%.2f MB resident if all loaded)"
          % (len(results), total_file, total_ram / 1048576.0))
    for n in notes:
        print(n)
    return 0


if __name__ == "__main__":
    sys.exit(main())
