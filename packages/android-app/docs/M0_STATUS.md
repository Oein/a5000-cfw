# 트랙 B M0 상태 — 이식성 UI 코어 + a5000 플랫폼 + 런처 (2026-11-15)

> 범위: NATIVE_PORT_PLAN **M0**("창+키+런처+안전장치")에서 **실제 Rigel 창**을 포함한 구현.
> Rigel/SDCom/입력 런타임 사실은 `TRACKB_RUNTIME.md`, 배포는 `NATIVE_DEPLOY.md`, 아키텍처는 `NATIVE_PORT_PLAN.md`.
> 실기에 아무것도 쓰지 않음 — 전부 에뮬레이터(QEMU `sony-a5000`, 순정 1.10, pristine NAND 사본).

## 0. 한 줄 결과
**기종 독립 `libuicore` + a5000 플랫폼 어댑터 + 런처 APK** 구현 완료. 에뮬레이터에서 **USB로 런처 설치 → 애플리케이션 목록에
"A5K UI" 등재 → 실행**까지 확인했고, 네이티브 `a5kui` 바이너리를 게스트에서 직접 실행해 **실제 Rigel 창 생성·event0·추상
액션 매핑·SDCom 접속 시도·스크린샷·SIGTERM 정리 종료**를 로그로 확인했다.

## 1. 아키텍처 (사용자 요구 반영: 이식성 + 추상 액션)
```
app/native/
  uicore/              libuicore.a — 플랫폼 헤더 include 0. 루프/캔버스/폰트/화면스택/앱컨트롤러/키맵.
                       인터페이스: Display, Input, CameraControl, Storage, Clock, Log (uicore/platform.h)
  platform/a5000/      Rigel 디스플레이 · event0 입력 · SDCom 카메라제어 · chroot/bionic · 크래시정리 · main · launcher(APK)
  platform/host/       호스트 시뮬레이터(PPM/SDL + 가짜 카메라) — 라이브러리가 a5000에 의존 않음을 증명
  platform/a6400/      keymap 스텁(TODO) — SDCom·Rigel·Android 없음, im.elf RAM mod 경로(cfw/docs/A6400_PORTABILITY.md)
  platform/common/     ctl 소켓(a5kctl), PPM 스텁 디스플레이 — 플랫폼 공용(코어 아님)
```
- **입력 = 추상 ACTION**(`uicore::Action`: NAV_*, CONFIRM, BACK, MENU, PLAYBACK, SHUTTER_HALF/FULL, MOVIE_REC,
  DELETE, DIAL_MAIN/SUB_CW/CCW, ZOOM_T/W, FN, CUSTOM_1..5, TOUCH). 화면은 물리 키를 모른다.
- **기종별 바인딩 표 = 데이터**(`platform/<model>/keymap.cpp`의 정적 `Binding[]`). 문맥(CTX_SHOOTING/MENU/PLAYBACK/GLOBAL)별
  매핑 + 사용자 override(Storage 영속) 지원. a5000 = event0 ISV(CLAUDE.md §5, 휴지통 **595**), host = 키보드 ASCII, a6400 = 스텁.
- 인터페이스/키맵 추가 방법은 `app/native/uicore/README.md`.

## 2. 빌드
```
# 호스트 시뮬레이터 (clang; SDL 창은 SDL=1)
app/native/build.sh host
# a5000 bionic ARM (docker a5k-ndk) — 런처가 번들하는 바이너리
app/native/build.sh a5000-ndk      # -> platform/a5000/build/ndk/{a5kui,a5kctl}, strip -> liba5kui.so/liba5kctl.so
# 런처 APK (ndk 바이너리 번들 + assembleRelease + API10 검사)
app/native/build.sh launcher       # -> platform/a5000/launcher/.../a5kui-launcher-release.apk
# (cross/glibc 빌드도 가능: build.sh a5000-cross)
```
- 플래그: `-Os -fno-exceptions -fno-rtti`, armv7-a thumb, vfpv3-d16, **NEON 없음**.
- bionic `libsdcom.so`는 헤더 인라인 접근자(getId/getData/리스너 소멸자)를 export 안 함 → `camera_sdcom.cpp`에서
  문서화된 객체 레이아웃으로 out-of-line 정의(`-DA5K_SDCOM_INLINE_FALLBACK`). glibc 빌드는 라이브러리 심볼 사용.
- 바이너리: `liba5kui.so` 17.7KB(strip), NEEDED = libsdcom/libosal_uipc/libdl/liblog/libstdc++/libc/libm. gralloc/dihal은 dlopen.
- 런처 APK: 패키지 `dev.oein.a5knative`, minSdk/targetSdk 10, **"0 reference(s) above API 10"** 통과,
  `lib/armeabi/{liba5kui.so, liba5kctl.so}` 번들. libtweak.so 재사용(RootShell, Track A와 동일).

## 3. 런처 동작 (NATIVE_DEPLOY §3)
- 투명 Activity(Theme.Translucent) — 라이브뷰가 비치도록. onResume: AppInfoReceive(ABT) + APO/NO + 네이티브 기동.
- 기동 1-라인(libtweak `nativeExec`, chroot 밖 root): 추출된 `liba5kui.so`를 tmpfs `/android/a5k/a5kui`로 복사·chmod →
  `busybox chroot /android /system/bin/sh -c "LD_LIBRARY_PATH=/system/lib /a5k/a5kui --emu"`. onPause/onDestroy: pid 파일로 `kill -TERM`.
- a5kui는 chroot 안에서 돌므로 경로가 chroot 상대(`/a5k/...` = 네이티브 `/android/a5k/...`).

## 4. 에뮬레이터 검증 (ports 46560–46569)
### 4.1 배포 경로 (USB 설치 → 앱 목록 → 실행) — run4
- `pmca-console install -d socket -f a5kui-launcher-release.apk` → `Package: dev.oein.a5knative … Task completed successfully`
  (`test/emu/install.log`).
- MENU → 애플리케이션 → 애플리케이션 목록에 **"A5K UI"** 등재(`test/emu/m0_applist_a5kui.png`) → 실행(Enter).
- 한계: 실행 직후 관찰 단계에서 **엔진 oops**(`RE_ENGINE_OOPS` = 호스트 vCPU 기아, 펌웨어/우리코드 무관, 세션당 수회 재현)로
  런처→a5kui 경로의 로그/앱모드 SDCom 관찰이 중단됨. 설치·등재·실행 자체는 확인됨.

### 4.2 네이티브 바이너리 직접 실행 (관찰 셸, standalone) — run6 (로그 전문)
```
a5kui(a5000) start pid=623 display=rigel sd_dry=0 rgl_dry=0 allowWrite=0
rigel: alloc[0]=0 stride=640 slot=5        # ws_hal_alloc usage 0x133 성공
rigel: alloc[1]=0 stride=640 slot=6
rigel: createWindow=0x13990                # rglCreateWindow("UX_DEBUG_CLIENT",640,480) 성공
rigel: window up (order 0)                 # rglShowWindow + rglSetWindowParam order=0(최상단)
input: /dev/input/event0 opened
ctl: listening on /a5k/a5kctl.sock
camera: connect = -5 (not app mode)        # SDCom: standalone(앱 아님)이라 -5 — TRACKB §3.1대로. 코어가 재시도
action: NAV_RIGHT raw=106 value=1          # a5kctl key 106 → a5000 keymap(CTX_MENU) → NAV_RIGHT
action: CONFIRM   raw=232 value=1
action: MENU      raw=514 value=1
action: DELETE    raw=595 value=1          # 휴지통 595 → DELETE (TRACKB 확정값)
app: screenshot /a5k/shot.ppm -> ok
# kill -TERM 623 → ps에 a5kui 없음(ALIVE=0): SIGTERM 핸들러가 정리 후 _exit (브릭안전 §1)
```
- 스크린샷 `test/emu/m0_emu_a5kui_shot.png`: a5kui가 게스트에서 렌더한 화면(헤더 "DISP RIGEL FRAME 9", ACTIONS 패널에
  매핑된 액션, CAMERA CONNECT NO). **실제 Rigel 백엔드로 창 생성**(스텁 폴백 아님).
- 참고: Rigel **합성(화면 표시)**은 앱(Scalar) 모드에서만 서버가 렌더(TRACKB §1.3). standalone이라 패널엔 안 뜨지만
  클라이언트 경로(alloc→create→show→order→swap)는 전부 성공. 캔버스 내용은 shot으로 확인.

### 4.3 호스트 시뮬레이터
- `a5kui_host --sock … --shot …` + `a5kctl key <ascii>` → 액션 매핑/렌더 확인(`test/host/m0_host_screenshot.png`).
  라이브러리에 a5000 의존이 없음을 증명(디스플레이=PPM 스텁 또는 SDL, 카메라=가짜).

## 5. 아직 남은 것 (M1 이후 / 관찰 보류)
- **앱 모드에서 Rigel 합성 스크린덤프 + SDCom connect=0**: 런처로 정상 진입한 세션에서 확인 필요(이번엔 엔진 oops로 중단).
  standalone 실행으로 바이너리는 입증됨 — 남은 건 "앱 모드 관찰"뿐.
- Rigel 블렌드/불투명도 필드, premultiplied 여부(TRACKB §1.7).
- 0x1000 OpenCamera/0x1002 GetParameters는 상태변경(OpenCamera)이라 M0 제외(M1 라이브뷰에서).
- 베이크 폰트/아이콘 아틀라스(`uicore/gen`, 다른 작업분기 산출물)는 존재. M0는 내장 5x7 폰트 사용(펌웨어 파생 자산 M0 범위 외).
  캔버스를 `uicore/gen`으로 바꾸면 순정 폰트/아이콘 적용 가능.

## 6. 재현 절차 (에뮬레이터)
전제: `emulator/`에서 `A5K_QEMU=<qemu-m0c>`, pristine NAND 사본, 포트 46560–46569.
1. 부팅: `A5K_SHELL=46561 A5K_NAND=<clone> A5K_QEMU=<q> ./run.sh -display none -monitor tcp:127.0.0.1:46560,server,nowait -M usb-socket=127.0.0.1:46562`
   — **부팅 중 UART 셸을 두드리지 말 것**(RT 커널 안정화 전 접근 시 엔진 oops 유발). ~110s 조용히 대기.
2. (배포) PMCA `install -d socket -f <launcher.apk>` → App List → A5K UI 실행.
   또는 (바이너리 검증) `a5kshell push liba5kui.so a5kui; push liba5kctl.so a5kctl` →
   `busybox chroot /android /system/bin/sh -c "LD_LIBRARY_PATH=/system/lib /a5k/a5kui --emu" &` → `cat /android/a5k/a5kui.log`.
3. 키: `busybox chroot /android /a5k/a5kctl key 106` 등. 스크린샷: `a5kctl shot /a5k/shot.ppm` → `a5kshell pull`.
4. 종료: `kill -TERM $(cat /android/a5k/a5kui.pid)` → `ps`에 없음 확인.

## 7. 벽돌 안전 (CLAUDE.md §1)
플래시/`/usr`/`/system`/Backup.bin 미접촉. 전부 (a) 일반 앱 설치영역 또는 (b) tmpfs `/android/a5k`. libsdcom 객체 크기 `static_assert`,
SIGSEGV/BUS/ILL/FPE/ABRT/TERM/INT/HUP → disconnect+디스플레이해제+`_exit`. SDCom은 휘발 엔진상태(재부팅 복구). **벽돌 위험 0.**
