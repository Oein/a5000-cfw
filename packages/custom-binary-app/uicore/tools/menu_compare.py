#!/usr/bin/env python3
"""menu_compare.py -- drive the host simulator through the menu scenarios, screenshot, and compare with
the Track A goldens (test/golden/menu__*.png, 853x480).

  <venv>/bin/python3 -I tools/menu_compare.py --host app/native/platform/host/build \
        --golden app/native/uicore/test/golden --out app/native/uicore/test/menu [--only name,...]

Per scenario: native_<name>.png (640x480 as the panel), cmp_<name>.png = [golden | native(stretched to
853) | abs-diff x3], and a one-line score (mean abs diff per channel, % of pixels differing > 40).
Needs Pillow + numpy (see tools/README.md). Native shots are stretched 640 -> 853 only for comparison.
"""
import argparse, os, subprocess, sys, tempfile, time
import numpy as np
from PIL import Image

UP, DOWN, LEFT, RIGHT, OK, MENU = ord('w'), ord('s'), ord('a'), ord('d'), 13, ord('m')
def rep(k, n): return [k] * n

SCEN = [
    ("cascade__root",        "menu__cascade__root",        [MENU]),
    ("cascade__cat_camera",  "menu__cascade__cat_camera",  [MENU, RIGHT] + rep(DOWN, 5) + [RIGHT] + rep(DOWN, 10) + [LEFT, LEFT]),
    ("cascade__cat_custom",  "menu__cascade__cat_custom",  [MENU] + rep(DOWN, 1)),
    ("cascade__cat_wireless", "menu__cascade__cat_wireless", [MENU] + rep(DOWN, 2)),
    ("cascade__cat_application", "menu__cascade__cat_application", [MENU] + rep(DOWN, 3)),
    ("cascade__cat_playback", "menu__cascade__cat_playback", [MENU] + rep(DOWN, 4)),
    ("cascade__cat_setup",   "menu__cascade__cat_setup",   [MENU] + rep(DOWN, 5)),
    ("cascade__cat_mymenu",  "menu__cascade__cat_mymenu",  [MENU] + rep(DOWN, 6)),
    ("cascade__group_focus", "menu__cascade__group_focus", [MENU, RIGHT]),
    ("cascade__item_focus",  "menu__cascade__item_focus",  [MENU, RIGHT, RIGHT]),
    ("cascade__expo_group",  "menu__cascade__expo_group",  [MENU, RIGHT] + rep(DOWN, 5)),
    ("picker_enum",          "menu__picker_enum",          [MENU, RIGHT, RIGHT, OK]),
    ("picker_range",         "menu__picker_range",         [MENU, RIGHT] + rep(DOWN, 5) + [RIGHT, OK]),
    ("item_exp_bool",        "menu__item_exp_bool",        [MENU, RIGHT] + rep(DOWN, 5) + [RIGHT] + rep(DOWN, 6)),
    ("item_bool",            "menu__item_bool",            [MENU, RIGHT] + rep(DOWN, 5) + [RIGHT] + rep(DOWN, 9)),
    ("picker_bool",          "menu__picker_bool",          [MENU, RIGHT] + rep(DOWN, 5) + [RIGHT] + rep(DOWN, 10) + [OK]),
]

def ppm_to_img(path):
    with open(path, "rb") as f:
        assert f.readline().strip() == b"P6"
        w, h = map(int, f.readline().split()); f.readline()
        return Image.frombytes("RGB", (w, h), f.read())

def run(sock, args, host):
    return subprocess.run([os.path.join(host, "a5kctl"), "--sock", sock] + [str(a) for a in args],
                          stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL, timeout=10)

def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--host", required=True); ap.add_argument("--golden", required=True)
    ap.add_argument("--out", required=True); ap.add_argument("--only", default="")
    a = ap.parse_args()
    os.makedirs(a.out, exist_ok=True)
    only = [x for x in a.only.split(",") if x]
    tmp = tempfile.mkdtemp(prefix="a5kmenu")
    for name, gold, keys in SCEN:
        if only and name not in only: continue
        sock = os.path.join(tmp, "s.sock"); shot = os.path.join(tmp, "s.ppm")
        p = subprocess.Popen([os.path.join(a.host, "a5kui_host"), "--sock", sock, "--shot", shot, "--seconds", "20"],
                             stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
        for _ in range(50):
            if os.path.exists(sock): break
            time.sleep(0.1)
        time.sleep(0.6)                       # let the fake camera connect + first frames
        for k in keys:
            run(sock, ["key", k], a.host); time.sleep(0.12)
        time.sleep(0.4)
        run(sock, ["shot", shot], a.host); time.sleep(0.4)
        run(sock, ["quit"], a.host); p.wait(timeout=10)
        nat = ppm_to_img(shot)
        nat.save(os.path.join(a.out, "native_%s.png" % name))
        st = nat.resize((853, 480), Image.BILINEAR)
        g = Image.open(os.path.join(a.golden, gold + ".png")).convert("RGB")
        ga, na = np.array(g).astype(int), np.array(st).astype(int)
        d = np.abs(ga - na)
        score = d.mean(); bad = (d.max(2) > 40).mean() * 100
        diff = Image.fromarray(np.clip(d * 3, 0, 255).astype(np.uint8))
        out = Image.new("RGB", (853 * 3 + 8, 480), (40, 40, 40))
        out.paste(g, (0, 0)); out.paste(st, (857, 0)); out.paste(diff, (1714, 0))
        out.save(os.path.join(a.out, "cmp_%s.png" % name))
        print("%-26s mean|d|=%.2f  px>40: %.2f%%" % (name, score, bad))

if __name__ == "__main__":
    main()
