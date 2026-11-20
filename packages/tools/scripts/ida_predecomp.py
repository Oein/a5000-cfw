#!/usr/bin/env python3 -I
"""Idle-time batch pre-decompiler for the Sony a5000 firmware.

Runs LOCALLY (macOS), drives IDA Pro 9.4 headless on `ssh windev`. When IDA is
idle it works through every firmware ELF that is not yet decompiled, producing
`<name>.c` / `.funcs.tsv` / `.names.tsv` / `.strings.tsv` / `.done` under
packages/data/re/ida/ (gitignored). Later RE reads those caches instead of
waiting for IDA.

Design rules:
  * Single IDA license -> yield to any OTHER idat/ida running on windev
    (interactive use or another agent). Poll, back off, resume.
  * Resumable: a binary with a local `<name>.done` is skipped.
  * Untrusted firmware: idat only ANALYZES; the target is never executed.
    Files are already extracted locally; paths are passed as args.
  * Disk: the giant `.i64` is deleted on windev after export (regenerable).
  * Nothing here is committed to the repo except this script; the decompiled
    output is Sony IP and lives only in the gitignored packages/data/re/ida/.

Usage:
  python3 -I packages/tools/scripts/ida_predecomp.py            # run the queue
  python3 -I packages/tools/scripts/ida_predecomp.py --list     # show queue, do nothing
  python3 -I packages/tools/scripts/ida_predecomp.py --only libGLESv2_dmp.so,libNetService.so
  python3 -I packages/tools/scripts/ida_predecomp.py --max 20   # stop after N binaries
Env:
  A5K_WINDEV=windev         ssh host alias
  A5K_IDA='C:\\Program Files\\IDA Professional 9.4\\idat.exe'
  A5K_WINWORK=C:\\a5000\\predecomp   windev scratch dir
"""
import os, sys, subprocess, time, shlex, argparse

HERE = os.path.dirname(os.path.abspath(__file__))
REPO = os.path.abspath(os.path.join(HERE, '..', '..', '..'))
DATA = os.path.join(REPO, 'packages', 'data')
IDA_OUT = os.path.join(DATA, 're', 'ida')
FW_ROOTS = [os.path.join(DATA, 'rootfs_usr'), os.path.join(DATA, 'firmware', 'unpacked')]

WINDEV = os.environ.get('A5K_WINDEV', 'windev')
IDA = os.environ.get('A5K_IDA', r'C:\Program Files\IDA Professional 9.4\idat.exe')
# Forward slashes throughout: scp's remote path and idat accept them on Windows,
# whereas backslashes break scp-back (see docs/AI_DOCS/0001.LOCAL_DEV_GUIDE.md).
WINWORK = os.environ.get('A5K_WINWORK', 'C:/a5000/predecomp')
IDA_SIDE = os.path.join(HERE, 'ida_predecomp_ida.py')

# Highest-value first: things the active RE (display/net/mem) wants, then the rest by size.
PRIORITY = [
    'libGLESv2_dmp.so', 'libGLESv1_CM_dmp.so', 'cmn_view_processDataMgr.so',
    'libNetService.so', 'wpa_supplicant', 'dhcpd', 'libSysDef.so',
    'libScalarDaemon.so', 'libInfraFsys.so', 'libmpr.so', 'orb-server',
]


def sh(*a, **kw):
    return subprocess.run(a, capture_output=True, text=True, **kw)


def ssh(ps, timeout=None):
    """Run a PowerShell command on windev; join statements with ';'."""
    return sh('ssh', WINDEV, ps, timeout=timeout)


def ps_quote(s):
    return "'" + s.replace("'", "''") + "'"


def inventory():
    """{basename: (size, local_path)} for every ELF under the firmware roots, dedup by name."""
    seen = {}
    for base in FW_ROOTS:
        if not os.path.isdir(base):
            continue
        for dp, dn, fn in os.walk(base, followlinks=False):
            for f in fn:
                p = os.path.join(dp, f)
                if os.path.islink(p) or not os.path.isfile(p):
                    continue
                try:
                    with open(p, 'rb') as h:
                        if h.read(4) != b'\x7fELF':
                            continue
                except OSError:
                    continue
                if f not in seen:
                    seen[f] = (os.path.getsize(p), p)
    return seen


def build_queue(only=None):
    inv = inventory()
    done = {f[:-5] for f in os.listdir(IDA_OUT) if f.endswith('.done')}
    todo = {k: v for k, v in inv.items() if k not in done}
    if only:
        todo = {k: v for k, v in todo.items() if k in only}
    order = []
    for name in PRIORITY:
        if name in todo:
            order.append(name)
    for name in sorted(todo, key=lambda k: -todo[k][0]):
        if name not in order:
            order.append(name)
    return [(n, todo[n][0], todo[n][1]) for n in order], len(inv), len(done)


def ida_busy():
    """True if any OTHER ida/idat process is running on windev (yield to it)."""
    r = ssh("(Get-Process -Name ida,ida64,idat,idat64 -ErrorAction SilentlyContinue | "
            "Measure-Object).Count", timeout=60)
    try:
        return int((r.stdout or '0').strip() or '0') > 0
    except ValueError:
        return True  # unsure -> treat as busy, be polite


def wait_idle(poll=90, announce=True):
    n = 0
    while ida_busy():
        if announce and n == 0:
            print('[predecomp] IDA busy on windev -> waiting...', flush=True)
        n += 1
        time.sleep(poll)
    return True


def process_one(name, path):
    win_bin = f'{WINWORK}/{name}'
    win_i64 = win_bin + '.i64'
    pre_out = f'{WINWORK}/{name}'           # ida-side writes <pre_out>.c/.tsv/.ok
    print(f'[predecomp] {name}: upload', flush=True)
    ssh(f'New-Item -ItemType Directory -Force -Path {ps_quote(WINWORK)} | Out-Null', timeout=60)
    up = sh('scp', '-q', path, f'{WINDEV}:{win_bin}')
    if up.returncode != 0:
        print(f'[predecomp] {name}: scp up FAILED: {up.stderr.strip()}', flush=True)
        return False
    up2 = sh('scp', '-q', IDA_SIDE, f'{WINDEV}:{WINWORK}/ida_predecomp_ida.py')
    if up2.returncode != 0:
        print(f'[predecomp] {name}: scp script FAILED', flush=True)
        return False
    # 1) auto-analyze -> .i64 (batch). 2) export via our script.
    print(f'[predecomp] {name}: analyze', flush=True)
    a = ssh(f'& {ps_quote(IDA)} -A -B -L{WINWORK}/{name}.log {win_bin}', timeout=3600)
    if not _remote_exists(win_i64):
        print(f'[predecomp] {name}: no .i64 produced (rc={a.returncode})', flush=True)
        return False
    print(f'[predecomp] {name}: decompile+export', flush=True)
    ssh(f'$env:PRE_OUT={ps_quote(pre_out)}; & {ps_quote(IDA)} -A '
        f'-S{ps_quote(WINWORK + "/ida_predecomp_ida.py")} {win_i64}', timeout=7200)
    if not _remote_exists(pre_out + '.ok'):
        print(f'[predecomp] {name}: export did not complete (.ok missing)', flush=True)
        _remote_rm(win_i64)
        return False
    # fetch artifacts
    ok = True
    for ext in ('.c', '.funcs.tsv', '.names.tsv', '.strings.tsv', '.ok'):
        d = sh('scp', '-q', f'{WINDEV}:{pre_out}{ext}', os.path.join(IDA_OUT, name + ext))
        if d.returncode != 0:
            print(f'[predecomp] {name}: scp back {ext} FAILED', flush=True)
            ok = False
    if ok:
        with open(os.path.join(IDA_OUT, name + '.done'), 'w') as h:
            h.write(time.strftime('%Y-%m-%dT%H:%M:%S') + ' predecomp\n')
        print(f'[predecomp] {name}: DONE', flush=True)
    # free windev disk (the .i64 is regenerable and huge)
    _remote_rm(win_i64, win_bin, *(pre_out + e for e in ('.c', '.funcs.tsv', '.names.tsv', '.strings.tsv', '.ok')))
    return ok


def _remote_exists(winpath):
    r = ssh(f'Test-Path {ps_quote(winpath)}', timeout=60)
    return (r.stdout or '').strip().lower() == 'true'


def _remote_rm(*winpaths):
    args = ','.join(ps_quote(p) for p in winpaths)
    ssh(f'Remove-Item -Force -ErrorAction SilentlyContinue {args}', timeout=120)


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('--list', action='store_true')
    ap.add_argument('--only', default='')
    ap.add_argument('--max', type=int, default=0)
    ap.add_argument('--poll', type=int, default=90)
    args = ap.parse_args()

    only = set(x.strip() for x in args.only.split(',') if x.strip()) or None
    queue, total, done = build_queue(only)
    mb = sum(s for _, s, _ in queue) / 1e6
    print(f'[predecomp] ELF total={total} done={done} queue={len(queue)} ({mb:.1f} MB)', flush=True)
    if args.list:
        for n, s, _ in queue:
            print(f'  {s/1e3:8.1f}KB  {n}')
        return

    n_ok = 0
    for i, (name, size, path) in enumerate(queue, 1):
        if args.max and i > args.max:
            break
        # re-check in case something finished it, or the file vanished
        if os.path.exists(os.path.join(IDA_OUT, name + '.done')):
            continue
        wait_idle(poll=args.poll)
        print(f'[predecomp] [{i}/{len(queue)}] {name} ({size/1e3:.1f}KB)', flush=True)
        try:
            if process_one(name, path):
                n_ok += 1
        except subprocess.TimeoutExpired:
            print(f'[predecomp] {name}: TIMEOUT, skip', flush=True)
        except Exception as e:
            print(f'[predecomp] {name}: ERROR {e!r}, skip', flush=True)
    print(f'[predecomp] finished: {n_ok} newly decompiled', flush=True)


if __name__ == '__main__':
    main()
