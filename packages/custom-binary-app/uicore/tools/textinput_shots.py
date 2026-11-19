#!/usr/bin/env python3
"""textinput_shots.py -- drive the host simulator through the text-input widget scenarios and save
640x480 PNGs (pure stdlib, no PIL).   python3 -I tools/textinput_shots.py --host app/native/platform/host/build --out app/native/uicore/test/textinput
Keys (host keymap): w/s up/down, a/d left/right (scrub), '.'/',' wheel CW/CCW, Enter confirm,
f = Fn (group), x = trash (backspace), t/g = zoom lever (caret), m = MENU (cancel)."""
import argparse, os, struct, subprocess, sys, tempfile, time, zlib

UP, DOWN, LEFT, RIGHT, OK, MENU, FN, DEL, ZT, ZW, CW, CCW = (ord(c) if isinstance(c, str) else c for c in
    ['w', 's', 'a', 'd', 13, 'm', 'f', 'x', 't', 'g', '.', ','])
def rep(k, n): return [k] * n
def typ(s):
    """keys to type a lowercase/digit string starting with the strip on 'a' (wheel-scrub from current cell)."""
    return []

# to the device-name / Wi-Fi rows: MENU, category "무선", group "무선", items
TO_WIRELESS = [MENU, DOWN, DOWN, RIGHT, RIGHT]          # focus on the item column of the wireless group
TO_DEVNAME  = TO_WIRELESS + rep(DOWN, 1)                # WPS/AP rows disabled -> skipped; next live row = device name
TO_WIFI     = TO_WIRELESS + rep(DOWN, 3)                # name, MAC, SSID/PW

SCEN = [
    ("00_menu_rows",         TO_WIRELESS, []),
    ("01_devname_open",      TO_DEVNAME + [OK], []),
    # wipe the old name (select-all + trash), auto-cap puts us on ABC: M (12 clicks), then a, then 123 -> 5, then #+= -> '-'
    ("02_devname_typing",    TO_DEVNAME + [OK, DEL] + rep(CW, 12) + [OK, OK, FN, FN] + rep(CW, 5) + [OK, FN, OK], []),
    # autocomplete: type "I" (ABC, 8 clicks) -> ghost "LCE-5000" + chip, UP+OK accepts the whole name
    ("03_devname_autocomp",  TO_DEVNAME + [OK, DEL] + rep(CW, 8) + [OK], []),
    ("04_devname_accept",    TO_DEVNAME + [OK, DEL] + rep(CW, 8) + [OK, UP, OK], []),
    # preset row: wipe, UP to the assist row, wheel to the 3rd chip
    ("05_devname_presets",   TO_DEVNAME + [OK, DEL, UP, CW, CW], []),
    ("06_devname_committed", TO_DEVNAME + [OK, DEL, UP, CW, CW, OK, CCW, OK], []),
    ("07_ssid",              TO_WIFI + [OK], []),
    # SSID: accept a preset (UP, OK), then the Done pill (one CCW from 'a'), which chains into the password editor
    ("08_psk_generated",     TO_WIFI + [OK, UP, OK, CCW, OK], []),
    ("09_psk_picked",        TO_WIFI + [OK, UP, OK, CCW, OK, UP, CW, OK], []),
    # too short: wipe, type one char, try Done -> blocked with the reason
    ("10_psk_too_short",     TO_WIFI + [OK, UP, OK, CCW, OK, DEL, FN, CW, OK, CCW, CCW, OK], []),
    # typed by wheel: "ab" then digit 3; masked except the char just typed
    ("11_psk_typed_masked",  TO_WIFI + [OK, UP, OK, CCW, OK, DEL, OK, CW, OK, FN, FN] + rep(CW, 3) + [OK], []),
    ("12_psk_shown",         TO_WIFI + [OK, UP, OK, CCW, OK, DEL, OK, CW, OK, FN, FN] + rep(CW, 3) + [OK] + rep(CCW, 5) + [OK], []),
    ("13_psk_committed",     TO_WIFI + [OK, UP, OK, CCW, OK, UP, CW, OK, CCW, OK], []),
]

def ppm_rows(path):
    with open(path, "rb") as f:
        assert f.readline().strip() == b"P6"
        w, h = map(int, f.readline().split()); f.readline()
        return w, h, f.read()

def write_png(path, w, h, rgb):
    raw = b"".join(b"\x00" + rgb[y * w * 3:(y + 1) * w * 3] for y in range(h))
    def ch(t, d): return struct.pack(">I", len(d)) + t + d + struct.pack(">I", zlib.crc32(t + d) & 0xffffffff)
    open(path, "wb").write(b"\x89PNG\r\n\x1a\n" + ch(b"IHDR", struct.pack(">IIBBBBB", w, h, 8, 2, 0, 0, 0)) +
                           ch(b"IDAT", zlib.compress(raw, 9)) + ch(b"IEND", b""))

def ctl(sock, host, args):
    subprocess.run([os.path.join(host, "a5kctl"), "--sock", sock] + [str(a) for a in args],
                   stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL, timeout=10)

def run(host, out, name, keys, shots_at):
    tmp = tempfile.mkdtemp(prefix="a5kti")
    sock = os.path.join(tmp, "s.sock"); shot = os.path.join(tmp, "s.ppm"); log = os.path.join(tmp, "l.txt")
    p = subprocess.Popen([os.path.join(host, "a5kui_host"), "--sock", sock, "--shot", shot, "--log", log, "--seconds", "60"],
                         stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
    for _ in range(50):
        if os.path.exists(sock): break
        time.sleep(0.1)
    time.sleep(0.6)
    snaps = {len(keys): name}; snaps.update(shots_at)
    def snap(label):
        ctl(sock, host, ["shot", shot]); time.sleep(0.35)
        w, h, d = ppm_rows(shot); write_png(os.path.join(out, label + ".png"), w, h, d); print("saved", label)
    for i, k in enumerate(keys):
        if i in snaps and i != len(keys): time.sleep(0.3); snap(snaps[i])
        ctl(sock, host, ["key", k]); time.sleep(0.15)
    time.sleep(0.5); snap(name)
    ctl(sock, host, ["quit"]); p.wait(timeout=10)
    try: print("".join(l for l in open(log) if "textinput" in l), end="")
    except OSError: pass

def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--host", required=True); ap.add_argument("--out", required=True); ap.add_argument("--only", default="")
    a = ap.parse_args(); os.makedirs(a.out, exist_ok=True)
    only = [x for x in a.only.split(",") if x]
    for name, keys, snaps in SCEN:
        if only and name not in only: continue
        run(a.host, a.out, name, keys, snaps)

if __name__ == "__main__":
    main()
