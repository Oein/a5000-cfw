# custom-binary-app (트랙 B, route-b) — STATUS

> 현재 주력. `a5kinproc.so`를 im.elf에 ptrace 주입하는 네이티브 커스텀 UI. 방향/TODO는 `docs/AI_DOCS/0002.TODO.md`, RE 상세는 `docs/RE/`.

최종: 2026-11-19

## 동작 (실기 검증)
- route-b 주입(a5kinject offset `c37cc`) 안정, im.elf 무크래시, teardown=`/android/a5k/stop`(.so swap 새 파일명).
- 렌더: VDF **detour-and-swap**(단일버퍼 in-place, ch53 연속 scanout). 입력: createEvent + InfraDial 훅(네비키·MENU·다이얼, 소유권 consume).
- 카메라 읽기: **촬영 라이브뷰 활성 시** mode/조리개/셔터/ISO 실값(`model/CAMERA` live-view 게이트, `docs/RE/camera/0002`). 배터리·노출보정은 항상.
- 촬영 OSD = 우리 것(Path A full-rate). 앱런처 액션은 device-seal 토스트(스톡 드롭 방지).

## 현재 이슈 (실기 2026-11-19)
- ⛔ **MENU 눌러도 우리 메뉴가 패널에 안 뜸** (funcMode=23 상태). 입력은 앱 도달·`pushScreen(menu)` 정상 → **디스플레이 쪽 추정**: MENU/DISP/재생 뷰는 OSD를 우리 훅(`vdf_if_port_send_update`)이 아닌 다른 경로로 제출해 우리 detour-swap이 안 먹는 듯. RE 중(`docs/RE/display/0005` 예정).
- 반응성: present가 uncached `/dev/mem` 매핑이라 ~8MB/s(24~33ms). **cached 매핑**(`GRM_bitmapWriteLockBits`)이 진짜 해결, 검증 중(`docs/RE/display/0006` 예정). 행별 dirty present는 보완재(스캔 축소).
- 촬영 외 모드에서 OSD 노출(funcMode 게이팅 미적용), 모니터 밝기 미적용(AM_LOCAL 버그) — `docs/AI_DOCS/0002.TODO.md`.

## 빌드/배포
- `./build.sh a5000-inproc` (docker a5k-cross) → `platform/a5000-inproc/build/a5kinproc.so`.
- 배포: adb staging→telnet cp→`/android/a5k/` → `a5kinject <im.elf pid> <so> c37cc`. 리부트 시 tmpfs 소실→재배치.

## 레이아웃
- `uicore/` — 플랫폼 무관 UI 코어(canvas, gfx, screens, model). `platform/a5000-inproc/` — route-b 어댑터(display_rigel, input_createevent, camera_inproc, engine.h, actions). `platform/host/` — 호스트 시뮬레이터.
- `sysroot*`, `toolchain*`, `aosp/` 등 빌드 입력 일부는 gitignore(펌웨어 파생/재획득).

## 디버그 도구 (2026-11-21)
- **sdctl** (`sdcom/sdctl.cpp`, standalone, `#ifdef A5K_DEBUG_API`): SDCom get/set/sweep. 실기 검증 — `mode N` 실제 변경, `sweep`로 set↔get 매핑 추출(exposureMode 0~24 대부분 자기사상, flash{0,1,2}, drive{0,1,3}, wb는 auto모드서 0잠김=모드의존).
- **in-app 디버그 채널** (`platform/a5000-inproc/debug_channel.cpp`, `#ifdef A5K_DEBUG`, `build.sh a5000-inproc DEBUG=1`→`build-debug/`): a5kinproc 내부 스레드가 `/android/a5k/dbgcmd`→실행→`dbgout`. **실기 검증(192.168.10.181, 디버그 .so 주입)**: 채널 기동·`help`·**`setparam 263 → SR_OK`(SDCom 0x1003 수락)** 확인, 복구·teardown OK, im.elf 무크래시. 단 **in-proc 엔진 읽기(getparam/getgroup)는 track-A 앱 전경 컨텍스트에서 "read failed"** — a5kinproc Engine 미완전 init(순정 카메라 전경에서 재검증 필요). SDCom 경로는 컨텍스트 무관 동작.
