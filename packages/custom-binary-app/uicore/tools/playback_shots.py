#!/usr/bin/env python3
"""playback_shots.py -- drive the host simulator through the M3 playback scenarios (fake media), save
640x480 screenshots, and compare the empty state with the Track A golden (gallery__empty.png).

  <venv>/bin/python3 -I tools/playback_shots.py --host app/native/platform/host/build \
        --golden app/native/uicore/test/golden --out app/native/uicore/test/playback
"""
import argparse, os, subprocess, sys, tempfile, time
import numpy as np
from PIL import Image

UP, DOWN, LEFT, RIGHT, OK, MENU, PLAY, DEL = ord('w'), ord('s'), ord('a'), ord('d'), 13, ord('m'), ord('p'), ord('x')
T, W = ord('t'), ord('g')
def rep(k, n): return [k] * n

# name, extra host args, keys
SCEN = [
    ("empty",          ["--media", "0"], [PLAY]),
    ("grid",           [], [PLAY]),
    ("grid_nav",       [], [PLAY, RIGHT, RIGHT, DOWN]),
    ("grid_page2",     [], [PLAY] + rep(RIGHT, 12)),
    ("single",         [], [PLAY, OK]),
    ("single_next",    [], [PLAY, OK, RIGHT, RIGHT]),
    ("single_info",    [], [PLAY, OK, RIGHT, OK]),
    ("single_wide",    [], [PLAY] + rep(RIGHT, 11) + [OK]),        # 16:9 (screen idx 11 -> file 3)
    ("single_movie",   [], [PLAY] + rep(RIGHT, 8) + [OK]),         # file 6 = movie
    ("single_prot",    [], [PLAY] + rep(RIGHT, 10) + [OK, OK]),    # file 4 = protected
    ("delete_modal",   ["--allow-write"], [PLAY, RIGHT, DEL]),
    ("delete_done",    ["--allow-write"], [PLAY, RIGHT, DEL, LEFT, OK]),
    ("delete_blocked", [], [PLAY, DEL]),
    ("exit",           [], [PLAY, OK, UP, PLAY]),                  # back on the shooting OSD
]

def ppm_to_img(path):
    with open(path, "rb") as f:
        assert f.readline().strip() == b"P6"
        w, h = map(int, f.readline().split()); f.readline()
        return Image.frombytes("RGB", (w, h), f.read())

def ctl(sock, host, args):
    subprocess.run([os.path.join(host, "a5kctl"), "--sock", sock] + [str(a) for a in args],
                   stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL, timeout=10)

def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--host", required=True); ap.add_argument("--golden", required=True)
    ap.add_argument("--out", required=True); ap.add_argument("--only", default="")
    a = ap.parse_args()
    os.makedirs(a.out, exist_ok=True)
    only = [x for x in a.only.split(",") if x]
    tmp = tempfile.mkdtemp(prefix="a5kpb")
    for name, extra, keys in SCEN:
        if only and name not in only: continue
        sock = os.path.join(tmp, "s.sock"); shot = os.path.join(tmp, "s.ppm")
        p = subprocess.Popen([os.path.join(a.host, "a5kui_host"), "--sock", sock, "--shot", shot, "--seconds", "30"] + extra,
                             stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
        for _ in range(50):
            if os.path.exists(sock): break
            time.sleep(0.1)
        time.sleep(0.6)
        for k in keys:
            ctl(sock, a.host, ["key", k]); time.sleep(0.15)
        time.sleep(0.6)
        ctl(sock, a.host, ["shot", shot]); time.sleep(0.4)
        ctl(sock, a.host, ["quit"]); p.wait(timeout=10)
        nat = ppm_to_img(shot)
        nat.save(os.path.join(a.out, "native_%s.png" % name))
        print("saved native_%s.png" % name)
        if name == "empty":
            st = nat.resize((853, 480), Image.BILINEAR)
            g = Image.open(os.path.join(a.golden, "gallery__empty.png")).convert("RGB")
            ga, na = np.array(g).astype(int), np.array(st).astype(int)
            d = np.abs(ga - na)
            diff = Image.fromarray(np.clip(d * 3, 0, 255).astype(np.uint8))
            out = Image.new("RGB", (853 * 3 + 8, 480), (40, 40, 40))
            out.paste(g, (0, 0)); out.paste(st, (857, 0)); out.paste(diff, (1714, 0))
            out.save(os.path.join(a.out, "cmp_empty.png"))
            # text bbox comparison (bright pixels)
            def bbox(arr):
                ys, xs = np.where(arr.max(2) > 128); return xs.min(), ys.min(), xs.max(), ys.max()
            print("empty: mean|d|=%.3f  px>40: %.3f%%  golden text bbox=%s native(stretched)=%s" %
                  (d.mean(), (d.max(2) > 40).mean() * 100, bbox(ga), bbox(na)))

if __name__ == "__main__":
    main()
