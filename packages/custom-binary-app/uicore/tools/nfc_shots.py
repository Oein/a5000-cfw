#!/usr/bin/env python3
"""nfc_shots.py -- drive the host simulator through the NFC one-touch scenarios (fake NFC + fake Wi-Fi) -> 640x480 PNGs.
   python3 -I tools/nfc_shots.py --host app/native/platform/host/build --out app/native/uicore/test/nfc
Ops: int = key, "sleep:S", "shot:name", "tap" (simulated phone tap via ctl `nfc`), "env:K=V" (before launch only).
Fake timing: tag read 0.8s, AP start 0.8s, phone joins the AP at 6s."""
import argparse, os, subprocess, sys, tempfile, time
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from textinput_shots import ppm_rows, write_png, ctl, UP, DOWN, LEFT, RIGHT, OK, MENU, FN, rep

TO_NFC = [MENU, DOWN, DOWN, RIGHT, RIGHT] + rep(DOWN, 2) + [OK]    # wireless group: 스마트폰 -> PC -> (TV disabled, skipped) -> 원터치(NFC)
S = lambda t: "sleep:%s" % t
SH = lambda n: "shot:" + n

SCEN = {
 "basic": ({}, [S(0.3)] + TO_NFC + [S(0.4), SH("01_nfc_idle_guidance"), OK, S(0.3), SH("02_nfc_off"), OK, S(0.3),
          "tap", S(0.3), SH("03_tap_detected"), S(1.0), SH("04_pairing_ap_starting"), S(1.2), SH("05_pairing_waiting_phone"),
          S(5.5), SH("06_connected"), MENU, MENU, MENU, MENU, S(0.5), SH("07_osd_ap_icon")]),
 "shortcut": ({}, [S(0.3), "tap", S(0.4), SH("10_tap_from_osd_opens_screen"), S(2.5), SH("11_pairing_from_osd"),
          DOWN, DOWN, S(0.2), SH("12_cancel_row"), OK, S(0.4), SH("13_cancelled")]),
 "wifi_page": ({}, [S(0.3)] + TO_NFC + ["tap", S(2.5), DOWN, OK, S(0.5), SH("20_wifi_ap_page_after_tap")]),
 "readfail": ({"A5K_FAKE_NFC_FAIL": "1"}, [S(0.3)] + TO_NFC + ["tap", S(1.3), SH("30_read_failed")]),
 "timeout": ({"A5K_FAKE_WIFI_MS": "6000"}, [S(0.3)] + TO_NFC + ["tap", S(2.0), SH("40_joining_slow"), S(31.0), SH("41_timeout_failed")]),
 "autotap": ({"A5K_FAKE_NFC_TAP": "1500"}, [S(4.0), SH("50_env_autotap")]),
}

def run(host, out, name, env, ops):
    tmp = tempfile.mkdtemp(prefix="a5knfc")
    sock = os.path.join(tmp, "s.sock"); shot = os.path.join(tmp, "s.ppm"); log = os.path.join(tmp, "l.txt")
    e = dict(os.environ); e.update(env)
    p = subprocess.Popen([os.path.join(host, "a5kui_host"), "--sock", sock, "--shot", shot, "--log", log, "--seconds", "180"],
                         stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL, env=e)
    for _ in range(50):
        if os.path.exists(sock): break
        time.sleep(0.1)
    time.sleep(0.6)
    for op in ops:
        if isinstance(op, int): ctl(sock, host, ["key", op]); time.sleep(0.15)
        elif op == "tap": ctl(sock, host, ["nfc"]); time.sleep(0.1)
        elif op.startswith("sleep:"): time.sleep(float(op[6:]))
        elif op.startswith("shot:"):
            ctl(sock, host, ["shot", shot]); time.sleep(0.8)
            w, h, d = ppm_rows(shot); write_png(os.path.join(out, op[5:] + ".png"), w, h, d); print("saved", op[5:])
    ctl(sock, host, ["quit"]); p.wait(timeout=10)

def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--host", required=True); ap.add_argument("--out", required=True); ap.add_argument("--only", default="")
    a = ap.parse_args(); os.makedirs(a.out, exist_ok=True)
    only = [x for x in a.only.split(",") if x]
    for name, (env, ops) in SCEN.items():
        if only and name not in only: continue
        print("==", name); run(a.host, a.out, name, env, ops)

if __name__ == "__main__": main()
