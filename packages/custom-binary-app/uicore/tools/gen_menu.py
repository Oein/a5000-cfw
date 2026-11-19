#!/usr/bin/env python3
"""gen_menu.py -- Track A 메뉴 인벤토리(A5kSettings.java) -> uicore 메뉴 테이블.

  python3 -I gen_menu.py --java <.../A5kSettings.java> --tsv app/docs/settings_sdcom_map.tsv \
      --out-src uicore/src/model/menu_gen.cpp --out-hdr uicore/include/uicore/menu_gen.h

출력: 카테고리 7 / 그룹 / 항목 ~140개 (CATALOG 순서·라벨·유형 그대로 = feature parity) + 값 선택지.
선택지 출처: ENUM = settings_sdcom_map.tsv 의 a5k-supported 행(또는 <int> 행 notes 의 "values a,b,c"),
PREF = A5kSettings.APP_PREF_VALUES, EXP = EXP_VALUES/EXP_RANGE, BOOL = off/on.
RANGE 범위 = TSV notes 의 "range a..b (emu min-/max-)" (없으면 호스트 시뮬 가짜 범위, FAKE_RANGE).
라벨 = A5kSettings.VAL (key:value 또는 value), 없으면 pretty(value) (Track A valueLabel 과 동일).
기본값(DEFAULTS)은 호스트 시뮬레이터 가짜 모델 값(골든 캡처 상태와 맞춤) -- 기기 동작 아님.
"""
import argparse, csv, re, sys

TYPES = ["ENUM", "BOOL", "RANGE", "TODO", "INFO", "EXP", "ACTION", "PREF"]
ICONS = {"GEAR": 1, "WIFI": 2, "GRID": 3, "PLAY": 4, "WRENCH": 5, "CAMERA": 0, "STAR": 6}
EXP_ENABLED_RE = r'EXPERIMENTAL_ENABLED\s*=.*?\)\);'

# 호스트 시뮬 가짜 모델 기본값(Track A 골든 캡처의 에뮬레이터 상태와 일치). 기기 동작 아님.
DEFAULTS = {
    "image-size": "large", "image-aspect": "16:9", "storage-fmt": "rawjpeg", "jpeg-quality": "50",
    "exposure-compensation": "0", "iso-sensitivity": "0", "metering-mode": "multi", "dro-mode": "auto",
    "dro-level": "1", "hdr-mode": "off", "auto-shutter-low-limit": "1", "ae-lock": "unlocked",
    "long-exposure-nr": "on", "ovf_preview": "true",
}
FAKE_RANGE = {"dro-level": (1, 5), "auto-shutter-low-limit": (1, 5)}
INFO_FAKE = {
    "info-auto-rotate": "켬", "info-model": "ILCE-5000", "info-fw-version": "1.10",
    "info-ui-version": "a5k-2.2-complete",
}

def read(p): return open(p, encoding="utf-8").read()

def parse_groups(src):
    g = {}
    m = re.search(r'private static final String (G_QUAL.*?);', src, re.S)
    for name, val in re.findall(r'(\w+)\s*=\s*"([^"]*)"', m.group(1)):
        g[name] = val
    return g

def parse_val(src):
    d = {}
    for k, v in re.findall(r'\bv\("([^"]+)",\s*"([^"]*)"\)', src):
        d[k] = v
    return d

def parse_pref(src):
    pv = {}
    for k, vs in re.findall(r'APP_PREF_VALUES\.put\("([^"]+)",\s*new String\[\]\{([^}]*)\}\)', src):
        pv[k] = re.findall(r'"([^"]*)"', vs)
    pd = {}
    for k, v in re.findall(r'APP_PREF_DEFAULT\.put\("([^"]+)",\s*"([^"]*)"\)', src):
        pd[k] = v
    return pv, pd

def parse_exp(src):
    ev = {}
    for k, vs in re.findall(r'EXP_VALUES\.put\("([^"]+)",\s*new String\[\]\{([^}]*)\}\)', src):
        ev[k] = re.findall(r'"([^"]*)"', vs)
    er = {}
    for k, a, b in re.findall(r'EXP_RANGE\.put\("([^"]+)",\s*new int\[\]\{(\d+),\s*(\d+)\}\)', src):
        er[k] = (int(a), int(b))
    en = set()
    m = re.search(EXP_ENABLED_RE, src, re.S)
    if m: en = set(re.findall(r'"([^"]+)"', re.sub(r'//[^\n]*', '', m.group(0))))
    return ev, er, en

def pretty(v):
    s = v.replace('-', ' ').replace('_', ' ')
    return s[:1].upper() + s[1:]

def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--java", required=True); ap.add_argument("--tsv", required=True)
    ap.add_argument("--out-src", required=True); ap.add_argument("--out-hdr", required=True)
    a = ap.parse_args()
    src = read(a.java)
    groups = parse_groups(src)
    VAL = parse_val(src)
    pv, pd = parse_pref(src)
    ev, er, en = parse_exp(src)

    cats = re.findall(r'new Category\("([^"]+)",\s*null,\s*MenuIcons\.(\w+),\s*0x([0-9a-fA-F]{8})\)', src)
    assert len(cats) == 7, cats

    cat_body = src[src.index("static final Item[] CATALOG"):]
    cat_body = cat_body[:cat_body.index("};")]
    cat_body = re.sub(r'//[^\n]*', '', cat_body)
    items = []
    for c, g, k, l, t in re.findall(r'new Item\((\d+),\s*(\w+),\s*"([^"]+)",\s*"([^"]+)",\s*(\w+)\)', cat_body):
        items.append(dict(cat=int(c), group=groups[g], key=k, label=l, type=TYPES.index(t)))
    print("items:", len(items), file=sys.stderr)

    # TSV -> a5000 supported option lists + ranges
    tsvopts, tsvrange = {}, {}
    for r in csv.DictReader(open(a.tsv, encoding="utf-8"), delimiter="\t"):
        k, v, n = r["android_key"], r["android_value"], r["notes"]
        if v == "<int>":
            m = re.search(r'values ([\d,\-]+)', n)
            if m: tsvopts.setdefault(k, [x for x in m.group(1).split(",") if x])
            m = re.search(r'range (-?\d+)\.\.(-?\d+)', n)
            if m: tsvrange[k] = (int(m.group(1)), int(m.group(2)))
        elif n.startswith("a5k-supported"):
            lst = tsvopts.setdefault(k, [])
            if v not in lst: lst.append(v)

    def label(key, raw):
        h = VAL.get(key + ":" + raw)
        if h is not None: return h
        h = VAL.get(raw)
        return h if h is not None else pretty(raw)

    # group items by category/group in first-appearance order -> contiguous
    order = []   # (cat, group)
    for it in items:
        if (it["cat"], it["group"]) not in order: order.append((it["cat"], it["group"]))
    flat = []
    for cg in order:
        flat += [it for it in items if (it["cat"], it["group"]) == cg]
    off = 0

    strs = []
    def esc(s): return '"' + s.replace("\\", "\\\\").replace('"', '\\"') + '"'

    optdefs = []; optnames = {}
    def optarr(key, pairs):
        nm = "kOpt_%d" % len(optdefs)
        optdefs.append((nm, pairs)); return nm

    rows = []
    for it in flat:
        k, t = it["key"], TYPES[it["type"]]
        flags = 0
        if k.startswith("info-na-") or t == "TODO" or k == "sys-dbrestore": flags |= 1
        if t == "EXP" and k in en: flags |= 2
        opts, rmin, rmax = [], 0, 0
        if t == "BOOL": opts = [("off", label(k, "off")), ("on", label(k, "on"))]
        elif t == "PREF": opts = [(x, label(k, x)) for x in pv.get(k, [])]
        elif t == "EXP":
            if k in er: opts = [(str(i), str(i)) for i in range(er[k][0], er[k][1] + 1)]
            else: opts = [(x, label(k, x)) for x in ev.get(k, [])]
        elif t == "ENUM": opts = [(x, label(k, x)) for x in tsvopts.get(k, [])]
        elif t == "RANGE":
            rmin, rmax = tsvrange.get(k) or FAKE_RANGE.get(k) or (0, 0)
        dflt = DEFAULTS.get(k)
        if dflt is None:
            if t == "PREF": dflt = pd.get(k, "")
            elif t == "BOOL": dflt = "off"
            elif t == "RANGE": dflt = str(min(max(0, rmin), rmax))
            elif t == "ENUM": dflt = opts[0][0] if opts else ""
            else: dflt = ""
        if k.startswith("quick-slot-"):
            tgt = pd.get(k, "none")
            it2 = [x for x in items if x["key"] == tgt]
            dflt = it2[0]["label"] if it2 else "— 없음 —"
        if t == "INFO": dflt = INFO_FAKE.get(k, "")
        rows.append((it, flags, opts, rmin, rmax, dflt))

    hdr = '''// Generated by uicore/tools/gen_menu.py from A5kSettings.java + settings_sdcom_map.tsv -- do not edit.
// Main-menu inventory (Track A catalog, feature parity): 7 categories / groups / items.
#pragma once

namespace uicore {

enum MenuItemType { MT_ENUM = 0, MT_BOOL, MT_RANGE, MT_TODO, MT_INFO, MT_EXP, MT_ACTION, MT_PREF };
enum MenuItemFlags { MF_DISABLED = 1, MF_EXP_ON = 2 };
enum MenuIconKind { MI_CAMERA = 0, MI_GEAR, MI_WIFI, MI_GRID, MI_PLAY, MI_WRENCH, MI_STAR };

struct MenuOpt  { const char *raw; const char *label; };
struct MenuItem {
    const char *key; const char *label;
    unsigned char type, flags;
    short nOpts; const MenuOpt *opts;
    int rmin, rmax;           // MT_RANGE
    const char *def;          // host-sim default raw value (fake model; not device behaviour)
};
struct MenuGroup { const char *name; short first, count; };            // items [first, first+count)
struct MenuCat   { const char *name; unsigned color; unsigned char icon; short firstGroup, nGroups; };

extern const MenuCat   kMenuCats[%d];
extern const MenuGroup kMenuGroups[%d];
extern const MenuItem  kMenuItems[%d];
enum { MENU_CATS = %d, MENU_GROUPS = %d, MENU_ITEMS = %d };

} // namespace uicore
''' % (7, len(order), len(flat), 7, len(order), len(flat))
    open(a.out_hdr, "w", encoding="utf-8").write(hdr)

    o = ['// Generated by uicore/tools/gen_menu.py -- do not edit.', '#include "uicore/menu_gen.h"', '',
         'namespace uicore {', 'namespace {']
    # option arrays
    optname_for = {}
    for idx, (it, flags, opts, rmin, rmax, dflt) in enumerate(rows):
        if opts:
            nm = "kO%d" % idx; optname_for[idx] = nm
            o.append("const MenuOpt %s[] = {" % nm)
            for raw, lab in opts: o.append("    { %s, %s }," % (esc(raw), esc(lab)))
            o.append("};")
    o.append("}\n")
    o.append("const MenuCat kMenuCats[7] = {")
    gi = 0
    for ci, (nm, ic, col) in enumerate(cats):
        n = len([1 for cg in order if cg[0] == ci])
        o.append("    { %s, 0x%sU, %d, %d, %d }," % (esc(nm), col[2:] if len(col) == 8 else col, ICONS[ic] if False else ["CAMERA","GEAR","WIFI","GRID","PLAY","WRENCH","STAR"].index(ic), gi, n))
        gi += n
    o.append("};\n")
    o.append("const MenuGroup kMenuGroups[%d] = {" % len(order))
    pos = 0
    for cg in order:
        n = len([1 for it in flat if (it["cat"], it["group"]) == cg])
        o.append("    { %s, %d, %d }," % (esc(cg[1]), pos, n)); pos += n
    o.append("};\n")
    o.append("const MenuItem kMenuItems[%d] = {" % len(flat))
    for idx, (it, flags, opts, rmin, rmax, dflt) in enumerate(rows):
        o.append("    { %s, %s, %d, %d, %d, %s, %d, %d, %s }," % (
            esc(it["key"]), esc(it["label"]), it["type"], flags, len(opts),
            optname_for.get(idx, "0"), rmin, rmax, esc(dflt)))
    o.append("};\n\n} // namespace uicore")
    open(a.out_src, "w", encoding="utf-8").write("\n".join(o) + "\n")
    print("cats 7 groups %d items %d" % (len(order), len(flat)), file=sys.stderr)

if __name__ == "__main__":
    main()
