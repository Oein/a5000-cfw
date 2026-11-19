#!/usr/bin/env python3
"""gen_apply.py -- settings APPLY conversion table (the "R1 table") for the 140 menu items.

  python3 -I uicore/tools/gen_apply.py --menu uicore/src/model/menu_gen.cpp \
      --bridge uicore/src/ui/screens/menu_cascade.cpp --tsv app/docs/settings_sdcom_map.tsv \
      --out-cpp uicore/src/model/apply_gen.cpp --out-tsv app/docs/settings_apply_table.tsv

Per menu item -> {method, routes, value encoding}. ONLY existing RE data is used:
  * SDCom paramId + A-side value  <- app/docs/settings_sdcom_map.tsv  (SETTINGS_SDCOM_MAP.md)
  * Backup propIds (recorded, never written here) <- re/static/bkid_whitelist.md "실기 확정 매핑"
  * in-process engine route <- TRACKB_INPROCESS.md §18 (image aspect, CmnViewProcessDataMgr vtable:
      16:9 = 1, 4:3 = 2, 1:1 = 3 observed (emulator OSD); 3:2 has NO observed internal value -> unmapped for that route)
Anything else is AM_UNMAPPED -- a param id is never guessed. Rows the TSV itself flags "[?]" are
treated as unmapped (candidate id kept in the note).
"""
import argparse, re, sys

T = {0: "ENUM", 1: "BOOL", 2: "RANGE", 3: "TODO", 4: "INFO", 5: "EXP", 6: "ACTION", 7: "PREF"}
UNM = "APPLY_UNMAPPED_VAL"

# Backup propIds confirmed on the device (re/static/bkid_whitelist.md). (key,label) -> (propId, note).
BACKUP = {
    ("beep", "오디오 신호"): (0x01070026, "M_BEEP; on/off family 01<->03 (value encoding not fixed) -- recorded only"),
    ("info-na-aflight", "AF 보조광"): (0x0107005c, "off=1 / auto=0 -- recorded only"),
    ("info-na-peaking", "피킹 레벨"): (0x01070352, "low=2 / high=0 -- recorded only"),
    ("info-na-peaking", "피킹 색상"): (0x01070360, "yellow<->white 2<->0 -- recorded only"),
    ("info-na-zebra", "제브라 패턴"): (0x010701b8, "enable 0/1 (+0x010701b9 level 70..100+=0..7); live apply = VDF 0x59000E, no SDCom -- recorded only"),
}
# In-process engine route via the global CmnViewProcessDataMgr singleton
# (TRACKB_INPROCESS.md §18/§23): key -> (InprocKind, group, {raw_option: internal value}).
# The stock Wrapper setters all drive this one object: write vtable[14](obj,group,internal),
# read-back vtable[3](obj,group,&out). The group numbers were recovered from the setXxx
# bodies (viewUnified2) and the round-trips verified in the emulator (grpscan/grpscan2).
# Per-option internal values come from the getter decode fns (sub_21FA82 etc., internal->
# Sony WRAPPER enum) anchored by the boot-default OSD; only options proven this way are
# listed (others stay unmapped for this route, never guessed -- the SDCom route still carries them).
INPROC = {
    "image-aspect":        ("IK_ASPECT", 1,  {"16:9": 1}),                       # §18 (3:2 internal not observed)
    "storage-fmt":         ("IK_GROUP",  2,  {"raw": 0, "rawjpeg": 1, "jpeg": 2}),   # boot RAW+J = internal 1
    "metering-mode":       ("IK_GROUP",  22, {"multi": 2, "center-weighted": 1, "center-spot": 4}),  # boot multi = internal 2
    "face-detection-mode": ("IK_GROUP",  31, {"off": 0, "auto": 1}),
}
INPROC_NOTE = {
    "image-aspect":        "inproc CmnViewProcessDataMgr group 1 (TRACKB §18): 16:9=1 [V], 4:3=2 [V], 1:1=3 [V] (OSD-observed); 3:2 internal value [?] unmapped",
    "storage-fmt":         "inproc CmnViewProcessDataMgr group 2 (TRACKB §23): raw=0 rawjpeg=1 jpeg=2 (decode sub_21FA82 + boot OSD RAW+J=1); round-trip [V] (grpscan)",
    "metering-mode":       "inproc CmnViewProcessDataMgr group 22 (TRACKB §23): multi=2 center-weighted=1 center-spot=4 (decode sub_2219DE + boot multi=2); round-trip [V] in P (grpscan2)",
    "face-detection-mode": "inproc CmnViewProcessDataMgr group 31 (TRACKB §23): off=0 auto=1; round-trip [V] in P (grpscan2)",
}

ALIAS = {"on": "true", "off": "false", "true": "on", "false": "off", "midium": "medium", "medium": "midium"}
UNCERTAIN = re.compile(r"\[\? ?(msg )?id|virtual key|inferred")
MOVIE_MSG = {1792: 0x300a}   # MovieSetParameters (not 0x1003)

def parse_menu(path):
    src = open(path, encoding="utf-8").read()
    opts = {}
    for m in re.finditer(r'const MenuOpt (kO\d+)\[\] = \{(.*?)\};', src, re.S):
        opts[m.group(1)] = re.findall(r'\{ "((?:[^"\\]|\\.)*)", "((?:[^"\\]|\\.)*)" \}', m.group(2))
    tbl = src[src.index("kMenuItems[140]"):]
    items = []
    for m in re.finditer(r'^    \{ "([^"]*)", "([^"]*)", (\d+), (\d+), (\d+), (kO\d+|0), (-?\d+), (-?\d+), "([^"]*)" \},', tbl, re.M):
        k, lab, ty, fl, n, o, rmin, rmax, d = m.groups()
        items.append(dict(key=k, label=lab, type=T[int(ty)], flags=int(fl), n=int(n), opts=opts.get(o, []),
                          rmin=int(rmin), rmax=int(rmax)))
    return items

def parse_bridge(path):
    src = open(path, encoding="utf-8").read()
    blk = src[src.index("kBridge[]"):]
    blk = blk[:blk.index("};")]
    return dict(re.findall(r'\{ "([^"]+)", (SET_\w+) \}', blk))

def load_tsv(path):
    rows = {}
    with open(path, encoding="utf-8") as f:
        f.readline()
        for line in f:
            c = line.rstrip("\n").split("\t")
            if len(c) >= 8: rows.setdefault(c[0], []).append(c)
    return rows

def find_val(rows, raw):
    for r in rows:
        if r[2].lower() == raw.lower() or r[2].lower() == ALIAS.get(raw.lower(), "\0"):
            return r
        if ('app pref "%s"' % raw) in r[7]:
            return r
    return None

def main():
    ap = argparse.ArgumentParser()
    for a in ("menu", "bridge", "tsv", "out-cpp", "out-tsv"): ap.add_argument("--" + a, required=True)
    a = ap.parse_args()
    items = parse_menu(a.menu); bridge = parse_bridge(a.bridge); tsv = load_tsv(a.tsv)
    if len(items) != 140: sys.exit("expected 140 items, parsed %d" % len(items))
    out = []   # per item dicts
    arrays = []
    for i, it in enumerate(items):
        k, ty = it["key"], it["type"]
        e = dict(i=i, key=k, label=it["label"], type=ty, method="AM_UNMAPPED", routes=[], enc="AE_NONE", inproc="IK_NONE", grp=0,
                 cat=bridge.get(k, "0"), param=0, msg=0x1003, backup=0, svals=None, ivals=None, note="", covered="", nopt=it["n"])
        rows = tsv.get(k, [])
        bk = BACKUP.get((k, it["label"]))
        if bk:
            e["backup"] = bk[0]; e["routes"].append("AR_BACKUP"); e["method"] = "AM_BACKUP"; e["note"] = bk[1]
        elif ty == "ACTION":
            e["method"] = "AM_ACTION"; e["note"] = "gated action / playback / quick slot (not a value apply)"
        elif ty == "INFO":
            if k.startswith("info-na-"):
                e["note"] = "native-only (HDMI/USB/region/rec-folder/...): no mapping data -> [unmapped]"
            else:
                e["method"] = "AM_LOCAL"; e["note"] = "read-only info display"
        else:
            # value-bearing item: PREF / ENUM / BOOL / RANGE / EXP / TODO
            pr = [r for r in rows if r[3] not in ("-", "")]
            ids = sorted(set(r[3] for r in pr))
            flagged = any(UNCERTAIN.search(r[7]) for r in pr)
            if ty == "PREF" and not pr:
                e["method"] = "AM_LOCAL"; e["note"] = "app-only preference (no SDCom path)"
            elif not pr:
                e["note"] = "no SDCom/engine mapping in the TSV -> [unmapped]"
                if ty == "EXP": e["note"] = "EXP item without a documented route (SETTINGS_SDCOM_MAP.md 6.2 / didep.Settings) -> [unmapped]"
            elif flagged:
                e["note"] = "TSV marks the id [?] (inferred, unverified): candidate %s -> [unmapped]" % ",".join(ids)
            elif len(ids) != 1 or not ids[0].isdigit():
                e["note"] = "compound/virtual param ids (%s), no per-value table -> [unmapped]" % ",".join(ids)
            else:
                pid = int(ids[0]); e["param"] = pid; e["msg"] = MOVIE_MSG.get(pid, 0x1003)
                introw = [r for r in pr if r[4].startswith("<int")]
                if introw:
                    e["enc"] = "AE_INT"; e["covered"] = "%d/%d" % (max(it["n"], 1), max(it["n"], 1))
                    e["routes"].append("AR_SDCOM"); e["method"] = "AM_SDCOM"
                    e["note"] = "int param %d (%s)" % (pid, introw[0][7][:60])
                elif it["n"] == 0:
                    e["note"] = "TSV param %d but the HAL has no settable key / no picker values (SETTINGS_SDCOM_MAP.md 6.1) -> [unmapped]" % pid
                    e["param"] = 0
                else:
                    vals = []; ok = 0
                    for raw, lab in it["opts"]:
                        r = find_val(rows, raw)
                        if r and r[4] not in ("-", "") and r[4].lstrip("-").isdigit():
                            vals.append(int(r[4])); ok += 1
                        else: vals.append(None)
                    if ok:
                        e["enc"] = "AE_OPT"; e["svals"] = vals; e["covered"] = "%d/%d" % (ok, len(vals))
                        e["routes"].append("AR_SDCOM"); e["method"] = "AM_SDCOM"
                        e["note"] = "SDCom msg 0x%x param %d, A-side value from TSV" % (e["msg"], pid)
                    else:
                        e["note"] = "TSV param %d but no option matches a TSV value row -> [unmapped]" % pid; e["param"] = 0
        # in-process route overlay
        if k in INPROC:
            kind, grp, m = INPROC[k]
            e["inproc"] = kind; e["grp"] = grp; e["routes"].insert(0, "AR_INPROC"); e["method"] = "AM_INPROC"
            e["ivals"] = [m.get(raw) for raw, lab in it["opts"]]
            e["note"] = INPROC_NOTE[k] + ("; also " + e["note"] if e["note"] else "")
            e["covered"] = "inproc %d/%d" % (sum(v is not None for v in e["ivals"]), len(e["ivals"])) + ("; sdcom " + e["covered"] if e["covered"] else "")
        out.append(e)

    def arr(name, vals):
        return "const int %s[] = { %s };\n" % (name, ", ".join(UNM if v is None else str(v) for v in vals))
    with open(a.out_cpp, "w") as c:
        c.write("// Generated by uicore/tools/gen_apply.py (menu_gen.cpp + settings_sdcom_map.tsv + documented RE maps) -- do not edit.\n")
        c.write('#include "uicore/apply.h"\n#include "uicore/catalog_gen.h"\n\nnamespace uicore {\nnamespace {\n')
        for e in out:
            if e["svals"] is not None: c.write(arr("kS%d" % e["i"], e["svals"]))
            if e["ivals"] is not None: c.write(arr("kI%d" % e["i"], e["ivals"]))
        c.write("} // namespace\n\nconst ApplyEntry kApply[140] = {\n")
        for e in out:
            rt = " | ".join(e["routes"]) or "0"
            c.write('    /*%3d*/ { "%s", %s, %s, %s, %s, %d, %s, %d, 0x%x, 0x%08x, %s, %s, "%s" },\n' % (
                e["i"], e["key"], e["method"], rt, e["enc"], e["inproc"], e["grp"], e["cat"], e["param"], e["msg"] if e["param"] else 0, e["backup"],
                "kS%d" % e["i"] if e["svals"] is not None else "0", "kI%d" % e["i"] if e["ivals"] is not None else "0",
                e["note"].replace('"', "'")))
        c.write("};\n\n} // namespace uicore\n")
    with open(a.out_tsv, "w", encoding="utf-8") as t:
        t.write("# Generated by uicore/tools/gen_apply.py -- settings apply conversion table (140 menu items, index == kMenuItems)\n")
        t.write("idx\tkey\tlabel\ttype\tmethod\troutes\tsdcom_param\tbackup_prop\tcatalog_id\tcoverage\tnote\n")
        for e in out:
            t.write("%d\t%s\t%s\t%s\t%s\t%s\t%s\t%s\t%s\t%s\t%s\n" % (
                e["i"], e["key"], e["label"], e["type"], e["method"][3:].lower(), "+".join(r[3:].lower() for r in e["routes"]) or "-",
                e["param"] or "-", ("0x%08x" % e["backup"]) if e["backup"] else "-", e["cat"] if e["cat"] != "0" else "-",
                e["covered"] or "-", e["note"]))
    from collections import Counter
    cnt = Counter(e["method"][3:].lower() for e in out)
    print("methods:", dict(cnt))

if __name__ == "__main__":
    main()
