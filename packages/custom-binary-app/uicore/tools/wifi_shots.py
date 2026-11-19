#!/usr/bin/env python3
"""wifi_shots.py -- drive the host simulator through the Wi-Fi scenarios (fake backend) and save 640x480 PNGs.
   python3 -I tools/wifi_shots.py --host app/native/platform/host/build --out app/native/uicore/test/wifi
Ops: int = key, "sleep:S", "shot:name".  Host keys: w/s up/down, a/d left/right, Enter ok, m MENU, f FN, x trash, ./, wheel.
Fake backend timing (real time): scan 0.9s, connect 1.5s, AP start 0.8s, fake phone joins at 6s.
Fake password of iptime-A402 is "aaaaaaaa" (8 x 'a'); HomeWiFi_5G / Office-LAN are pre-saved."""
import argparse, os, subprocess, sys, tempfile, time
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from textinput_shots import ppm_rows, write_png, ctl, UP, DOWN, LEFT, RIGHT, OK, MENU, FN, DEL, CW, CCW, rep

TO_WIRELESS = [MENU, DOWN, DOWN, RIGHT, RIGHT]           # items column of the wireless group; first = 스마트폰으로 전송
TO_HUB   = TO_WIRELESS + rep(DOWN, 3) + [OK]             # 액세스 지점 설정 -> Wi-Fi hub
TO_PHONE = TO_WIRELESS + [OK]                            # 스마트폰으로 전송 -> AP page (auto-start)
TO_PC    = TO_WIRELESS + [DOWN, OK]                      # PC로 전송 -> STA page (auto-start)
S = lambda t: "sleep:%s" % t
SH = lambda n: "shot:" + n

SCEN = {
 "sta": [S(0.3)] + TO_HUB + [S(0.4), SH("01_master_off"), OK, S(0.4), SH("02_master_on_sta"),
          DOWN, DOWN, OK, S(0.3), SH("03_scanning"), S(1.3), SH("04_scan_list"),
          OK, S(0.5), SH("05_connecting_saved"), S(2.0), SH("06_connected"),
          DOWN, DOWN, DOWN, FN, S(0.3), SH("07_options_secured"), MENU, UP, UP, UP, FN, S(0.3), SH("08_options_connected"), MENU,
          MENU, MENU, MENU, S(0.5), SH("09_osd_connected")],
 "password": [S(0.3)] + TO_PC + [S(2.0), SH("10_pc_entry_list"),
          DOWN, DOWN, DOWN, OK, S(0.5), SH("11_password_open")] + rep(OK, 8) + [S(0.3), SH("12_password_typed"),
          CCW, OK, S(0.4), SH("13_password_connecting"), S(2.0), SH("14_password_connected")],
 "wrongpw": [S(0.3)] + TO_PC + [S(2.0), DOWN, DOWN, DOWN, OK, S(0.5)] + rep(OK, 7) + [CW, OK, S(0.3), CCW, CCW, OK, S(2.2),
          SH("15_wrong_password_toast")],
 "saved": [S(0.3)] + TO_HUB + [OK, DOWN, DOWN, DOWN, OK, S(0.4), SH("20_saved_list"), OK, S(0.3), SH("21_saved_options"),
          DOWN, DOWN, S(0.2), SH("22_saved_forget_sel"), OK, S(0.3), SH("23_forget_confirm"), DOWN, OK, S(0.4), SH("24_forgotten")],
 "manual": [S(0.3)] + TO_HUB + [OK, DOWN, DOWN, DOWN, DOWN, OK, S(0.5), SH("25_manual_ssid")],
 "ap": [S(0.3)] + TO_HUB + [OK, DOWN, OK, S(0.4), SH("30_master_ap_mode"),
          DOWN, OK, S(0.3), SH("31_ap_starting"), S(1.2), SH("32_ap_waiting"), FN, S(0.2), SH("33_ap_pw_shown"), S(5.5), SH("34_ap_phone_joined"),
          OK, S(0.5), SH("35_ap_edit_ssid")],
 "ap_direct": [S(0.3)] + TO_PHONE + [S(2.2), SH("36_ap_direct_waiting"), MENU, MENU, S(0.5), SH("37_osd_ap")],
 "connecting_osd": [S(0.3)] + TO_PC + [S(2.0), OK, MENU, MENU, S(0.1), SH("38_osd_connecting")],
}

def run(host, out, name, ops):
    tmp = tempfile.mkdtemp(prefix="a5kwf")
    sock = os.path.join(tmp, "s.sock"); shot = os.path.join(tmp, "s.ppm"); log = os.path.join(tmp, "l.txt")
    p = subprocess.Popen([os.path.join(host, "a5kui_host"), "--sock", sock, "--shot", shot, "--log", log, "--seconds", "120"],
                         stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
    for _ in range(50):
        if os.path.exists(sock): break
        time.sleep(0.1)
    time.sleep(0.6)
    for op in ops:
        if isinstance(op, int): ctl(sock, host, ["key", op]); time.sleep(0.15)
        elif op.startswith("sleep:"): time.sleep(float(op[6:]))
        elif op.startswith("shot:"):
            ctl(sock, host, ["shot", shot]); time.sleep(0.35)
            w, h, d = ppm_rows(shot); write_png(os.path.join(out, op[5:] + ".png"), w, h, d); print("saved", op[5:])
    ctl(sock, host, ["quit"]); p.wait(timeout=10)

def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--host", required=True); ap.add_argument("--out", required=True); ap.add_argument("--only", default="")
    a = ap.parse_args(); os.makedirs(a.out, exist_ok=True)
    only = [x for x in a.only.split(",") if x]
    for name, ops in SCEN.items():
        if only and name not in only: continue
        print("==", name); run(a.host, a.out, name, ops)

if __name__ == "__main__": main()
