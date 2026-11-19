# libuicore — 기종 독립 UI 코어

> 목적: a5000의 커스텀 카메라 UI 로직(루프·그래픽·위젯·화면 스택·앱 컨트롤러)을 **플랫폼 의존 0**으로 두고,
> 나중에 a6400(SDCom·Rigel·Android 전부 없음, im.elf RAM mod 로더로 제어 — `cfw/docs/A6400_PORTABILITY.md`)에
> 그대로 이식한다. 코어는 `uicore::` 네임스페이스. C++ 부분집합(`-fno-exceptions -fno-rtti`, STL 비의존),
> bionic gcc / glibc gcc / host clang 모두에서 컴파일된다.

## 디렉터리
```
app/native/
  uicore/                  libuicore.a (이 라이브러리) — 플랫폼 헤더 include 금지
    include/uicore/        public 인터페이스 (platform.h, app.h, screen.h, canvas.h, action.h, keymap.h, types.h, log.h)
    src/                   구현 (app, canvas, screen_test, keymap, action, log) + font_data.h(내장 5x7 폰트)
    common.mk  Makefile    TARGET=host|ndk|cross -> build/<TARGET>/libuicore.a
  platform/
    a5000/                 a5000 어댑터 + main + launcher(APK). Rigel 디스플레이·event0 입력·SDCom 카메라제어·chroot/bionic
    host/                  호스트 시뮬레이터 어댑터(SDL2/PPM + 가짜 카메라) — 라이브러리가 a5000에 의존하지 않음을 증명
    a6400/                 (STUB) keymap만. 버튼 캡처 후 어댑터 추가 예정
    common/                ctl 소켓(a5kctl), PPM 스텁 디스플레이 — 플랫폼 공용(코어 아님)
```

## 코어 ↔ 플랫폼 경계 (`uicore/platform.h`)
코어는 아래 순수가상 인터페이스로만 바깥과 통신한다. 플랫폼이 구현을 만들어 `Platform` 구조체로 `App::init`에 넘긴다.

| 인터페이스 | 역할 | a5000 구현 | a6400(예정) |
|---|---|---|---|
| `Display` | 백버퍼(Canvas) 합성/표시, dirty rect, 크기/포맷 | Rigel UX_DEBUG_CLIENT 창(`ws_hal_*`/`rgl*`) | grm_gles/dmpgles2 또는 im.elf 창 경로 |
| `Input` | raw 키/터치 이벤트 블로킹 poll | event0(ISV) + ctl 소켓 | im.elf 입력 후킹 |
| `CameraControl` | 설정 get/set(카탈로그 id), 셔터/AF/줌/노출스텝, 정보 읽기 | SDCom(ScalarDaemon) | im.elf 내부 메시지/후킹 |
| `Storage` | prefs/파일 영속 | tmpfs 플랫 파일 | RAM/파일 |
| `Clock` | `nowMs()` | gettimeofday | 동일 |
| `Log` | 한 줄 로그 싱크 | 파일+stderr | 동일 |

코어는 `Display`/`Input`를 **직접 init 하지 않는다**(플랫폼 main이 백엔드 폴백을 처리). 카메라 connect는 코어가 재시도한다.

## 입력 = 추상 ACTION (물리 키 아님)
화면(Screen)은 **물리 키를 모른다.** `uicore::Action`(NAV_UP/DOWN/LEFT/RIGHT, CONFIRM, BACK, MENU, PLAYBACK,
SHUTTER_HALF/FULL, MOVIE_REC, DELETE, DIAL_MAIN_CW/CCW, DIAL_SUB_CW/CCW, ZOOM_T/W, FN, CUSTOM_1..5, TOUCH)만 소비한다.
- 플랫폼은 **물리 raw 코드 → Action** 바인딩 표를 **데이터**로 제공한다(`platform/<model>/keymap.cpp`의 정적 `Binding[]`).
- `Binding{ raw, context, action }`. `Context`(GLOBAL/SHOOTING/MENU/PLAYBACK)로 **문맥별 매핑**을 둔다
  (예: 휠 회전 = 메뉴에선 NAV, 촬영화면에선 노출 변경). `CTX_GLOBAL`은 모든 문맥 폴백.
- 코어의 `KeyMap`이 (raw, 활성 화면의 context)로 Action을 해석한다. 우선순위: 사용자 override → (raw,ctx) → (raw,GLOBAL).
- 사용자 리매핑/커스텀 키는 `KeyMap`의 override(런타임 추가)로, `Storage`에 load/save(키 접두사 `keymap.`)한다.

### 새 기종 버튼 추가하는 법
1. `platform/<model>/keymap.cpp`에 그 기종 **물리 코드 → Action** 표를 채운다. 기존 Action 어휘를 재사용한다
   (앞다이얼→DIAL_MAIN, 뒷다이얼→DIAL_SUB, C1..C3→CUSTOM_1..3, AEL/AF-ON/Fn/터치 등). 버튼이 달라도 **화면·코어는 그대로**.
2. 문맥이 다르면 `CTX_*` 행을 추가한다(촬영/메뉴/재생).
3. raw 코드는 실기/에뮬에서 입력 후킹으로 캡처해 넣는다(a5000은 CLAUDE.md §5 + TRACKB_RUNTIME; 휴지통=595).

## 새 플랫폼 추가 (a6400 예시)
1. `platform/a6400/`에 어댑터 6종을 만든다:
   - `display_*`(grm_gles/dmpgles2 또는 im.elf 창), `input_*`(im.elf 입력 후킹→raw 이벤트),
     `camera_*`(`CameraControl`을 im.elf 내부 메시지/후킹으로 — SDCom 없음), `sys_*`(Clock/Log/Storage), `keymap.cpp`, `main`.
2. `main`에서 어댑터를 `Platform`에 담아 `App::init(opt, plat)` → `app.run()`.
3. 빌드: `uicore/common.mk`에 타겟을 추가하거나 기존 ndk/cross 타겟 재사용. 코어/화면 코드는 **수정 없음**.
4. 현재 `platform/a6400/keymap.cpp`는 TODO 스텁(물리 코드 미확정). `cfw/docs/A6400_PORTABILITY.md` 참고.

## 빌드
```
# 호스트 시뮬레이터 (PPM; SDL 창은 SDL=1)
app/native/build.sh host           # 또는 make -C platform/host [SDL=1]
# a5000 bionic ARM (docker a5k-ndk) → platform/a5000/build/ndk/{a5kui,a5kctl}
app/native/build.sh a5000-ndk
# 런처 APK (ndk 바이너리 번들 + assemble + API10 검사)
app/native/build.sh launcher
```
자세한 M0 실행/검증 절차는 `app/docs/M0_STATUS.md`.
