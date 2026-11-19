# platform/a5000-inproc — uicore running INSIDE im.elf (Track B route (b))

The convergence of **uicore** (platform-agnostic UI) and **route (b)** (the proven
in-process injection path): this backend compiles uicore into a glibc ARM shared
object that the stock loader maps into **im.elf**, where it draws the real custom
shooting OSD onto the panel, reads live engine state, takes keys, and suppresses
the stock UI — all RAM-only and reversible.

Everything it relies on is empirically proven in the emulator; see
`app/docs/TRACKB_INPROCESS.md` §10–§18. This backend re-derives nothing.

## Files
| File | uicore interface | Mechanism (TRACKB_INPROCESS.md) |
|---|---|---|
| `inproc.cpp` | — (entry) | self-gating constructor → detached worker; assembles `Platform`, suppresses stock (gate 2b), runs `App` |
| `display_osd.cpp` | `Display` | blit RGBA8888 Canvas → OSD double-buffer (0x8f758000↔0x8f7ee000, RGBA4444) via `/dev/mem` (§10) |
| `input_event0.cpp` | `Input` | read `/dev/input/event0` EV_KEY → raw ISV codes (§5); a5000 keymap maps raw→action |
| `camera_inproc.cpp` | `CameraControl` | READ `getModelInfo("model/CAMERA",15,…)` (§13/§16); SET image aspect via `CmnViewProcessDataMgr` vtable (§18, guarded) |
| `engine.h` | — | per-boot libObj/viewUnified2 base resolution + the fixed Thumb vaddr offsets + gate 2b + OSD probe |
| (reused) `../a5000/keymap.cpp` | bindings (data) | a5000 physical-key → action table |
| (reused) `../a5000/sys_a5000.cpp` | `Clock`/`Log` | generic libc |

uicore core stays platform-header-free; this backend is the only code that knows
im.elf internals.

## Build
```
app/native/build.sh a5000-inproc      # -> platform/a5000-inproc/build/a5kinproc.so (docker a5k-cross, glibc)
```
The C++ runtime is linked statically (`-static-libstdc++ -static-libgcc`) so the
`.so` has **no libstdc++ NEEDED** to satisfy on the guest — only libc/libpthread/libm.
uicore sources are compiled here with `-fPIC` (the committed `libuicore.a` is non-PIC).

## Install (emulator only — never the real device)
Autostart via the stock loader's `/setting/env.txt` LD_PRELOAD hook (§17, no ptrace,
every cold boot, reversible):
```
/setting/mode/dmode   = "rb"                               (benign; gates set_oreore_env)
/setting/env.txt      = "LD_PRELOAD=/setting/a5k/a5kinproc.so"
/setting/a5k/a5kinproc.so
```
Optional env tokens in `env.txt` (space-separated):
`A5K_UI_ASSETS=<dir>` baked fonts/icons (else built-in 5x7 fallback),
`A5K_UI_FRAME_MS=<n>` (default 66), `A5K_UI_SECS=<n>` (0 = until reboot),
`A5K_UI_SETTEST=1` one-shot visible aspect SET self-test, `A5K_UI_TEST=1` test screen.

Reverse: delete those three `/setting` paths (or just reboot for the RAM state).
`nflasha1`/`nflasha2` structure, `/usr`, `/system`, rootfs, kernel untouched →
recovery invariant intact, brick risk 0.

## Status / what is live vs representative
- **Render + suppression + autostart**: real, from the proven mechanisms.
- **Baked assets**: `A5K_UI_ASSETS=<dir>` loads the real OSD fonts + icon atlas, so the
  shooting OSD renders in the full host-sim style (mode/drive/AF icons, EV/ISO glyphs,
  baked numerals) instead of the 5x7 fallback.
- **CameraControl READ — exposure MODE is LIVE**: `readState()` reads `getModelInfo(
  "model/CAMERA", 15, 0x12C)` (the engine `s_exposureMode` D value) and maps it to the
  uicore mode via `modeDtoUicore()` (settings_sdcom_map.tsv "SD s_exposureMode D=" ↔
  kVals_EXPOSURE_MODE). Verified: the stock boot mode is landscape (D=11 → uicore 3 →
  the mountain icon the live OSD then draws, matching the stock OSD). The rest of the
  exposure row (SS/F/EV/ISO) needs an AE-active engine (lens + card) which the emulator
  does not run, and each per-param model→catalog value is the M2 "R1" RE — so those
  stay representative; the readable model params are logged (`mdlscan_so.c` scans them).
- **CameraControl SET**: image aspect (§18) is wired + guarded (visible, reversible).
- **Input — LIVE via the engine-input hook** (`input_createevent.cpp`, TRACKB §21/§22):
  in Diadem event0 is never fed, so keys are taken from the single native confluence point
  `InputService::createEvent` (libObj 0x37F854), hooked with a non-destructive absolute
  Thumb jump. my_createEvent records each input id (0x15010000 | code<<4 | edge), the id is
  decoded to the a5000 ISV raw code, and the existing keymap maps raw→abstract action for
  the real uicore `App`. Verified in-emulator: MENU opens the uicore cascade menu, the 4-way
  navigates categories/groups (#D34100 focus moves), CENTER drills in. (The legacy
  `input_event0.cpp` is kept for reference but not built; see §20.3 for why event0 is empty
  in-process.) Note: inject keys with a real hold (~400 ms) so the sub-micro/ADC poll
  registers them.
