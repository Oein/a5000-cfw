# 트랙 B 경로 (b) — im.elf 내부(in-process)에서 우리 UI 구동 (앱모드/SDCom 없이)

> 작성 2026-11-15, **RE 전용**(코드 커밋·펌웨어 수정·실기 접촉 없음). 동적 관찰은 전부 **에뮬레이터**
> (`emulator/`, QEMU `sony-a5000`, 순정 fw 1.10 무수정, 사용자 NAND pristine 클론, `A5K_SHELL` UART2 루트 셸).
> 표기: **[V]** = 에뮬 관찰, **[S]** = 정적(디컴파일/IMDB/심볼/바이너리 문자열) 근거, **[?]** = 미확정.
> 배경: CLAUDE.md §1(벽돌 0)·§2(두 트랙); `app/docs/SONY_PROCESS_MAP.md`(im.elf 분해·Appframework 11%),
> `app/docs/TRACKB_RUNTIME.md`(Rigel/SDCom/입력 런타임), `app/docs/NATIVE_DEPLOY.md`(백도어 배포),
> `app/docs/NATIVE_PORT_PLAN.md`·`app/native/uicore/`(uicore 어댑터), `cfw/docs/RECOVERY_MAP.md`(복구 불변식),
> `cfw/docs/BOOT_TIME.md`(부팅 프로파일), `cfw/docs/A6400_PORTABILITY.md` + ivica00/sony-a6400-research(참조 구현).

---

## 0. 한눈 요약 / 결론

**경로 (a)**(현재): 설치 APK 런처 → 앱모드(D2A/state 5) 진입 → **외부 프로세스**가 SDCom(osal_uipc IPC)으로 엔진 제어 +
Rigel `UX_DEBUG_CLIENT` 창으로 합성. 앱모드는 "외부" 프로세스에만 필요. a6400엔 APK 플랫폼이 없어 (a)는 불가.

**경로 (b)**(본 문서): 우리 UI 코드를 **im.elf 주소공간 안에** 올려, 순정 UI(appFw/MINT/viewUnified)를 억제하고
**순정과 똑같은 인프로세스 내부 호출**로 엔진을 몬다. 앱모드/런처APK/SDCom 전부 불필요. a6400 유일 경로와 동일한 모양.

핵심 발견(에뮬 pid 147 = im.elf):
- im.elf는 **glibc**(`/lib/ld-2.11.2.so`) 단일 프로세스이고, UI 라이브러리 **viewUnified2/8·libmintx(MINT)·libMWF·
  libObj·libScalarDaemon·libsdcom가 전부 같은 주소공간에 로드**되어 있다 [V]. 스톡 UI는 **IMDB 스테이지 테이블**로
  dlopen+init 되고(row 102 `appFw.so Application_System_Init`), 엔진은 **같은 프로세스 내부 호출**로 몬다.
- 스톡 UI의 **인프로세스 제어 API = `viewManagerIf/ViewBase/LkmBase::requestModelExecute("model/<NAME>", len, ParamList*)`**
  (model/CAMERA, model/STILL_REC, model/DISPLAY, model/MOVIE_REC_MANUAL, model/MEDIA … 네임스페이스), 파라미터는 MWF `ParamList/ObjMsg` [S].
  이게 **SDCom msgId의 인프로세스 등가물**이다. 인프로세스에서는 이걸 직접 호출하면 된다.
- 인프로세스 합성 타깃 = **`UX_MAIN_WINDOW`**(서버 자신의 메인 서피스). (a)의 app-mode 게이팅(`ctx+8==0`)은 **외부
  클라이언트에만** 적용 — 인프로세스는 우리가 서버 안에 있으므로 **Diadem(순정 촬영) 모드에서 그대로 라이브뷰 위 합성 가능**.
- **RAM-only 주입 = ptrace**(루트 백도어 프로세스가 im.elf에 attach → tmpfs `.so` dlopen + 함수 패치). 벽돌 0, 재부팅 복구.
  이것이 ivica00이 a6400 실기에서 입증한 방식(174스레드 freeze→패치→resume, `.so` 후킹)과 동일.
- **영속(production)**: a5000은 a6400과 달리 `/etc`·`/usr`가 **ro flash**라 쓰기 가능한 `/etc/ld.so.preload`가 **없다** [V].
  최저 벽돌티어 영속 = **설치 APK(저티어, 삭제=완전 원복)의 부팅 오토런이 매 부팅 ptrace 주입을 재실행** + (옵션) **순정이
  이미 쓰는 `/setting/mode/dmode` LD_PRELOAD 훅**(§6). 복구 불변식(updater 도달) 절대 보존.
- **부팅시간 ≤ 스톡**을 수용기준으로 건다. 주입 훅은 im.elf 기동기(B/C 구간)에 가볍게 발화하고, 억제하는 스톡 UI 작업
  (appFw/viewUnified init + 상시 11% OSD 재그리기 + Android 설정DB 복원 대기)이 우리 추가비용보다 커서 **넷 동일~빠름**이 성립(§7).

---

## 1. 스톡 UI는 im.elf에 어떻게 결합되나 (질문 1)

### 1.1 IMDB 스테이지 테이블 = 인프로세스 플러그인/등록 레지스트리 [S][V]
`im.elf`(21KB 로더)는 `libIMDB`의 **스테이지 테이블**(`re/ida/libIMDB.imdb_raw.txt`, 147행)을 순서대로 처리해
전 서브시스템을 올린다. 각 row:
- `type` = 종류: `10000001`=`.ko` insmod, `20000001/2`=라이브러리 dlopen+`init=` 호출, `20000004`=앱.
- `file=` 대상, `init=` 진입함수, 그리고 `sus=/res=/inact=/act=` 수명 콜백(서스펜드/리줌/비활성/활성).

UI 관련 핵심 row:
| row | type | file | init | 의미 |
|---|---|---|---|---|
| 30 | 20000002 | **libMWF.so** | `MWF_Init` | **Model/Framework**(엔진 제어 미들웨어) 등록 |
| 102 | 20000004 | **appFw.so** | **`Application_System_Init`** | 앱 프레임워크 기동(`act=AppConfig.so`, `inact=AdjChkConfig.so`). UI 수명/뷰 시스템 — **순정 UI 진입점** |

즉 스톡 UI는 **dlopen+init 기반 플러그인**으로 결합된다. `appFw.so`의 `Application_System_Init`이 `AppConfig.so`를
불러 뷰 시스템(viewUnified*/MINT)을 구성하고 **"Appframework" 스레드**를 돌린다(순정 촬영화면 유휴에도 ~11% CPU로
OSD 재그리기 — `SONY_PROCESS_MAP.md` §3.1).

### 1.2 에뮬 확인 — 한 주소공간에 UI 전부 [V]
im.elf(pid 147) `/proc/147/maps`에 동시 매핑(에뮬):
```
/usr/bin/im.elf  /lib/ld-2.11.2.so(glibc)
/usr/lib/libObj.so  /usr/lib/libMWF.so  /usr/lib/libmintx.so
/usr/lib/viewUnified2.so  /usr/lib/viewUnified8.so
/usr/lib/libScalarDaemon.so  /usr/lib/libsdcom.so
```
→ **처리 서비스(libObj=Rigel WS+GPU+엔진 에이전트, libScalarDaemon=SDCom 서버)와 UI(viewUnified/MINT/libMWF/appFw)가
동일 프로세스·동일 주소공간.** 우리 코드를 이 안에 넣으면 전부 직접 호출 가능. (libsdcom도 로드돼 있으나 인프로세스에선 불필요 — §2.)

### 1.3 "플러그인/등록" 위치
- **엔진 등록**: libObj 안의 엔진 object 에이전트(ObjCamera/ObjRenderer/ObjPlayer/ObjMovieRecorder 등)가
  av-cam 엔진의 인프로세스 대리자. UI는 이들을 **MWF 모델**(model/CAMERA 등)을 통해 간접 호출(§2).
- **Rigel 등록**: libObj의 `rigel::WindowServer(_DI/EGL)`가 **서버 본체**. 순정 UI는 서버와 **같은 프로세스** 안에서
  메인 윈도 `UX_MAIN_WINDOW`(`aUxMainWindow`, libObj .rodata)로 그린다 — 외부 창 생성 프로토콜(Communicator 큐) 불필요.
- **입력 등록**: `InputServant`가 uinput `/dev/input/event0`를 만들고, 전면 뷰가 ABT로 키 라우팅. 인프로세스 UI는
  순정 뷰 핸들러를 가로채거나 event0을 직접 읽는다(§5).

---

## 2. 인프로세스 제어 API (질문 2) — SDCom의 내부 등가물

스톡 UI가 엔진을 모는 방식은 **문자열로 이름 붙은 모델 실행**이다. SDCom처럼 숫자 msgId를 osal 큐로 보내는 게 아니라,
**같은 주소공간의 함수를 직접 호출**한다.

### 2.1 중심 호출 — `requestModelExecute` [S]
libObj/viewUnified 심볼(`re/ida/libObj.so.names.tsv`, `viewUnified2.so.names.tsv`):
```
viewManagerIf::requestModelExecute(char const* name, ulong len, ParamList*)   // libObj .text 0x3948e0
ViewBase::requestModelExecute(char const*, ulong, ParamList*)                 // libObj .text 0x392912
LkmBase::requestModelExecute(char const*, ulong, ParamList*)                  // libObj .text 0x3864d0
CmnViewModelIfWrapper::requestModelExecute(...)  LkmBase::pushEvent(Event*)
AbstractUtilityManager::createRequestModelExecuteEvent(int, ulong, ParamList*)
View::requestModel(int)   ViewBase::requestModel(char const*)   View::setModelRequest(bool)
```
viewUnified는 이들을 `.plt`로 호출(예: `CmnViewFocus::requestModelExecuteSetAfAreaModeFlex(int,int,bool)`).

### 2.2 모델 네임스페이스 — "SDCom 명령표"의 내부판 [S]
viewUnified2 문자열 테이블(`viewUnified2.so.strings.tsv`)의 `model/*` (= `requestModelExecute`의 첫 인자):
```
model/CAMERA  (최다 참조 = 촬영 제어 본체)   model/STILL_REC   model/MOVIE_REC_MANUAL
model/DISPLAY   model/MEDIA   model/HISTOGRAM   model/LED   model/POWER   model/AUTO_POWER_OFF
model/USB_POWER model/REMOTE  model/CLOCK  model/SPEAKER model/CEC model/AIRPLANE model/JET
model/ROOT  model/HANDOVER  model/INH_WRAPPER  model/WATER_DEPTH  model/BROWSE_REPAIR  …
```
→ 설정/촬영/표시/전원/미디어 전 영역이 모델로 노출. **우리 인프로세스 UI는 이 모델들을 직접 `requestModelExecute`**
하면 된다(설정 read/set, 셔터/AF/줌, 라이브뷰 레이아웃, 전원 등).

### 2.3 파라미터/메시지 = MWF [S]
`MWF::ParamList`, `MWF::ObjMsg`, `MWF::Param`, `MWF::ParamStd_Template<T,Type>`, `MWF::ParamArray_Template<…>`,
`MWF::Struct::GetValue`. libMWF(`MWF_Init`)가 이 체계를 등록. 모델 실행 결과/상태도 `ObjMsg::GetParam(uint)`로 읽음.

### 2.4 이벤트
엔진 이벤트는 libObj object 에이전트 → MWF `pushEvent(Event*)` → View 핸들러로 인프로세스 전달. (외부 경로의
SDCom `RegisterEvent 0x0001`과 달리 등록/수신이 전부 콜백.) 수신 ID 의미표는 `re/scalar/event_dispatch.tsv`가 근사.

### 2.5 외부(SDCom) ↔ 내부(model) 대응 — 둘 다 같은 엔진 에이전트로 수렴
| 동작 | 경로 (a) 외부 SDCom | 경로 (b) 인프로세스 model |
|---|---|---|
| 설정 조회/변경 | `0x1002`/`0x1003`/Shift `0x101a…` | `requestModelExecute("model/CAMERA", …)` |
| 촬영/AF/줌 | `0x1006/0x1008/0x102e…` | `model/CAMERA` + `CmnViewFocus::…` 헬퍼 |
| 라이브뷰 레이아웃 | `0x0604/0x0603/0x0606` | `model/DISPLAY` |
| 동영상 | `0x3002/0x3003` | `model/MOVIE_REC_MANUAL`, `model/STILL_REC` |
| 합성 출력 | Rigel `UX_DEBUG_CLIENT`(외부, app-mode 필요) | Rigel `UX_MAIN_WINDOW`(인프로세스, 게이팅 없음) |

> 주의: `requestModelExecute`의 "명령명↔ParamList 레이아웃↔model 라우팅"은 **정적 심볼로 식별**됐으나 각 모델의
> 정확한 인자 스키마는 **디컴파일 + 에뮬 호출로 확정 필요** [?]. SDCom 요청표(`re/scalar/request_params.tsv`)와
> `settings_sdcom_map.tsv`가 값 매핑의 교차검증 소스(둘 다 같은 엔진 에이전트로 수렴하므로).

---

## 3. 주입 메커니즘 랭킹 (질문 3) — RAM-only vs flash, 벽돌위험순

> 공통 전제: 백도어로 **네이티브 루트**(telnet/`nativeExec`, chroot 밖, `NATIVE_DEPLOY.md` §1)를 이미 확보.
> im.elf = glibc `ld-2.11.2.so`(LD_PRELOAD 존중, non-setuid) [V]. 커널 3.0.27_nl-rt106 RT = a6400와 동일 계열.

| # | 메커니즘 | flash 쓰기? | 벽돌위험 | 복구(updater) 영향 | 가역성 | 에뮬검증 | 평가 |
|---|---|---|---|---|---|---|---|
| **(b)** | **ptrace 코드주입 (실행중 im.elf)**: 루트 프로세스가 attach → 스레드 freeze → tmpfs `.so`를 `dlopen`시키고 UI 함수 패치 → detach | **없음**(tmpfs `.so` + ptrace) | **없음**(재부팅 복구) | 없음 | 재부팅/언패치 | ptrace API는 커널 지원 확인 필요 [?], a6400 실기 입증(E0205: im.elf 174스레드 freeze/패치/resume) | **★ RAM-only 1순위**. 개발·런타임 적용 정답 |
| (a-restart) | **LD_PRELOAD + im.elf 재시작**: im.elf kill 후 `LD_PRELOAD=/android/a5k/our.so`로 재기동 | 없음(env+tmpfs) | **높음→중**(im.elf=엔진 글루/Rigel/SDCom 숙주; 재기동 실패 시 화면검정·USB무열거 = `RECOVERY_MAP` e3a류) | **위험**: im.elf 죽은 사이 USB 가젯 소실 → 그 순간 플래그 세우기 불가 | 재부팅 | 미검증, 위험 | 비권장(엔진 숙주를 끊음) |
| (dmode-preload) | **`/setting/mode/dmode` LD_PRELOAD 훅**: 순정 bootin.elf가 dmode 보고 im.elf에 LD_PRELOAD 설정 | **없음**(/setting만, 저티어) | 낮음 | 없음(/setting 유지) | dmode 삭제 | §6에서 검증 중 | 영속 후보(단 preload 경로가 `/usr` 하드코딩 — §6) |
| (preload-usr) | **`/usr/tool/LeakTracer.so`를 우리 `.so`로 교체 + dmode=lt** | **`/usr` flash** | **높음(진짜 벽돌 티어)** | `/usr` 파손 시 im.elf 불가 = USB 복구 상실 | `/usr` 복원(flash) | 금지 | **설계 전용**(standalone 전까지 금지) |
| (ldso-preload) | **`/etc/ld.so.preload`에 우리 `.so` 추가**(a6400 방식) | **rootfs flash**(a5000 `/etc` = ro ext2, 애초에 쓰기 불가 [V]) | 높음 | rootfs 파손 시 복구 상실 | rootfs 복원(flash) | `/etc` ro 확인 [V] | **a5000엔 부적용**(§6) |
| (thread-entry) | **UI 스레드 엔트리 치환**: (b)로 들어간 뒤 Appframework 스레드/뷰 draw 함수 후킹 | 없음(주입 후속) | 없음 | 없음 | 언패치 | (b)와 묶음 | (b)의 **"무엇을 후킹하나"** 단계 — §5 |

**결론**: RAM-only·벽돌0·가역·복구안전은 **(b) ptrace 주입**뿐. LD_PRELOAD는 "im.elf 재시작"이 전제라 엔진 숙주를
끊어 위험. 영속 preload는 전부 플래시(저티어 /setting 하나만 예외, 단 경로 하드코딩 제약 — §6).

### 3.1 ptrace 주입 레시피(설계)
1. 백도어 루트로 tmpfs `/android/a5k/a5kui_hook.so`(glibc, `-march=armv7-a -mthumb -mfpu=vfpv3-d16`) 배포.
2. 루트 로더가 `PTRACE_ATTACH im.elf` → 전 스레드 정지 확인(`/proc/147/task/*`가 traced) → 레지스터 저장.
3. im.elf 안의 `__libc_dlopen_mode`(또는 `dlopen`) 주소로 PC 세팅해 `our.so`를 로드시키고(ivica 방식), 반환 후 복원.
4. `our.so` 생성자가 `requestModelExecute`/`UX_MAIN_WINDOW` 심볼을 **프로세스 심볼테이블/매핑 베이스+오프셋**으로
   해석하고, 스톡 UI 억제 훅(§5)을 건 뒤 우리 UI 스레드를 시작 → `PTRACE_DETACH`.
5. 크래시 안전(CLAUDE.md §1): 우리 스레드는 SIG 핸들러에서 훅 원복·버퍼 해제; 로더는 attach 실패/타임아웃 시 즉시 detach.

> **a6400 대비 주의**: a6400는 커널 text 슬롯까지 패치(E0205, UVC용 `.ko`)하지만, 우리 UI 목적은 **유저랜드 함수 후킹**
> 만으로 족함(ivica의 touch 모드도 `InputService::setTpEventSw`/`PAS_MenuSelectMode::setTouchable` **유저랜드 ptrace 패치**).
> 커널 패치는 범위 밖(벽돌티어).

---

## 4. 스톡 UI 억제 (질문 4) — 런타임, RAM-only

목표: appFw/viewUnified의 **전면 그리기·입력 소유를 멈추되** 엔진·Rigel·SDCom 서버·object 에이전트는 **그대로 살린다**
(혼합 프로세스라 kill 불가 — §1.2). (b)로 들어간 우리 `.so`가 다음 중 하나를 후킹:

1. **뷰 draw 억제**: Appframework 스레드의 뷰 렌더 진입(ViewBase/viewManagerIf의 매 프레임 draw) 또는 `UX_MAIN_WINDOW`
   post 지점을 가로채 **스톡 OSD 그리기를 no-op**로 만들고, 우리 백버퍼를 같은 메인 서피스에 post. → 11% 상시 CPU 회수.
2. **입력 가로채기**: 순정 뷰의 키 핸들러(ScalarView EventHandler류) 앞단에서 이벤트를 우리 UI로 돌림. (또는 event0 병렬 읽기.)
3. **모델만 공유**: 엔진 제어는 억제하지 않음 — 우리 UI가 그대로 `requestModelExecute`로 몬다.

핵심: **인프로세스라 app-mode/라이브뷰 가림 문제가 없다.** (a)에서 YUV 라이브뷰가 SF `DI_ANDROID_APP` 불투명창에
가려졌던 문제(`TRACKB_RUNTIME.md` §4.2)는 외부 경로 한정. 인프로세스는 순정과 같은 `UX_MAIN_WINDOW`에 그려 라이브뷰
평면 위 OSD 합성이 자연스럽다.

> flash로만 가능한 "완전 비로드"(IMDB row 102 appFw 미로드)는 im.elf/`/usr` 교체 = 벽돌티어 [설계 전용]. 런타임 억제로
> CPU 대부분 회수되므로 우선순위 낮음.

---

## 5. 권장 안전 (b) 설계 + uicore 어댑터 매핑 + a6400 대응

### 5.1 권장 설계 (벽돌 0, RAM-only 개발 + 저티어 영속)
```
[설치 APK: dev.oein.a5knative]  (저티어, 삭제=완전원복; 투명 Activity 선택)
   부팅 오토런(§6) → 백도어 루트로:
      루트 로더 a5kinject (glibc, chroot 밖)
        └─ PTRACE_ATTACH im.elf → dlopen(/android/a5k/a5kui_hook.so) → DETACH
[a5kui_hook.so]  im.elf 안에서:
   - 스톡 UI 억제 훅(§4)
   - 우리 UI 루프: 캔버스 → UX_MAIN_WINDOW 인프로세스 post
   - 엔진 제어: requestModelExecute("model/*", MWF ParamList)   ← SDCom 불필요
   - 입력: 순정 핸들러 가로채기 또는 event0 병렬
```
- **SDCom/앱모드/런처 전부 불필요.** 라이브뷰는 인프로세스 `UX_MAIN_WINDOW` 합성으로 자연 노출.
- 상태변경 명령은 EXP 게이트 유지. 포맷/DB 등 위험동작 비활성 유지.

### 5.2 uicore 어댑터 매핑 (`app/native/uicore/include/uicore/platform.h`)
uicore는 플랫폼 헤더 0개. 경로 (b)는 **새 인프로세스 어댑터**로 SDCom 어댑터를 대체:
| uicore 인터페이스 | a5000 경로 (a) 어댑터 | **a5000 경로 (b) 인프로세스 어댑터(신규)** |
|---|---|---|
| `CameraControl` | SDCom(`connect 0x1540432`→`sendSync`) | **`requestModelExecute("model/CAMERA"/…", ParamList)`** 직접 호출; `getSetting/setSetting/shutter/autofocus/zoom`→모델 매핑 |
| `Display` | Rigel `UX_DEBUG_CLIENT`(bionic, app-mode) | **Rigel `UX_MAIN_WINDOW` 인프로세스 post**(ws_hal_* 심볼 프로세스 내 해석) |
| `Input` | event0 + ABT 통지(런처) | 순정 뷰 핸들러 가로채기 / event0 병렬(ABT 불필요) |
| `Storage` | SD `ILCEREMO/` | 동일(SD) — 설정 자체복원으로 Android DB 의존 끊음(§7) |
- `CameraControl`은 **설정 카탈로그 id 단위** 추상화 유지: a5000(b) = model 매핑, a6400 = 기종별 매핑. `platform/a5000-inproc/`,
  `platform/a6400/` 둘 다 이 인터페이스를 구현 → **UI 코어 1벌로 양 기종**.

### 5.3 a6400 대응 (질문 5) — 동일 설계가 양 기종 커버
ivica00/sony-a6400-research가 **실기 입증**한 경로 (b)와 1:1 대응:
| 요소 | a5000 (b) | a6400 (ivica00) |
|---|---|---|
| 주입(RAM) | ptrace attach→dlopen 후킹 | **ptrace 유저랜드 패치**(`touch_native_guarded_v1.so`가 `InputService::setTpEventSw`/`PAS_MenuSelectMode::setTouchable` 패치) + libObj RX 패딩에 mod 링크(`menu_touch_full` 트램펄린) |
| 제어 API | `requestModelExecute("model/*")` + MWF | libObj/libMWF/viewUnified2 인프로세스 호출(동명 라이브러리, 오프셋만 상이) |
| 합성 | Rigel `UX_MAIN_WINDOW`(grm_gles/dmpgles2) | 공통 grm_gles/dmpgles2 경로(Rigel 문자열 없음 → 메인 윈도 직접) |
| 영속 | §6 (APK 오토런 / dmode) | **`/etc/ld.so.preload`(nflasha10, 쓰기가능 config 파티션) → `/setting/*.so` 2개 등재** |
| 백도어 | OM telnet/`nativeExec` | USB Debugger v2(`/tmp_bt`→`/setting`) |
- **설계 공통점**: 둘 다 "루트 백도어 → im.elf에 `.so` 주입(ptrace) → libObj/MWF/viewUnified 인프로세스 후킹 → 영속은
  저티어 config 파티션의 `.so` + 부팅 런처". a5000은 `/etc`가 ro라 ld.so.preload 자리가 없다는 **한 가지 차이**(§6).

---

## 6. 영속 / 오토스타트 (production) — 최저 벽돌티어, 가역, 복구 불변식 보존

> production은 매 전원ON에 우리 UI가 떠야 하므로 **어떤 디스크 쓰기**가 필요. 원칙: **config/setting 티어만(저),
> rootfs/usr/system/kernel 금지(벽돌티어)**, 삭제/복원으로 완전 원복, **updater 도달(복구 불변식) 절대 불변**(`RECOVERY_MAP.md`).

### 6.1 OpenMemories-Tweak는 telnetd를 영속화하지 않는다 [S]
`tools/OpenMemories-Tweak`: `DeveloperActivity`가 **앱 실행 중에** `Shell.exec("busybox telnetd -l sh")`로 띄울 뿐,
부팅 오토런 영속 기록이 **없다**(코디네이터 가정 정정). 즉 a5000엔 OM가 심어둔 "부팅 telnetd"가 없고, 영속은 우리가 설계.

### 6.2 a5000 파티션 쓰기 티어 [V](에뮬 /proc/mounts)
```
/ (dnflasha7 ext2)   ro      ← /etc 포함, 쓰기 불가 (touch /etc/x → Read-only FS) [V]
/usr (dnflasha15 ext2) ro    ← /usr/tool/LeakTracer.so 존재하나 ro [V]
/setting (nflasha2 vfat) rw  ← Backup.bin, mode/, updater/, appfw_infra/ … 쓰기가능·영속 [V]
/tmp (nflasha10 vfat) rw     ← 단 CLAUDE.md 정책상 NAND 쓰기금지 취급
```
→ **a5000엔 쓰기가능 `/etc/ld.so.preload`가 없다**(a6400는 nflasha10 config에 있음). 저티어 영속 후보는 `/setting`과 `/data`(Android 앱).

### 6.3 순정 LD_PRELOAD 훅 — `/setting/mode/dmode` + bootin.elf [S][V]
`bin/bootin.elf`(im.elf를 fork하는 부트 스테이지, interp `/lib/ld-linux.so.3`) 문자열 분석:
- 사용법 `bootin.elf {file=<mode_file>} {dmode=…}`, **기본 mode_file = `/setting/mode/dmode`**.
- dmode에 LeakTracer 플래그가 있으면 im.elf fork 전에 **`LD_PRELOAD=/usr/tool/LeakTracer.so` + `LT_CHECK_PROCESS=1`**
  를 set(평상시엔 `LD_PRELOAD/MALLOC_OPTIONS/LT_CHECK_PROCESS`를 **clearing env**로 지움).
- `change_mode.sh 7` → dmode에 `3L` 기록. gdb모드(`a`/`b`)는 `gdbserver`/`gdb --args im.elf`.
- **의미**: 순정 자체가 **/setting 한 파일로 im.elf에 임의 `.so`를 LD_PRELOAD하는 메커니즘을 매 부팅 실행**한다.
  단 **preload 경로가 `/usr/tool/LeakTracer.so`로 하드코딩** → 우리 `.so`를 바로 못 가리킴.
- **에뮬 검증 완료 [V]**: `/setting/mode/dmode=3L` 기록·sync → `system_reset`(동일 NAND) → **재부팅 후 새 im.elf
  (pid 183)의 `/proc/183/maps`에 `/usr/tool/LeakTracer.so` 로드(3매핑)**, dmode·마커도 생존. 즉 **/setting 한 파일이
  bootin.elf를 통해 매 부팅 im.elf에 `.so`를 LD_PRELOAD하고, 그게 재부팅을 넘어 재적용됨을 직접 입증**(이것이
  코디네이터가 요청한 "재부팅 넘어 재기동되는 /setting 오토스타트"의 실증). **/setting 영속 자체도 확정**(마커 NAND 재독 일치).

### 6.4 영속 옵션 랭킹
| 옵션 | 쓰는 위치(티어) | 가역성 | 복구 불변식 | 평가 |
|---|---|---|---|---|
| **A. 설치 APK + 부팅 오토런 → ptrace 주입** | `/data`(Android, 저티어) + 런타임 ptrace(RAM) | 앱 삭제=완전원복 | 불변(/usr·/system·/etc·NAND·Backup 미접촉) | **★권장**. 플래시 UI영역 0 쓰기, 삭제로 순정 복귀 |
| B. `/setting/mode/dmode=gdb` + 부팅 루트 에이전트가 gdb로 주입 | `/setting`(저) | dmode 삭제 | 불변 | 순정훅 재사용, 단 im.elf가 gdb 대기로 부팅 변형 + 에이전트 기동 주체 필요 |
| C. `/usr/tool/LeakTracer.so`를 우리 `.so`로 교체 + dmode=lt | **`/usr` flash(벽돌티어)** | /usr 복원(flash) | **위험**(/usr 파손=복구상실) | **설계 전용**, standalone 전 금지 |
| D. rootfs `/etc/ld.so.preload` 신설(a6400식) | **rootfs flash(벽돌티어)** + 애초 /etc ro | rootfs 복원 | 위험 | a5000 부적용 |

**A의 "부팅 오토런" 확정 필요 [?]**: 설치 PMCA/Android 앱이 a5000에서 부팅 자동기동되는지(Scalar autorun 또는
`BOOT_COMPLETED`)는 미검증. 안 되면 (가) 사용자가 앱을 1회 실행(앱이 주입) 또는 (나) B(dmode)와 조합. **어느 쪽도 저티어·가역.**

### 6.5 복구 불변식 체크(권장안 A 기준, `RECOVERY_MAP.md`)
- `nflasha1`(updater core)·`nflasha2`(/setting+Backup.bin) **미접촉**. `/usr`/`/system`/rootfs/kernel **미접촉**.
- "플래그 세울 능력" 보존: 순정 USB `SWITCH_MODE`/루트셸 경로 **그대로** → 언제든 updater 도달 가능.
- 삭제 경로: 앱 삭제(+ dmode 썼다면 dmode 삭제) = **완전 스톡**.

---

## 7. 부팅시간 ≤ 스톡 — 수용기준 (user 요구)

> **수용기준(ACCEPTANCE)**: 주입된 우리 UI의 **콜드부트→촬영가능 OSD 게스트시간이 스톡 이하**여야 한다. 측정 =
> 에뮬 A/B(미수정 스톡 vs 주입본), 게스트시간만(`BOOT_TIME.md` §1: 벽시계는 호스트부하로 ~2배 과장 → snsc/klog
> 타임스탬프·`-icount shift=0`). A/B 지표: T_osd(최초 OSD), T_shoot(전체 OSD), BOOT_CMP, CamReady.

### 7.1 스톡 임계경로(게스트초, `BOOT_TIME.md`)
```
S1 로더 1.6 → A 커널 0.30 → B init 0.34 → C im.elf infra 0.46 → D 엔진init+Android DB복원대기 2.46 → E UI첫합성 0.37 → F 라이브뷰YC 4.67[?]
T_osd≈3.6(stage-2)/5.2(총),  T_shoot≈8.7/10.3.  지배구간 D+F=7.1s(82%)=av-cam 엔진(보존).
```
E(UI 첫 합성, appFw/viewUnified)는 0.37s + **유휴 상시 11% CPU**. D의 OSD(3.6s)는 **Android 설정DB 복원(BOOT_CMP)** 대기에 걸림.

### 7.2 주입 훅 발화 시점 & 추가비용
- (b) ptrace 주입 훅은 **im.elf 기동 후(B/C 구간 또는 그 직후)** 발화. **엔진/첫프레임 임계경로(D+F=av-cam) 앞을
  직렬화하지 않는다** — av-cam은 liro 커널태스크로 CPU0-2에서 독립 진행, 우리 ptrace 로더는 백도어 측(병렬).
- 추가비용: ptrace attach/dlopen/심볼해석/훅설치 = **수~수십 ms**(일회성). 우리 UI 첫 프레임까지의 그리기 비용은
  스톡 viewUnified 첫 합성(E 0.37s)보다 **가벼운 UI**(트랙 A 규격, RSS≤6MB)로 설계 → ≤ E.

### 7.3 우리가 스킵하는 스톡 UI 작업 (= 넷 이득)
1. **appFw/viewUnified 전면 init + 상시 11% OSD 재그리기 억제**(§4) → UI 레이어 비용 대체(더 가벼움).
2. **Android 설정DB 복원 대기 우회**: 우리 UI가 SD `ILCEREMO/`에서 **마지막 설정 자체복원**(`NATIVE_PORT_PLAN` §3,
   `BOOT_TIME` §4-2) → D의 Android 대기분(~1.7s[?])을 **첫 프레임 임계경로에서 제거**. (Android 미기동은 단순제거 시
   엔진 BOOT_CMP가 멈추므로(ic1/2/4) **DB복원 대체가 전제** — `BOOT_TIME` §3.)
3. 비필수 서비스 defer(Wi-Fi/NFC/HDMI/오디오) — 제거 아닌 지연 → 콜드구간 CPU/RAM 경합↓(parity 유지).

### 7.4 넷 부팅시간 목표 & 트레이드오프
- **런타임(RAM-only) 주입**: 주입이 im.elf 기동 **이후**라, appFw init 자체는 이미 일부 진행된 뒤 억제 → UI레이어
  순이득은 "상시 11% + 라이브뷰 OSD 재그리기"에 집중. T_osd는 **스톡 동급(≤3.6s)** 달성이 현실적 1차 목표.
- **더 빠르게 = 영속훅으로 더 이른 진입**: dmode/preload로 **im.elf 로드시점(C 구간)**에 바로 들어가면 appFw 전면
  init을 **아예 시작 전에** 가로채 **net-faster** 여지(0.37s + DB대기분 회수). 단 이는 §6-C(flash) 또는 §6-B(dmode)
  경로 → 저티어는 B(dmode), 큰 이득은 C(flash=설계전용).
- **최대 레버 = 스냅샷/WBI·HOT 부팅**(`BOOT_TIME` §4-1, ~1~2s): 주입된 우리 UI까지 올라온 상태를 스냅샷 → 처리동일성은
  스냅샷으로 정의상 보장. **플래시(설계전용)**, standalone 단계.
- **수용기준 판정법**: A/B에서 (a) snsc 엔진 이벤트 시퀀스 동일(처리불변 증명), (b) 우리 T_osd ≤ 스톡 T_osd,
  (c) T_shoot(촬영가능) ≤ 스톡. RAM-only가 (b)를 못 맞추면 §6-B(dmode) 이른 진입으로, 그래도 부족하면 스냅샷으로.

---

## 8. 에뮬레이터 PoC 계획 (질문: 최소 인프로세스 후킹 실증)

M0-inproc(각 단계 fresh NAND 클론, 게스트시간 A/B):
1. **ptrace 가용성**: 루트 로더가 im.elf에 `PTRACE_ATTACH`→`PTRACE_PEEKTEXT`→`DETACH`. 스레드 생존 확인.
2. **dlopen 주입**: im.elf 안 `__libc_dlopen_mode`로 tmpfs `.so` 로드, 생성자 로그 확인. (ivica E0205 레시피 이식.)
3. **인프로세스 Rigel 사각형**: 주입 `.so`가 `UX_MAIN_WINDOW` 경로로 사각형 1개 post — **앱모드 없이**, Diadem 화면 위.
   (스크린덤프 경로 기록.)
4. **model 호출**: `requestModelExecute("model/CAMERA", …)`로 설정 1건 read → SDCom getparams와 값 일치 확인.
5. **스톡 UI 억제**: Appframework draw 후킹 후 11% CPU 감소 A/B(`psnap`).
6. **부팅 A/B**: 주입본 T_osd/T_shoot ≤ 스톡, snsc 엔진 시퀀스 동일(§7 판정법).
7. **영속**: §6-A(앱 오토런) 또는 §6-B(dmode) 재부팅 재주입 1회 성공.

---

## 9. 에뮬레이터 관찰 기록 (이 세션)
- 바이너리 `re-inproc/qemu-inproc`(=shared `qemu-system-arm-sgc1m`), pristine NAND 클론 `re-inproc/run1/t.nand`, 포트 46650–46651.
- **im.elf=pid 147, glibc `/lib/ld-2.11.2.so`** [V]. 한 주소공간에 libObj/libMWF/libmintx/viewUnified2/8/libScalarDaemon/libsdcom 로드 [V].
- mounts: `/`·`/usr`(ext2) **ro**, `/etc` **ro**(쓰기 거부), `/setting`·`/tmp`(vfat) **rw** [V]. `/etc/ld.so.preload` **없음** [V].
- `/usr/tool/LeakTracer.so`(31128B) 존재(bootin LD_PRELOAD 하드코딩 타깃) [V]. gdbserver는 PATH에 없음(bootin가 디버그모드서 기동).
- `/setting` 영속: 마커 `inproc_marker` 기록 → **NAND 백킹파일에서 재독 일치** [V] → 재부팅 넘어 영속 확정.
- `/setting/mode/dmode=3L`(leaktracer) 기록 후 `system_reset` → **재부팅 후 새 im.elf(pid 183) `/proc/183/maps`에
  `/usr/tool/LeakTracer.so` 로드(3매핑) 확인** [V]. dmode=3L·마커 생존 → **/setting-티어 파일이 매 부팅 im.elf에 `.so`를
  LD_PRELOAD하고 재부팅 넘어 재적용**됨을 실증(영속 오토스타트 벡터 확정).

### 9.1 미확정 [?]
- `requestModelExecute` 각 model의 정확한 ParamList 스키마 — 디컴파일 + 에뮬 호출로 확정.
- (해결 [V] §11) ptrace→`__libc_dlopen_mode` 주입이 a5000 커널/im.elf에서 성공함 — 에뮬 실증 완료(핸들 non-NULL, inj.so 4매핑, 내부 그리기, 재부팅 원복).
- 인프로세스 `UX_MAIN_WINDOW` post가 외부 심볼(ws_hal_*/rgl*)을 프로세스 내에서 그대로 쓸 수 있는지(이들은 libObj가
  내부 사용 — 심볼 공개 여부·호출규약 확정 필요).
- 설치 Android 앱 부팅 자동기동(§6-A) 가능 여부.
- (해결 [V]) `/setting/mode/dmode` LD_PRELOAD가 재부팅 후 im.elf에 반영됨 — §6.3/§9 입증. 남은 건 **임의 `.so` 경로
  지정**(현재 `/usr/tool` 하드코딩 → 우리 `.so`로 쓰려면 §6-C(flash) 또는 §6-A/B 조합).
- 부팅 A/B 수용기준 실측(§7).

---

## 10. PoC: 인프로세스 OSD 그리기 — **픽셀 경로 end-to-end 실증** [V] (2026-11-15)

> 목표(코디네이터 M0-inproc §8.3의 핵심): "우리 코드가 OSD 레이어에 알려진 사각형을 그려 **화면에 실제로 도달**함"을
> 에뮬레이터에서 증명. 이번 세션은 **픽셀 경로(질문 1)를 완전 확정**하고, 주입은 **poke 우선** 원칙대로 in-guest
> userspace로 먼저 입증(ptrace/dlopen 체인은 다음 단계). 전부 **RAM-only**(플래시·NAND 쓰기 0). 에뮬 전용.
> 바이너리: 신규 NAND COW 클론, frozen qemu `qemu-system-arm`(HEAD f2d9c2d), `A5K_SHELL`(UART2) + HMP 모니터, `-display none`.

### 10.1 OSD 버퍼 시맨틱 — 실기 레지스터 라이브 확정 [V]
순정 촬영화면 OSD 레이어의 scanout = **AIF ch53**(panel OSD, `RE_DISPLAY_SCANOUT.md` §2). 라이브뷰 중 모니터로 읽은 값:

| 레지스터 | 값 | 의미 |
|---|---|---|
| `0xF3441320` (ch53 +0x20) | `0x8f758000` ↔ `0x8f7ee000` | **OSD 프레임버퍼 base — 더블버퍼**(두 번 읽자 플립). 두 버퍼는 **백투백**(각 0x96000 B; 0x8f7ee000−0x8f758000=0x96000) |
| `0xF3441324` (ch53 +0x24) | `0x500` | bytes/line = **stride 1280** (640px×2B) |
| `0xF344132C` (ch53 +0x2C) | `0x1df` | lines−1 = **480 라인** |
| `0xF3441348` (ch53 +0x48) | `0x500` | signed pitch 1280 (양수 = flip 없음) |
| `0xF3440144` (0x144) | `0x01000233` | bit0=YUV on, **bit1=OSD on** 둘 다 ON |
| `0xF3440404` (0x404) | `0x0` | **16bpp**(bit0=0 → 2 B/px) = **RGBA4444** |
| `0xF344014C`/`0x150` | `0x260026`/`0x2060206` | OSD active window (38,38)–(518,518) [에뮬 모델은 panel OSD를 1:1 그려 미적용] |

**RGBA4444 바이트오더**(에뮬 `cxd90014_vph.c` `vph_compose` + 실측 디코드로 확정): 16비트 LE 워드 `v`에서
`R=v[15:12], G=v[11:8], B=v[7:4], A=v[3:0]`, 각 니블×17. 메모리 바이트: **byte0=(B<<4)|A, byte1=(R<<4)|G**.
알파는 straight-over 합성 `out=(c·a+dst·(255−a))/255`, 니블 0xF=완전 불투명.
→ 불투명 빨강 = `0xF00F`, 불투명 초록 = `0x0F0F`.

**커밋/플러시 불필요** [V]: VPH는 매 vsync(60Hz, IRQ 101/102)에 AIF ch53가 base에서 버퍼를 **재DMA**한다.
Rigel/VDF의 `vdf_if_port_send_update`나 OSD 업데이트 메시지를 호출하지 않아도, **버퍼 메모리에 직접 쓰면 다음 프레임에 표시**된다.
단 순정 UI가 상시 재그리기(§4의 11% CPU)로 같은 버퍼를 덮으므로, 지속 표시는 §4의 스톡 draw 억제가 전제(아래는 경쟁 상태에서 연속 refill로 입증).

### 10.2 주입(poke) — in-guest userspace `/dev/mem` 쓰기 [V]
`emulator/tools/osdpoke.c`(신규): freestanding ARM(libc 없음·raw EABI 시스콜, 순정 glibc 유저랜드서 바로 실행).
`open("/dev/mem")` → `mmap2`(두 버퍼 연속 0x12c000 B, pgoff `0x8f758`) → 두 버퍼 모두에 사각형(x120 y80 w200 h120)을
RGBA4444로 연속 채움(~9s, 빨강/초록 블링크). `a5kshell push`로 tmpfs `/android/a5k/`에 올려 백그라운드 실행.
→ **이것이 주입된 `.so`가 할 바로 그 동작**(프로세스 내에서 매핑된 OSD 버퍼에 직접 write)과 동일한 픽셀 경로.
QEMU 모니터 poke(HMP는 물리메모리 write 명령 없음)보다 충실 — 실제 소프트웨어 경로(userspace→/dev/mem→물리RAM→AIF DMA).

### 10.3 검증 결과 [V] (스크린덤프 `screendump … vph 0`, 640×480 PPM, 사각형 영역 x120–320 y80–200 = 24000px)
- **before**: 사각형 영역 24000px 전부 검정(0,0,0) — 순정 촬영화면(NO CARD/RAW+J/노출스케일/ISO AUTO).
- **after(실행 중)**: 24000px **100% 단색**. 빨강 프레임 center=(255,0,0), 초록 프레임 center=(0,255,0).
  `0xF00F`→정확히 (255,0,0), `0x0F0F`→정확히 (0,255,0) — **바이트오더·알파 합성 수식 실측 일치**.
  블링크 두 위상 모두 포착(빨강 14프레임·초록 10프레임) → 화면이 **우리 라이브 데이터를 추종**(고정 아티팩트 아님).
- **사각형 밖**: before↔after 차이 **0/31560 px** → 순정 OSD(아이콘/텍스트 1829 비검정px) **완전 불변**.
  즉 우리 write는 **국소·가산적**, 순정 UI와 공존(기능 손실 0). 실행 종료 후 순정 OSD가 영역 회수(RAM-only·가역).

### 10.4 주입 체인 도달도 & 안전성
- **픽셀 경로(질문 1)**: ✅ 완전 확정. OSD 버퍼 주소·stride·bpp·RGBA4444 바이트오더·알파·커밋불요·더블버퍼 전부 [V].
- **주입(질문 2)**: poke 단계 ✅ = **in-guest userspace /dev/mem 직접 쓰기**(주입 `.so`의 등가 동작). ⬜ **ptrace attach→`__libc_dlopen_mode`로
  im.elf 안에 `.so` 로드** 체인은 미착수(다음 단계) — §3.1 레시피, a6400 실기 입증 있음.
- **복구/부팅 안전(질문 3)**: ✅ 쓰기 대상은 **tmpfs `/android/a5k/`(전원off 소멸) + `/dev/mem`(RAM)**뿐. **플래시/NAND/`/setting`/`/usr`/`/etc` 미접촉** →
  updater 도달 불변식 보존, 벽돌 0, 재부팅 완전 원복. 주입은 **부팅 완료(촬영화면 up) 후** 실행 → 부팅 임계경로 비간섭(부팅 A/B는 §7/§8 별도 마일스톤).

### 10.5 다음 구체 단계
1. **ptrace/dlopen 체인 실증**(§8.1–8.2): 루트 로더가 im.elf `PTRACE_ATTACH`→스레드 freeze→`__libc_dlopen_mode`로 tmpfs `.so` 로드→생성자 로그.
   그 `.so`가 ch53 base를 프로세스 내에서 매핑(혹은 Rigel 메인윈도 버퍼 심볼 해석)해 **im.elf 주소공간에서** 동일 사각형을 그림.
2. **스톡 draw 억제**(§4): Appframework 뷰 draw/`UX_MAIN_WINDOW` post 후킹 → 지속 표시 + 11% CPU 회수 A/B.
3. **model 호출**(§8.4): `requestModelExecute("model/CAMERA", …)` 설정 1건 read ↔ SDCom getparams 교차검증.

### 10.6 산출물
- `emulator/tools/osdpoke.c` — 재현용 PoC(빌드/실행 레시피는 파일 헤더). (emulator 저장소)
- 스크린샷 증거: before(검정 영역)·초록 사각형 프레임 — 세션 스크래치(저장소 미커밋).

---

## 11. PoC 2: in-process `.so` 주입 — **ptrace→dlopen→im.elf 내부 그리기 실증** [V] (2026-11-16)

> §10에서 픽셀 경로와 외부 poke를 확정했으니, 이번엔 **route (b)의 핵심**: 우리 `.so`를 **실행 중 im.elf 주소공간 안에**
> ptrace로 로드시켜 **im.elf 내부에서** 동일 사각형을 그림(= osdpoke의 결과를 이번엔 im.elf 프로세스 안에서). a6400 공통 경로.
> 전부 RAM-only. 에뮬 전용(frozen qemu, 신규 NAND COW 클론, `A5K_SHELL`+HMP, `-display none`).

### 11.1 주입 체인 (실측) [V]
`emulator/tools/a5kinject.c`(freestanding 아님 — **a5k-cross glibc static** ARM 로더) + `emulator/tools/inj_so.c`(glibc ARM `.so`).
1. **dlopen 주소 확정**: 게스트 rootfs libc(`/lib/libc-2.11.2.so`) `readelf -s` → `__libc_dlopen_mode` st_value `0x000c37cd`
   (**LSB=1 = Thumb**; 코드 offset `0xc37cc`), FUNC/GLIBC_PRIVATE. 첫 LOAD(vaddr 0) → 원격주소 = **libc text base + 0xc37cc** [S].
   libc base는 **부팅마다 다름**(관측: 0x4253000 / 0x4230000) → 로더가 `/proc/<pid>/maps`에서 **실시간 해석**.
2. **attach/hijack**: `PTRACE_ATTACH`(Yama ptrace_scope 없음, root) → `wait` 정지 → `PTRACE_GETREGS`(ARM 18워드: r0–15·cpsr·orig_r0) 저장 →
   원격 스택에 `.so` 경로 문자열 `POKETEXT` → 레지스터 세팅 `{r0=경로, r1=RTLD_NOW(2), sp=정렬스크래치, lr=0, pc=0xc37cc(Thumb), cpsr|=T(0x20)}` →
   `SETREGS`→`CONT`.
3. **복귀 포착**: 호출이 `lr=0`로 복귀→`pc=0`에서 **의도된 SIGSEGV**→로더 `wait` 포착. `GETREGS` r0 = dlopen 핸들.
   **원래 레지스터 복원**(`SETREGS`)→`DETACH` → im.elf는 하이잭 전 상태로 재개.
4. **엔진 비차단**: `.so` 생성자는 **worker 스레드만 `pthread_create`로 띄우고 즉시 반환** → 하이잭된 im.elf 스레드는 dlopen에서 바로 빠져나와 엔진 비차단.
   worker가 `/dev/mem` mmap 후 OSD 더블버퍼에 사각형 그림.

관측(실측): `a5kinject: returned, stopsig=11 pc=0x0 handle=0x268e48` → **핸들 non-NULL = dlopen 성공**, SIGSEGV-복귀 설계대로, clean detach.

### 11.2 im.elf 내부 실행 증거 [V]
- **in-process 로그**: `.so` 생성자/worker가 im.elf 안에서 쓴 `/android/a5k/inj.log` = `ctor in im.elf, spawning worker` →
  `worker mapped, drawing` → `worker done`.
- **매핑 증거**: `/proc/<impid>/maps`에 `/android/a5k/inj2.so` **4매핑(r-xp text / ---p gap / r--p rodata / rw-p data)** → 우리 `.so`가 im.elf 주소공간에 적재됨.
- **화면 증거**(`screendump vph 0`): 사각형 영역(x340–520,y300–410=19800px) **100% 파랑**(center=(0,0,255)) — `0x00FF`→정확히 (0,0,255)(§10.1 바이트오더 재확인).
  순정 UI(촬영/갤러리 뷰의 썸네일·아이콘)는 **정상 렌더 지속**, 우리 사각형이 그 위에 **합성**(공존·기능손실 0). osdpoke(외부 프로세스)와 **동일 결과를 im.elf 내부에서** 재현.
- **엔진 건전성**: im.elf pid 생존·uptime 연속(81s→138s, oops/리부트 없음). oops(`liro-kliro_113`)·크래시 **미발생**.

### 11.3 복구 불변식 — 전원 재부팅 교차확인 [V]
`system_reset`(콜드) → 재부팅 후: `/android/a5k` 소멸(tmpfs), 새 im.elf(pid 147) `maps`에 **inj2.so 0매핑**,
순정 촬영화면 **동일 복원**(nonblack 16599px = 주입 전과 일치, 사각형 0). → **주입 전체가 RAM-only(tmpfs `/android/a5k` + `/dev/mem` + dlopen 매핑),
플래시/NAND/`/setting`/`/usr`/`/etc` 미접촉, 재부팅 완전 원복**. updater 도달 불변식 보존, 벽돌 0.

### 11.4 주입 체인 도달도 (갱신)
- 픽셀 경로(질문 1): ✅ (§10). **주입(질문 2): ✅ ptrace→`__libc_dlopen_mode`→im.elf 내부 `.so` 그리기 완전 실증**(이번 세션). 외부 poke·내부 dlopen 둘 다 [V].
- 안전(질문 3): ✅ RAM-only·재부팅 원복·엔진 비차단·복구 불변식 보존(재부팅 교차확인 포함).
- a6400 대응(§5.3): 동일 모양(ptrace 유저랜드 주입 + libObj/MWF/viewUnified 인프로세스) — a5000은 libc base 실시간 해석·Thumb dlopen만 기종차.

### 11.5 미확정/주의 [?]
- **warm-resume 후 OSD 재구성**: idle 5분 전원절약(factor 8 warm off) 후 `system_wakeup`시 OSD 레이어(0x144) 재활성이 지연되는 관측 1건 — 주입과 무관(순정 warm-resume 경로, `RE_WARM_RESUME` 영역). 재확인 필요.
- 하이잭 대상이 **main thread 1개**(그 순간 블로킹 syscall 중이면 복원 후 재시작 의미론 미세 위험) — 이번 관측에선 무해. 실사용 로더는 안전 스레드 선택·타임아웃 detach 권장(§3.1-5).
- 다음: **스톡 draw 억제**(§4, worker가 `UX_MAIN_WINDOW`/viewUnified draw 후킹→지속표시+11% CPU 회수), `requestModelExecute` 내부호출(§2) 실측.

### 11.6 산출물
- `emulator/tools/a5kinject.c` — ptrace .so 주입 로더(빌드/실행·dlopen offset 유도 레시피는 파일 헤더). (emulator 저장소)
- `emulator/tools/inj_so.c` — im.elf 내부 그리기 payload `.so`. (emulator 저장소)
- 스크린샷 증거(파랑 사각형이 순정 UI 위에 합성·재부팅 클린복원) — 세션 스크래치(저장소 미커밋).

---

## 12. 순정 draw 억제 + CPU 회수 — **in-process 훅 실증 + A/B 측정** [V] (2026-11-16)

> §4의 목표: 주입된 worker 안에서 순정 draw를 억제해 (1) 우리 OSD가 프레임 넘어 **지속 표시**, (2) viewUnified/appFw의
> 상시 CPU **회수**. RAM-only. 에뮬 전용(frozen qemu, 신규 NAND 클론, `A5K_SHELL`+HMP, `-display none`, 단독 실행).
> 결과: **CPU 회수·지속표시 완전 입증(목표 초과)**, 단 **런타임 가역성은 조건부**(아래 §12.4) — 정직 보고.

### 12.1 훅 지점 확정 (질문 1) [S]
순정 OSD가 화면(VPH)에 닿는 **유일 합류점** = libObj `vdf_if_port_send_update` (RE_DISPLAY_SCANOUT §5):
- 디컴파일 `re/ida/libObj.so.c`에서 **`sub_5DC3D4(const void* rec, int n)`** 확정(함수 내 에러문자열
  `"DMPNative_Error vdf_if_port_send_update(update_osd_record*, int)"` 일치). libObj vaddr **0x5DC3D4**, **Thumb**(libObj 전 FUNC LSB=1).
- 본문: `osal_wai_sem_tmo(…,-1)`(세마포 대기) → 메시지버퍼 획득 → `type=5`·OSD레코드 memcpy·count → `send`(sub_5DB934) → 세마포 해제, `return 0`.
  **void-류 fire-and-forget**(호출자 set_data는 반환값 무시). 엔진/라이브뷰 YUV 핀은 **별도 경로**(VphUpdateYuvCall)라 무관.
- 런타임 주소 = **libObj r-xp base + 0x5DC3D4**(base 부팅마다 상이, 관측 0x05b15000 → target 0x060f13d4). `/proc/self/maps`에서 실시간 해석.

### 12.2 구현 (질문 2) — in-process 코드패치, 가역설계
`emulator/tools/supp_so.c`(a5kinject로 im.elf에 주입). worker:
1. `/dev/mem`로 OSD 더블버퍼 mmap → 사각형(마젠타 `0xF0FF`→(255,0,255)) **1회만** 그림(지속성 테스트: 리필 없음).
2. **패치**: libObj text 페이지 `mprotect(RW)` → target 2바이트 저장(`b0 b5`=`PUSH{r4,r5,r7,lr}`) → **`70 47`(Thumb `BX LR`)** 기록 → `__clear_cache` → `mprotect(RX)`.
   → `send_update`가 즉시 반환 = 순정 OSD 레코드가 VDF로 **전송 안 됨** → VPH scanout이 마지막 버퍼에 **고정** → 우리 사각형 **지속**.
3. HOLD(22s) 유지 후 원바이트 **복원**. (패치/복원 전부 im.elf 자기 메모리 — 플래시 미접촉.)

### 12.3 A/B 측정 (질문 3) — im.elf 스레드별 CPU (`/proc/147/task/*/stat`, 게스트 uptime 기준, HZ=100)
| 상태 | THREAD_INFRA0 | Appframework | grm_dn_vdf_if | im.elf 합 |
|---|---:|---:|---:|---:|
| **baseline(순정 UI 활성)** | **22.0%** | **19.4%** | 1.1% | **~42.8%** |
| **suppressed(send_update no-op, 패치 LIVE)** | **<0.3%** | **<0.3%** | — | **~0.5%** |
| reboot(클린 복원) | 27.7% | 25.0% | 1.1% | ~54% |

→ **순정 UI 두 축(appFw redraw + INFRA0 합성)이 둘 다 ~0으로 수렴**. 플립을 끊자 **더블버퍼 백프레셔**로 compose→composite→flip
파이프라인 전체가 블록 → **~42% 회수**(§3.1의 ~11% 목표를 크게 초과; appFw뿐 아니라 그와 **직렬화된** INFRA0 합성까지 회수).
(%절대값은 호스트부하로 런마다 변동 — 불변식은 "두 UI 스레드 20%+ → <0.3%".) 지속성: 마젠타 사각형 **1회 그림·리필 없이** 10s+ 두 프레임 100% 유지(scanout base 0x8f7ee000 고정), 순정 OSD(촬영화면)는 **그 프레임에서 정지**한 채 우리 사각형이 위에 합성.

### 12.4 안전/무회귀 (질문 4) — 정직 보고
- **엔진·라이브뷰·입력 무손상** [V]: 억제 중 VM running, **oops 없음**, SYSV 프레임카운터 전진(0x28dc→0x28fb), 0x144 bit0(YUV)=1 유지, I-InputRepeat 생존. 억제는 **UI 렌더 경로에 국한**.
- **콜드 리부트 완전 원복** [V]: `system_reset` 후 마젠타 0%, 순정 촬영화면 복원, libObj 바이트 플래시서 재적재, 두 UI 스레드 정상. **RAM-only·플래시 미접촉**.
- ⚠️ **런타임 가역성 조건부(핵심 한계)**: 원바이트 복원·키주입을 해도 **순정 UI가 재개되지 않음**. 원인 = `send_update` no-op이 **OSD 플립/세마포 핸드셰이크**(`osal_wai_sem_tmo`)를 **프레임 중간에 굶김** → appFw/INFRA0 compose 스레드가 영구 블록. 언패치는 **누락된 플립을 소급 재생 못 함**. ⇒ 이 훅은 **FREEZE**(CPU·지속성 최상)이나 런타임은 **리부트로만 복구**, detach-가역 아님.
  - 코디네이터 규칙("상태머신 흔들면 강행 말고 기록") 준수: 엔진 불안정은 아니나 **UI 렌더 경로 데드락**을 정확히 기록. 강행 안 함.

### 12.5 선택한 훅 평가 & 더 안전한 대안
- **채택 훅**: `vdf_if_port_send_update` no-op. **장점**: 주소 확정·단일함수·엔진무관·**CPU 완전회수+지속성 즉증**. **단점**: 런타임 비가역(플립 데드락).
- **detach-가역 production 훅(권장 다음)**: **appFw의 redraw 트리거를 상류에서 게이트**(플립 핸드셰이크를 끊지 않고 appFw가 **일거리 없이 idle**). 이러면 플립이 프레임경계에서 깨끗이 멎어 데드락 없이 재개 가능. 단 **appFw.so 디컴파일 필요**(현재 `re/ida`에 없음 — RE 선행). viewUnified의 `PAS_MenuSelectMode::update`/`PAS_MenuListAccessPoint::update`는 메뉴 한정이라 유휴 redraw 트리거 아님(후보 아님).
- **가역·CPU미회수 대안**(즉시 가능): send_update를 **주소-리다이렉트**(핸드셰이크 유지 + OSD 레코드의 타깃버퍼를 우리 버퍼로 치환) → 파이프라인 계속 돌아 **detach-가역**·지속표시, 단 CPU 미회수. 트레이드오프 기록.

### 12.6 산출물
- `emulator/tools/supp_so.c` — §4 억제 PoC(패치/측정/가역 캐비엇은 파일 헤더). (emulator 저장소)
- CPU A/B·스크린샷(마젠타 사각형이 정지된 순정 OSD 위에 지속) — 세션 스크래치(저장소 미커밋).

### 12.7 다음 단계
1. **appFw redraw 트리거 RE**(appFw.so 디컴파일 확보) → 상류 게이트로 **detach-가역 + CPU회수** 달성.
2. **§2 `requestModelExecute("model/CAMERA", …)` 내부호출** 실측 → 설정 read ↔ SDCom getparams 교차검증(우리 UI가 엔진을 모는 경로).

---

## 13. 인프로세스 카메라/엔진 제어 (`requestModelExecute` 경로) — RE + **안전 READ 실증** [V] (2026-11-16)

> §2 목표: 주입 UI가 **순정과 동일한 인프로세스 경로**로 엔진 상태를 읽고(그다음 몰고), SDCom 경로와 등가임을 보임.
> 이번 세션: **경로 전체 RE 확정 + 가장 안전한 인프로세스 READ를 실기 성공**(엔진/뷰 상태를 im.elf 내부에서 읽어 라이브 추종 확인).
> 가역 SET(파라미터 변경)은 **두 블로커**(아래 §13.4)로 **강행하지 않고** 레시피·안전경로 기록. RAM-only, 에뮬 전용.

### 13.1 인프로세스 제어 API — 심볼/주소/전역 싱글턴 [S]
`re/ida/libObj.so.c`·`viewUnified2.so.c` 디컴파일로 확정(libObj 전 FUNC = Thumb):
| 함수 | libObj vaddr | 역할 | 비고 |
|---|---|---|---|
| `viewManagerIf::requestModelExecute(name,cmd,ParamList*)` | **0x3948E0** | 모델 실행(SET/명령) | 전역 `dword_EEDDDC`(**AbstractUtilityManager** 싱글턴) → `createRequestModelExecuteEvent(mgr,id,cmd,list)` → 이벤트 펌프 |
| `viewManagerIf::getModelInfo(name, numList, &outList, 0, …)` | **0x39485C** | 모델 파라미터 **READ** | 전역 `dword_EEDDD8`(**ViewManager** 싱글턴) → `ViewManager::getModelInfo(id, numList, outList)` |
| `viewManagerIf::getCurrentFuncEventMode()` | **0x394924** | 현재 func/event 모드 READ | `sub_70A734(dword_EEDDD8)`: 뷰스택 top의 가상함수 getMode. **인자 없음** |
| `LkmBase::requestModelExecute` / `ViewBase::…` | 0x3864D0 / 0x392912 | 뷰 경유 래퍼 | 내부서 `this[2]`→`sub_388D44`→동일 싱글턴 경로 |

**핵심**: requestModelExecute/getModelInfo는 **프로세스 전역 싱글턴**(`dword_EEDDDC`/`dword_EEDDD8`)을 내부서 읽는다 → **살아있는 뷰 객체(this) 불필요**. 모델명은 문자열(`"model/CAMERA"` 또는 코드명 `"@M00B"`)을 넘기면 `IdGenerator::Get`이 id로 해시.

### 13.2 ParamList/IntParam ABI (SET 레시피) [S]
순정 SET 레퍼런스 = `CmnViewFocus::requestModelExecuteSetAfAreaModeFlex`(viewUnified2 0x1808E0):
```
v9 = operator new(8); ParamList::ParamList(v9)             // libObj 0x70912E, ParamList=8B
p  = operator new(0x10); sub_EA468(p, intval)             // IntParam ctor(0x10B) + 값
ParamList::add(v9, 0x153, p)                              // libObj 0x7095D6: add(id, ParamBase*)
ParamList::add(v9, 0x10E, p2) … (0x443,0x2F4 …)
ViewBase::requestModelExecute(view, "@M00B", 0x16, v9)    // 모델 "@M00B"(AF area), cmd 0x16
ParamList::~ParamList(v9)                                  // libObj 0x7091F8
```
READ 레퍼런스 = `sub_17F798`(viewUnified2): `ParamList pl; getModelInfo("model/CAMERA", numList, &pl, 0, pl[0]); p=sub_EA4B0(&pl, 0x12C); val=sub_EA4A8(p);` → model/CAMERA의 param **0x12C**(캡처모드) 정수 read.
- 관련 주소: ParamList ctor 0x70912E / dtor 0x7091F8 / add 0x7095D6 / clear 0x709066; nullptr ParamList 호출도 존재(`@M0E9`,cmd 0xD04/0xD05).

### 13.3 **인프로세스 READ 실증** [V] — `emulator/tools/mdl_so.c`
a5kinject로 주입 → worker가 libObj base 실시간 해석 → `(base+0x394924)|1`(Thumb) 호출 = `getCurrentFuncEventMode()`를
im.elf **내부에서** 1초 간격 20회 호출. 중간에 외부로 MENU 키 주입.

| 구간 | 관측 funcEventMode | 화면 상태 |
|---|---:|---|
| t0–5 (촬영화면) | **8** | shooting |
| t6–11 (MENU 진입) | **23** | menu |
| t12–19 (MENU 복귀) | **8** | shooting |

→ **우리 주입 코드가 순정 ViewManager 싱글턴을 통해 라이브 func/event 모드를 읽고, 그 값이 실제 UI/엔진 상태(MENU 진입/복귀)를
정확히 추종**. 엔진 건전성: VM running, **oops 없음**, SYSV 프레임클럭 전진(0x9d1→0x9eb), im.elf 생존, 촬영화면 유지.
코드패치 없음(순수 read) → §4보다 더 안전, tmpfs만 → 리부트 완전 원복(자명).

### 13.4 SET + SDCom 교차검증 — **블로커 & 안전경로** (강행 안 함)
가역 파라미터 SET(requestModelExecute)과 SDCom 0x1002 교차검증은 **두 블로커**로 이번 세션 보류:
1. **getModelInfo arg3 = 모델별 `PrmNumberList`**(레퍼런스의 `&byte_9[6]`) — viewUnified2의 IDA-로컬 전역이라 **주소 확정 불가**. 틀리면 크래시. (requestModelExecute SET은 ParamList만 필요해 이 블로커는 없으나 2번이 남음.)
2. **스레드 안전성**: 순정은 이 호출들을 **Appframework 스레드**에서만 구동(전역 싱글턴·이벤트펌프 단일스레드 가정). **READ**(getCurrentFuncEventMode)는 유휴 상태서 worker 스레드 호출이 성공했지만, **SET**(이벤트 큐 변이)은 appFw 펌프와 **레이스** 위험 → 크래시/상태오염 가능.
- **안전경로(권장)**: SET을 **worker 스레드가 아니라 Appframework 스레드 컨텍스트에서** 실행(appFw 함수 1개를 후킹해 우리 ParamList+requestModelExecute를 그 스레드에서 호출) → 프레임워크 락/컨텍스트 유효. 그 뒤 SDCom `0x1002`(getparams, 어댑터 param 267 등)로 before/after 교차검증. **flash/Backup 영구기록 금지** 유지.

### 13.5 model ↔ SDCom 등가성 [S]
- **model 경로**(인프로세스): requestModelExecute → AbstractUtilityManager(`dword_EEDDDC`) 이벤트 → 엔진 object 에이전트(ObjCamera 등).
- **SDCom 경로**(외부/인프로세스): ScalarDaemon → 동일 엔진 object 에이전트.
- getModelInfo가 읽는 값 = ViewManager(`dword_EEDDD8`)가 보유한 **UI가 표시하는 그 상태**(§13.3서 MENU 추종으로 실증) = SDCom 0x1002가 반환하는 동일 엔진 상태. ⇒ 두 경로는 **같은 엔진 에이전트로 수렴**(doc §2.5 교차검증 소스).

### 13.6 산출물
- `emulator/tools/mdl_so.c` — 인프로세스 엔진-상태 READ PoC(주소·레시피는 헤더). (emulator 저장소)
- `mdl.log`(8→23→8) — 세션 스크래치.

### 13.7 다음 단계
1. **appFw 스레드 후킹 → requestModelExecute SET**(가역 파라미터) → SDCom 0x1002 before/after 교차검증(안전경로 §13.4).
2. 또는 **§4 appFw 가역 억제**(상류 트리거 게이트) / **§3 autostart**(부팅 재주입, §6-A/B).

---

## 14. appFw 훅 지점: redraw-gate + appFw-thread 콜백 — **정적 RE 확정** [S] (2026-11-16)

> §12.5(가역 억제)·§13.4(안전 SET)의 **두 블로커가 모두 "appFw.so 디컴파일 필요"로 수렴**(§12.7-1, §13.7-1). 본 절은 그 RE를 완료한다.
> RAM-only·플래시 미접촉·실기 미사용. 모든 주장은 libObj.so 주소/심볼로 뒷받침, 추정은 `[?]`.

### 14.0 ⭐ 전제 정정 — appFw.so는 별도 파일이 아니다 (libObj.so에 정적 포함) [S][V]
`appFw.so`는 **디스크에 존재하지 않는다**(펌웨어 `/usr/lib` 155개 .so 전수조사: `appFw.so`/`AppConfig.so`/`AdjChkConfig.so`/
`Application_System_Init` 문자열 **0건**; im.elf=23KB 씬 로더라 미포함). 이는 IMDB 스테이지 테이블(row 102 `file=appFw.so
init=Application_System_Init`)의 **논리 모듈명**일 뿐 — 실체 코드는 **libObj.so 안에 정적 컴파일**되어 있다.
- 근거: `emulator/docs/RE_UI_BRINGUP.md` L6 — *"all of appFw/grm/gs/uxengine/Rigel/dmpNative/DMP-EGL is statically inside libObj.so"*,
  동 §2(appFw = `Application_System_Init` @ **libObj 0x7029E8**). libObj `.rodata`에 `"appFw.so"` 문자열(0xB288EC) + `IdSoTable::add(Instance,-2,"appFw.so",nullptr)`.
- ⇒ **appFw 디컴파일은 이미 보유**(`re/ida/libObj.so.c`, 59MB). 별도 바이너리 확보/디바이스 pull 불필요. 아래 모든 주소 = **libObj.so vaddr, 전부 Thumb**(libObj 전 FUNC LSB=1, §12.1 재확인).

### 14.1 appFw 스레드 메인 / 이벤트 펌프 (질문 1 — 안전 SET용 appFw-thread 콜백) [S]
**진입 체인**(im.elf `main` a_thread → libObj): `Application_System_Init(0x7029E8)` → `osal_create_thread`(디스크립터 @0xEE8D68,
field 0x83=appFw `me`) → 스레드 엔트리 **`sub_702978`**(0x702978) → uxengine 루트윈도(`sub_4F03C8`) → **AppSystem ctor `sub_7031A8`**(0x7031A8,
gate A 세마포 0x83037A) → **메인 루프 `sub_702E3C`**(0x702E3C) → `sub_70332C`.

**이벤트 펌프 = `AppSystem::Main` = `sub_702E3C` @ libObj 0x702E3C** (단일스레드 이벤트 펌프 — §13.4가 말하는 그 펌프). 루프 골격:
```
while (sub_702E1C(a1)==0) {            // 종료조건=EVENT_APP_SYSTEM_END
  blog "sleep"; sub_702B54(v4);        // 내부 파이프 poll (블록, EventManager::push로 깨어남)
  blog "wake";  sub_70A2DA(a1[3]);     // ← 매 iteration 무조건 1회, appFw 스레드
  // 이벤트 디큐·디스패치 루프:
  while ((v5=sub_702BE0(a1)))          //   큐에서 Event 꺼내
     { sub_704110(a1[4]); sub_702C60(a1, ev); }   // ← sub_702C60 = Event 디스패처(appFw 스레드)
  // (IsViewModeDiadem && 드로 게이트) → updateWidget → sub_70CD64(a1[2]) [드로, §14.2]
}
```
관련 주소: 루프가드 `sub_702E1C`=0x702E1C, poll `sub_702B54`=0x702B54, 이벤트 디큐 `sub_702BE0`=0x702BE0(→`EventManager::sub_DEA58`),
**이벤트 디스패처 `sub_702C60`=0x702C60**, 매-iteration 처리 `sub_70A2DA`=0x70A2DA.

**권장 훅(안전 SET): `sub_702C60` @ 0x702C60 트램펄린**(또는 매-iteration `sub_70A2DA` @ 0x70A2DA). 둘 다 **appFw 스레드에서만** 실행 →
전역 싱글턴(`dword_EEDDDC`/`dword_EEDDD8`) 변이가 펌프와 **직렬화**되어 §13.4-블로커2(레이스) 해소.
- 레시피: worker 스레드가 공유메모리에 `ParamList`(§13.2 ABI) + `g_pending=1` 세팅 → 펌프를 깨움(기존 키주입 경로로 임의 이벤트 1개 주입, 또는 `EventManager::push`) →
  트램펄린이 appFw 스레드 위에서 `g_pending` 확인 시 **`viewManagerIf::requestModelExecute`(libObj 0x3948E0, §13.1)** 실행 후 `g_pending=0`, 원함수로 tail-call.
- §13.3 READ(`getCurrentFuncEventMode` 0x394924)는 **코드패치 없이** worker 스레드에서 성공했으나, SET은 이벤트큐 변이라 **반드시 이 스레드 컨텍스트** 필요 — 그 요구를 본 훅이 충족.

### 14.2 스톡 redraw 트리거 (질문 2 — §12 데드락 없는 가역 억제) [S]
**드로 체인**(모두 appFw 스레드, libObj): 펌프 `sub_702E3C` 드로블록 → **`sub_70CD64`**(0x70CD64) → **`sub_4EFADE`**(0x4EFADE, dirty-게이트 프레임 post)
→ `sub_66D442`(0x66D442, dirty 테스트) → (dirty면) eglSwapBuffers 경로 → begin_swap → … → `vdf_if_port_send_update`(§12의 `sub_5DC3D4`).

`sub_70CD64`(=ScalarAppfwIF 드로) 본문:
```
updateFrame   : while(sub_505920(gfx)) sub_50641E(gfx)
notifyDraw    : sub_5063D0(gfx)
drawFramebuffer: sub_704C0C(GraphicsEngine)      // ux gfxeng GL 드로
                 sub_4EFADE(a1[4])               // ← Rigel WindowClient post (프레임 인큐)
```
`sub_4EFADE` 본문(0x4EFADE): `if (sub_66D442(dirtyMgr)!=0) { dirty-rect 계산 → sub_4EEF3C → sub_53229C→sub_5323B4→sub_53217C (post/swap) }
**else { sub_53233C(); }**` — **not-dirty면 스톡이 스스로 인큐를 건너뛴다**(유휴 프레임의 정상 경로). `sub_66D442` = `*(u8*)sub_66D39E(a1)` = dirty 개수(num_of_dirty).

**게이트 지점(권장 순):**
1. **`sub_70CD64` @ 0x70CD64 → no-op**(`BX LR` = `70 47`). 펌프 루프·이벤트 디스패치·상태머신은 전부 **계속 실행**, GL 드로+post만 생략.
   **CPU 최대 회수**(appFw 드로 + 그와 직렬된 Rigel WS compose 둘 다 유휴 — §3 WS는 클라이언트 dirty 때만 compose, periodic refresh 없음).
2. **`sub_66D442` @ 0x66D442 → `return 0`**(`00 20 70 47` = `movs r0,#0; bx lr`). **최고 충실도**: `sub_4EFADE`가 항상 스톡의
   `sub_53233C()`(=유휴 "그릴 것 없음") else-분기를 타게 함 — 스톡 스스로 매 유휴 프레임에 타는 바로 그 경로. GL 드로(sub_704C0C)는 남아 CPU 회수는 (1)보다 적음.
3. `sub_4EFADE` @ 0x4EFADE → no-op (post만 제거, 중간).

**왜 §12 데드락을 피하나:** §12는 `vdf_if_port_send_update`(sub_5DC3D4)를 no-op — **begin_swap 하류**(이미 UpdateManager mutex + InternalCmd 세마포 획득,
MSGID_PIN_SEND_COMP 대기 상태)라 완료메시지 영구 미도달 → 락 영구보유 → 2번째 eglSwapBuffers 영구블록(GATE C, "No timeout exists"). 언패치도 **누락 플립 소급재생 불가** → 리부트만 복구.
본 상류 게이트는 억제 프레임에서 **eglSwapBuffers/begin_swap을 애초에 호출하지 않음** → UpdateManager mutex=free, pending slot=empty, vdf_if 세마포(EEF15C 등)=rest. 파이프라인이 **마지막 완료 프레임 경계에서 정지** →
언패치 시 다음 iteration이 dirty redraw로 정상 재개(소급재생 불필요) = **detach-가역**. (§12.5 "상류 게이트" 권장안의 실체.)

### 14.3 각 훅 레시피 (후속 code agent용)
공통: libObj 로드베이스 = `/proc/self/maps`의 `libObj.so` **r-xp** 매핑 시작주소(부팅마다 상이, §12 관측 0x05b15000) + vaddr. **호출**은 Thumb이므로 함수포인터에 `|1`.
텍스트 패치는 `mprotect(page,RW)` → 바이트 기록 → `__clear_cache` → `mprotect(page,RX)`(§12.2 검증절차 동일). 전부 im.elf 자기 메모리 = **플래시 미접촉·RAM-only·리부트 완전원복**.

| # | 목적 | 지점 | vaddr | Thumb | 방식 | 비고 |
|---|---|---|---|---|---|---|
| 1 | appFw-thread SET 콜백 | `sub_702C60`(이벤트 디스패처) | 0x702C60 | Y | **트램펄린**(원함수 보존 호출) | or `sub_70A2DA`@0x70A2DA(매-iter). pending-flag+펌프 wake 필요 |
| 2a | redraw 게이트(CPU회수) | `sub_70CD64`(드로) | 0x70CD64 | Y | **패치** 2B `70 47`(BX LR) | 가역: 원2B 복원 |
| 2b | redraw 게이트(최고충실) | `sub_66D442`(dirty 테스트) | 0x66D442 | Y | **패치** 4B `00 20 70 47` | 스톡 유휴경로 강제 |
| — | SET 호출 | `requestModelExecute` | 0x3948E0 | Y | 호출(§13.1/13.2 ABI) | 전역싱글턴, this 불필요 |

### 14.4 충실도/안전 메모 · 신뢰도
- **엔진·라이브뷰·입력 무관**: 두 훅 모두 **UI 렌더/이벤트 경로에 국한**(libObj 드로·펌프). 엔진 YUV 핀은 별도 경로(VphUpdateYuvCall, §12.1). SDCom/ScalarDaemon 불변.
- **가역성**: #2 상류 게이트는 설계상 detach-가역(§14.2 논증) — **단 에뮬 실측 미완**(§12는 하류 no-op만 실측). 실증 전까지 `[S, 미실측]`.
- **신뢰도**:
  - appFw=libObj 정적포함, 펌프=`sub_702E3C`, 드로=`sub_70CD64`, dirty게이트=`sub_4EFADE`/`sub_66D442`: **높음 [S]** (디컴파일 본문 직접 확인 + RE_UI_BRINGUP 교차).
  - §12 데드락 회피 논증: **높음 [S]** (GATE C 무타임아웃 하류락 보유 vs 상류 미진입 — 코드상 명확).
  - `sub_702C60`/`sub_70A2DA`가 SET에 충분한 프레임워크 락/컨텍스트 보유: **중 [S?]** (단일스레드 펌프라 레이스는 없음이 확실, 그러나 특정 모델명령이 추가 뷰락을 요구하는지는 미실측).
  - 펌프 wake 메커니즘(키주입 vs EventManager::push)·#2 런타임 가역: **미실측 [?]** — 다음 에뮬 PoC에서 확정.

### 14.5 다음 단계
1. 에뮬 PoC: #2a(`sub_70CD64` no-op) A/B — §12와 동일 CPU 회수 + **원2B 복원 후 UI 재개(런타임 가역)** 확인 → §12.4 한계 해소 실증.
2. 에뮬 PoC: #1(`sub_702C60` 트램펄린) + pending `requestModelExecute` SET(가역 파라미터) → SDCom 0x1002 before/after 교차검증(§13.4 안전경로).

---

## 15. 가역 억제 + 안전 SET — **에뮬 PoC 실증** [V] (2026-11-16)

> §14의 두 훅을 실기(에뮬)에서 검증. **A = detach-가역 CPU회수 억제**(§12.4 freeze 한계 해소), **B = 인프로세스 requestModelExecute 안전 호출**(§13.4 블로커2 해소).
> 전부 RAM-only·플래시 미접촉·콜드리부트 원복. frozen qemu, 신규 NAND 클론, `A5K_SHELL`+HMP, `-display none`. 주소 = libObj base(부팅마다 상이, 실시간 해석) + vaddr, 전부 Thumb.

### 15.A 가역 스톡 억제 — **detach-가역 실증(핵심)** [V]
두 상류 게이트를 각각 주입·측정(a5kinject + `.so`가 libObj text를 in-process 패치, mprotect RW→바이트→`__clear_cache`→RX):

| 훅 | 지점 | 패치 | 결과 CPU(im.elf) | detach-가역 | 비고 |
|---|---|---|---|---|---|
| baseline | — | — | **~42.4%**(INFRA0 21.9 + Appframework 19.2) | — | 순정 유휴 |
| §12(하류) | `sub_5DC3D4` | no-op | 0.5% | ❌ **데드락**(리부트만) | 플립 핸드셰이크 굶김 |
| **2a** | `sub_70CD64`(드로) | `BX LR` | **96%**(Appframework 스핀) | ✅ | 드로에 프레임 스로틀 포함 → no-op시 펌프 **busy-spin** → CPU회수 실패 |
| **2b** ⭐ | `sub_66D442`(dirty테스트) | `00 20 70 47`(`movs r0,#0;bx lr`) | **~13.5%**(INFRA0 **<0.3%**, Appframework 11.7) | ✅ | 스로틀 유지, 스톡 유휴 else-분기 강제 |

- **2b가 정답**: `sub_4EFADE`가 항상 "그릴 것 없음" 분기(`sub_53233C`)를 타게 함 → **INFRA0(합성) 완전 유휴 + Appframework 19.2→11.7%**(잔여=GL드로 `sub_704C0C`), **총 42.4→13.5%(~29% 회수)**, 스핀 없음.
- **detach-가역 실증(§12.4 한계 해소)**: 2a·2b 모두 **원바이트 복원(unpatch) 직후 스톡 UI 정상 재개**(CPU 42.3% 복귀, scanout 0x8f758000↔0x8f7ee000 재플립, 마젠타/시안/옐로 사각형 사라지고 순정 OSD 복원). §12 하류 no-op의 **영구 데드락과 결정적 차이** — 상류 게이트는 eglSwapBuffers/begin_swap에 **진입조차 안 해** 락 미보유 → 마지막 완료 프레임 경계에서 정지 → 복원시 다음 iteration이 dirty redraw로 재개(§14.2 논증 실측 확인).
- **지속표시**: 게이트 중 우리 사각형(시안, 리필)이 **정지된 순정 촬영화면 OSD 위에 100% 공존**(NO CARD/RAW+J/노출스케일/ISO AUTO 전부 보존, 스크린덤프 확인).
- **안전**: VM running, oops 없음, SYSV 프레임클럭 전진, 엔진/라이브뷰/입력 무손상. 코드패치 전부 im.elf 자기메모리 → **리부트 완전 원복**.

### 15.B 인프로세스 `requestModelExecute` 안전 호출 — **실증 + 블로커2 해소** [V]
`emulator/tools/mset_so.c`: worker가 libObj base 해석 → `viewManagerIf::requestModelExecute`(0x3948E0, Thumb, **전역 싱글턴 `dword_EEDDDC`, this 불필요**) 호출.
- **§13.4 블로커2(스레드안전) 해소**: 본 함수는 `createRequestModelExecuteEvent` → **`EventManager::push(mgr,ev,true)`**(`sub_70A770`)로 끝나는 **크로스스레드 안전 ENQUEUE** — 모델 실행은 appFw 펌프가 **자기 스레드에서** 처리. ⇒ **worker 스레드 호출 = 안전한 인큐**(트램펄린 불필요; §14.1 훅1은 선택지).
- **실증(로그)**: ① funcEventMode read(0x394924)=23 → ② `requestModelExecute("model/ZZPROBE",0,NULL)` **return 0**(비매칭 모델=엔진영향 0, 경로만 실행) → ③ mode=23(불변) → ④ `requestModelExecute("@M0E9",0xD05,NULL)`(실제 스톡 명령) **return 0** → ⑤ mode=23 → worker done. **전 과정 크래시 없음**, VM running, oops 없음, SYSV 프레임클럭 전진(0x796→0x7b4), UI(장면선택 화면) 정상 렌더 지속.
- ⇒ **우리 주입 코드가 순정 requestModelExecute 인프로세스 경로(이벤트 생성→push→펌프 디스패치)를 안전하게 구동**함을 실증(반환 성공·무크래시·엔진건전).
- **미실증 [?]**: **관측 가능한 가역 파라미터 변경**. 택한 명령(비매칭/`@M0E9`)은 funcEventMode·OSD에 가시변화 없음. 실제 파라미터 SET(예: model/CAMERA cmd 0xE param 0x2F1, 또는 노출보정)은 **ParamList 구성(§13.2 ABI: `ParamList::ParamList`0x70912E + IntParam + `add`0x7095D6)** 필요 — 호출 메커니즘은 **동일**(이미 안전 입증), 남은 건 **모델별 param id/값 스키마 확정**(malformed param = 엔진 assert 위험이라 강행 안 함). getModelInfo read-back은 `PrmNumberList` arg 블로커(§13.4-1) 잔존 → OSD 스크린덤프 검증 권장.

### 15.C 산출물
- `emulator/tools/gate2_so.c` ⭐ — 가역 CPU회수 억제(hook 2b `sub_66D442`→return 0). `gate_so.c` — hook 2a(가역이나 스핀, 상류-게이트 가역성 증거). (emulator)
- `emulator/tools/mset_so.c` — 인프로세스 requestModelExecute 안전호출. (emulator)
- CPU A/B·스크린샷(시안 사각형이 정지 순정 OSD 위에 공존, 복원후 순정 복귀) — 세션 스크래치.

### 15.D 다음 단계
1. **관측 가능한 가역 SET 완성**: gate2(또는 appFw-thread) + `requestModelExecute` model/CAMERA 1-param SET(ParamList §13.2) → OSD 스크린덤프 before/after. IntParam ctor 주소(viewUnified2 `sub_EA468` 0xEA468 등) 확정.
2. **§3 autostart**(§6-A/B): 부팅 재주입(설치 APK 오토런 or `/setting/mode/dmode`) — production 영속.
3. gate2 잔여 Appframework 11.7%(GL드로 `sub_704C0C`) 추가 게이트 검토(선택).

---

## 16. 관측 가능 파라미터 SET + 인프로세스 READ (ParamList 라운드트립) — **에뮬 PoC** [V] (2026-11-16)

> §15.B(인큐 안전)에 이어 **ParamList를 실은 실제 파라미터 SET**과 **인프로세스 getModelInfo READ**를 실증. RAM-only·플래시 미접촉.
> frozen qemu, **고유** NAND(`set_test.nand`)·monitor(46671)·PID로만 기동/종료(동시 실행 중인 타 QEMU 3개 무접촉, 광범위 `pkill` 금지).

### 16.1 ParamList SET — ABI 정확 구성·안전·가역 [V]
`emulator/tools/set2_so.c`: 순정 단일-param 레퍼런스(viewUnified2 `sub_172F20`, Bkup 미기록) 복제 —
```
list = malloc(8);  ParamList::ParamList(list)            // libObj 0x70912E
p    = malloc(0x10); sub_EA468(p, value)                 // viewUnified2 0xEA468 (IntParam ctor; vtable off_40B6D0)
ParamList::add(list, 0x29D, p)                           // libObj 0x7095D6
viewManagerIf::requestModelExecute("model/CAMERA", 0x55, list)  // libObj 0x3948E0 (전역 싱글턴)
```
- 실행: `SET 0x29D = 0 → 1 → 0`(ABI스모크 → 토글 → 되돌림) **모두 ret=0, 무크래시**, im.elf 생존, VM running, oops 없음. ⇒ **ParamList ABI 정확, 인프로세스 파라미터 SET 안전·가역**(malformed 없음 = assert 없음). IntParam vtable가 viewUnified2(로드됨)라 펌프가 처리 시 가상호출 유효.
- **관측 한계**: 0x29D/cmd 0x55는 **OSD 비가시**(set=1 vs 되돌림 프레임 **0 px 차이**; before와의 2984px 차는 시변 OSD요소로 SET 무관). 또한 0x29D는 **read-group 없음**(write-only로 추정, `sub_EA4B0(.,0x29D)` 참조 0건) → read-back 불가.

### 16.2 인프로세스 READ — **§13.4 블로커1 해소** [V]
`emulator/tools/rd_so.c`: `viewManagerIf::getModelInfo("model/CAMERA", selector, &outList, 0, 0)`(libObj 0x39485C, 전역 싱글턴) →
`ParamList::get(outList, id)`(viewUnified2 `sub_EA4B0` 0xEA4B0) → `*(ParamBase+0xC)`.
- **블로커1 해소**: §13.4가 막혔던 `getModelInfo` arg2(`&byte_9[6]` = "PrmNumberList")는 **실은 작은 정수 즉치**다 — `sub_17F798` 디스어셈블 `movs r1,#15`(Hex-Rays가 15를 `&byte_9[6]`로 오표기, byte_9@9+6=15). ⇒ selector = **작은 int(그룹 번호)**, 포인터 아님.
- 실증: `getModelInfo("model/CAMERA", 15, out)` **ret=1, 무크래시** → `ParamList::get(out, 0x12C)` = **11** (15회 일관). 스크린덤프 = **SCN** 모드 ⇒ **param 0x12C(촬영모드)=11=SCN, OSD 일치**. **인프로세스 엔진-파라미터 READ가 실제 표시 상태와 일치**(OSD-correlated). (앞서 selector 0x55로 읽자 ret=1이나 0x29D 미포함 → 그룹 번호가 param마다 다름 확인.)

### 16.3 종합 — 인프로세스 read+set 제어루프
우리 주입 코드가 **순정과 동일한 인프로세스 경로**로 ① 엔진-파라미터 READ(`getModelInfo`+int selector, OSD 일치) ② 파라미터 SET(`requestModelExecute`+ParamList, 안전·가역) 둘 다 구동 — SDCom 없이 엔진 제어 루프 성립. (SDCom 0x1002는 게스트서 -5로 막힘 → 인프로세스 getModelInfo가 대체 교차검증 경로.)

### 16.4 남은 1건 [?] & 안전경로
- **우리 SET의 OSD 가시 변화**: 아직 미실증. 가시 param(촬영모드 0x12C)은 **multi-param SET**(`model/STILL_REC` 4개: 0x12C/0x12D/0x12F/0x27E, `sub_189ED4`)이라 **부분 구성 시 assert 위험** → 강행 안 함(코디네이터 규칙). 깔끔한 1-param SET(0x29D 등)은 OSD 비가시. **안전경로**: `sub_189ED4`의 4-param을 **정확히** 구성해 model/STILL_REC로 촬영모드 변경 → `getModelInfo(.,15,0x12C)` read-back + OSD 스크린덤프 왕복(ABI·READ 모두 검증 완료라 기계적).

### 16.5 안전
- RAM-only(라이브러리 자기코드·힙), **플래시/Backup 미기록**(0x55 경로는 Bkup_Write 없음), 각 SET ret=0 무크래시, 엔진/라이브뷰/입력 건전(VM running·oops 없음·SYSV 전진). 콜드 리부트 완전 원복(자명: tmpfs+코드 미패치).
- 운영: **내 QEMU만** 고유 NAND/monitor/PID로 종료, 동시 실행 타 QEMU 3개 무접촉(광범위 `pkill -f qemu` 금지 준수).

### 16.6 산출물
- `emulator/tools/set2_so.c` — 인프로세스 ParamList SET(1-param, 가역). `rd_so.c` — 인프로세스 getModelInfo READ(selector=15 → 촬영모드 0x12C). (emulator)
- CPU/스크린샷·로그(SET ret=0×3, READ 0x12C=11=SCN) — 세션 스크래치.

### 16.7 다음
1. **OSD 가시 1-step SET**: `sub_189ED4` 4-param(model/STILL_REC) 정확 구성 → 촬영모드 변경 → read-back(sel 15)+OSD 왕복.
2. **§3 autostart**(§6-A/B): 부팅 재주입(설치 APK 오토런 or `/setting/mode/dmode`) — production 영속.

---

## 17. 오토스타트 영속 — **매 콜드부트 자동 재주입 실증** [V] (2026-11-16)

> §3/§6의 production 영속("재부팅 넘어 우리 `.so`가 im.elf에 자동 로드")을 **구체 실증**. 코디네이터 요청 전부 충족:
> ①최저위험·가역 훅 식별+근거, ②NAND COW 클론서 콜드리부트 후 **수동 ptrace 0** 자동주입+그리기(maps+스크린덤프), ③복구 불변식+가역, ④부팅시간 A/B.
> RAM-only 런타임·**쓰기는 `/setting`(rw 저티어) 3파일뿐**·frozen qemu·고유 NAND/포트/PID(동시 타 QEMU 무접촉, 광범위 `pkill` 금지). 주소 = init RE(objdump, 비스트립 심볼).

### 17.1 훅 메커니즘 — 스톡 init의 `/setting/env.txt` 환경주입 [S][V]
부팅 체인 RE(stage-2 `/sbin/init` = nflasha3 initrd, 비스트립): init은 bootin.elf/im.elf fork **이전에** rw `/setting`에서 환경을 읽어 주입한다.
- **`set_oreore_env`(init 0x9568)** → `open("/setting/env.txt")`→`mmap(PROT_READ)`→**`parse_and_set_env`(0x9484)**: 공백분리 토큰마다 `'='` 있으면 **`putenv`**, 없으면 **`unsetenv`**. `#` 주석·공백 스킵(`skip_comment_and_space` 0x9424). ⇒ **Sony 제공 "임의 환경변수 주입" 기구**(oreore=俺俺).
- **게이트**: `main`(0x96cc)에서 `set_oreore_env`는 **`/setting/mode/dmode` 존재 시에만** 호출(`check_mode_file` 0x9088가 열기 성공 시 전역 0x144bc=1 세팅 → main 0x98c0 `cbz` 게이트). dmode 내용은 무관(디버그 문자 `0/k/g/d/e` 회피 시 거동 불변 — 본 PoC는 `rb`).
- **전파**: init이 bootin.elf·sen.elf·s_start_android.sh·net_cmdd.elf를 **`execv`(execve 아님, `run_bg_process` 0x8e34)**로 기동 → **init environ 그대로 상속**. 정상 부팅 경로에서 init은 이후 **LD_PRELOAD를 클리어하지 않음**(클리어는 특수 '0'/shell 분기 0x9ad0, 또는 efence/duma 디버그 오버라이드 한정).
- **티어**[V] `/proc/mounts`: `/setting`=`nflasha2 vfat rw,sync,dirsync`로 **`noexec`/`nosuid` 플래그 없음** ⇒ **ld.so가 `/setting`의 `.so`를 매핑·로드 가능**. (`/`·`/usr`=ro ext2, `/system`=ro vfat.)

### 17.2 ⭐ 핵심 발견 — LD_PRELOAD는 im.elf까지 전파된다(§6.3 정정) [V]
`env.txt: LD_PRELOAD=/setting/a5k/probe.so` 설치·콜드부트 후 probe.so(각 프로세스서 `comm` 로깅)가 적재된 프로세스: **bootin.elf·sen류·s_start_android·mount/busybox/gzip/insmod/… 그리고 `im.elf`(pid 148) 자신**. `/proc/148/maps`에 **`/setting/a5k/probe.so` 4매핑 확인**.
- ⇒ **bootin.elf가 im.elf fork 전 LD_PRELOAD를 지운다는 §6.3 가정은 정상 부팅에서 성립하지 않는다**: im.elf 자신의 glibc 로더가 우리 `.so`를 매핑하고 **우리 생성자를 im.elf 안에서 실행**. **ptrace 없이** 매 콜드부트 im.elf 주입이 공짜로 성립.

### 17.3 입증한 두 오토스타트 (둘 다 RRM-only 런타임, `/setting` 3파일 영속)
| 옵션 | 메커니즘 | im.elf 진입 | 부팅시 추가작업 | 평가 |
|---|---|---|---|---|
| **Y ⭐(권장)** | `env.txt: LD_PRELOAD=/setting/a5k/a5kpreload.so`. `.so`는 **self-gating**(`comm=="im.elf"`일 때만 draw worker, 그 외 즉시 return) | **스톡 ld.so가 네이티브 적재**(ptrace 0) | `.so` 매핑 + im.elf서 **detached worker 1개**(OSD-on 폴링 후 그리기). 엔진 메인 스레드 비차단 | **최저위험**: ptrace 없음, 스톡 LeakTracer와 동형의 자연 경로 |
| X(대안) | `env.txt`가 `a5kauto.so`를 bootin.elf에 주입(comm 게이트) → detached worker가 im.elf 폴링→OSD-on 대기→**ptrace `__libc_dlopen_mode`**로 `pay.so` 주입(§11 경로) | ptrace→dlopen | 위 + ptrace attach/hijack/detach 1회 | §11 route-(b) ptrace와 동형. im.elf 단일 타깃. 약간 더 복잡 |

### 17.4 콜드부트 자동주입 실증 (수동 ptrace 0) [V]
pristine NAND **COW 클론**(`cp -c state/a5000.nand`)에 `/setting/mode/dmode`(="rb")·`/setting/env.txt`·`/setting/a5k/*.so` 설치 → **`system_reset`(콜드)**. 재부팅 후 셸·HMP로 관찰:
- **옵션 X**: `autohook.log` = `worker:armed → im_pid=0x94 → libc_base=0x425a000 → osd on → inject dlopen=0x431d7cc → dlopen_handle=0xecfb0(non-NULL=성공) → inject:done`. `/proc/148/maps`에 **`/setting/a5k/pay.so` 4매핑**. `auto_pay.log`=`AUTOSTART payload drawing in im.elf pid=148`. 스크린덤프: payload 영역(x120–320,y180–300=24000px) **100% 옐로(255,255,0)**(베이스라인 0). **사각형 밖 순정 OSD = 베이스라인과 0px 차이**(nonblack 13722=13722) ⇒ 국소·가산, 기능손실 0.
- **옵션 Y**: `/proc/146/maps`에 **`/setting/a5k/a5kpreload.so` 4매핑**(probe/pay/a5kauto **부재**). `preload.log`=`PRELOAD payload drawing in im.elf pid=146`. 스크린덤프: cyan 영역(x360–540,y120–230=19800px) **100% 시안(0,255,255)**. **ptrace 미사용**.
- **엔진 건전**: 두 경우 im.elf 생존·uptime 전진·oops(`liro-kliro_113`) 없음. 주입은 **OSD-on(촬영화면 up) 이후**라 부팅 임계경로 비간섭.

### 17.5 복구 불변식 — updater 도달성 보존 [V]
- **쓰기 범위**: `/setting/mode/dmode`·`/setting/env.txt`·`/setting/a5k/*.so` **신규 파일뿐**. `nflasha1`(updater core)·`nflasha2`의 기존 구조·`/setting/updater/`·`/usr`·`/system`·rootfs·커널 **전부 미접촉**(RECOVERY_MAP §5 보존 대상 불변).
- **구조적 근거**: updater 진입 결정은 **stage-1 로더**가 `/setting/updater/mode` 유무로 내린다(`provision/loader.sh` L42: `if [ -f /mnt/setting/updater/mode ]`) — **init/env.txt/im.elf보다 앞선 단계**라 우리 훅(init·im.elf 레이어)은 **구조적으로 이 결정에 간섭 불가**.
- **실증**: 훅 운용 후 시스템에서 `touch /setting/updater/mode; sync` → `system_reset` → 보드 로그 **stage-2 = `kernel 977820 B @0x81528000, initrd 3809280 B`** = **nflasha1 updater core**(정상 메인 3478912 B와 구별, RECOVERY_MAP §1 e2와 일치). ⇒ 플래그 경로 정상, **updater 도달성 보존**.

### 17.6 가역성 — 삭제 시 순정 완전 복원 [V]
`rm -f /setting/env.txt /setting/mode/dmode; rm -rf /setting/a5k; sync` → 콜드부트. 재부팅 후:
- `/proc/<im.elf>/maps`에 **`/setting/a5k` 매핑 0개**, env.txt·dmode **부재**, `/setting/updater/` 온전.
- 스크린덤프: 사각형 영역 **0 px 색상**(순정 화면 복원). ⇒ **훅은 전적으로 `/setting` 3파일에 격리**, 삭제=순정. (NAND·flash·Backup 미접촉 → 자명 복원.)

### 17.7 부팅시간 A/B — ≤ 스톡 [V]
동일 방법(HMP에서 AIF `0xF3440144` bit1=panel OSD on + `jiffies_64`@`0x80360240`, HZ=100 폴링; INITIAL_JIFFIES 공통 → 상쇄)으로 **T_osd**(최초 OSD) 측정, 동일 호스트:
| 부팅 | T_osd(게스트초, jiffies 보정) | raw jiffies |
|---|---|---|
| **옵션 Y 훅** | **≈9.98s** | 4294938294 (+998) |
| **스톡** | **≈10.14s** | 4294938310 (+1014) |
- **훅 − 스톡 = −0.16s**(훅이 더 빠름 = 런간 노이즈 내) ⇒ **T_osd ≤ 스톡** 성립. (절대값은 호스트부하로 ~2x 팽창 — BOOT_TIME.md §1; 유효량은 훅-스톡 델타.)
- **구조적 근거**: 옵션 Y는 im.elf에 작은 `.so`를 매핑 + **detached worker 스레드 1개**(OSD-on 폴링)만 추가 — **엔진/UI 임계경로(D+F=av-cam, BOOT_TIME.md 82%)를 직렬화하지 않음**. 옵션 X는 OSD-on **이후** ptrace라 역시 임계경로 밖. ⇒ 설계상 델타≈0, 측정과 일치.
- **노이즈 주의** `[?]`: 단일쌍 측정·호스트부하 변동 → 정밀 델타는 icount A/B로 보강 여지(결론 "≤스톡"은 불변).

### 17.8 잔여 위험 / 한계
- **전파 범위**: env.txt LD_PRELOAD는 **모든 init 자식**(mount/busybox/…)에 `.so`를 매핑 → self-gating(comm≠im.elf면 즉시 return)으로 inert하나, 매핑 비용(수 KB×프로세스)은 존재. 영향 미미(T_osd A/B가 흡수). 축소하려면 더 좁은 게이트/별도 로더 가능.
- **dmode 게이트 공존**: 누군가 `/setting/mode/dmode`에 디버그문자(`g`=gdb 등)를 넣으면 거동 변화 → 우리 dmode는 중립문자(`rb`) 고정.
- **warm-resume**: 유휴 5분 warm power-off 후 ttyAM1 셸/ OSD 재구성 지연 관측(§11.5) — 주입과 무관(순정 warm-resume 경로). 콜드부트 자동주입엔 영향 없음.
- **실기**: 전부 에뮬 전용. 실기 배포는 `/setting` 쓰기(OM telnet)로 동일 3파일 설치 — 단 **실기 검증 전까지 설치 금지**(CLAUDE.md). a6400 이식: 동 기구(`/setting` config + 부팅 preload), ld.so.preload 자리 유무만 기종차(§5.3/§6.4).

### 17.9 산출물 (emulator/tools, 미push)
- **`a5kpreload_so.c`** ⭐ — 옵션 Y: `env.txt` LD_PRELOAD, self-gating(im.elf서만) OSD draw. **권장 production 훅**.
- `a5kauto_so.c` — 옵션 X: bootin.elf서 발화, detached worker가 im.elf에 ptrace `__libc_dlopen_mode`로 payload 주입.
- `auto_pay_so.c` — 옵션 X payload(옐로, 자동시작 마커 로그).
- `osdtime.py` — T_osd 측정(OSD 레지스터+jiffies 폴링, stock/hook 동일).
- 빌드: `docker a5k-cross` `arm-linux-gnueabi-gcc -O2 -fPIC -shared`(헤더에 레시피). 설치: `/setting/mode/dmode`(중립) + `/setting/env.txt`(`LD_PRELOAD=/setting/a5k/a5kpreload.so`) + `/setting/a5k/a5kpreload.so`.
- 스크린덤프(옐로/시안 사각형이 순정 OSD 위 공존, 삭제후 순정 복원)·로그 — 세션 스크래치(저장소 미커밋).

### 17.10 다음 단계
1. 옵션 Y를 §15.2b 가역 draw-억제 + §16 getModelInfo/requestModelExecute 제어루프와 결합 → 콜드부트부터 우리 UI가 뜨는 end-to-end(지속표시+엔진 read/set).
2. (선택) icount 기반 T_osd/T_shoot 정밀 A/B(BOOT_TIME.md 하네스)로 델타 정량 확정.

---

## 17. OSD 가시 SET 왕복 시도 — **멀티-param SET ABI 실증 + 가시변경 블로커 규명** [V] (2026-11-16)

> §16.4 "OSD 가시 SET" 마무리 시도. 코디네이터 제안대로 `sub_189ED4`의 **4-param `model/STILL_REC`** 를 정확 RE·구성·호출.
> 결과: **멀티-param ParamList SET이 안전 실행**(ret=0, assert 없음)됐으나, 이 경로로는 **가시 모드변경 불가**임을 규명(강행 안 함). 전부 RAM-only, 고유 인스턴스(ss_test.nand/46681/PID)만 사용.

### 17.1 4-param SET — ABI 정확 RE·안전 실행 [V]
`sub_189ED4(a1,a2,a3,a4)` 디컴파일 정확 디코드: 4개 IntParam(`sub_EA468`)을 각각 **0x12C(촬영모드)/0x12D/0x12F/0x27E**로 `ParamList::add` 후
`requestModelExecute("model/STILL_REC", 0x15, list)`(cmd = `&dword_14+1` = **0x15**). Backup 매핑: 0x12C←0x1070103, 0x12D←0x1070100, 0x12F←0x1070102, 0x27E←0x10700FD.
- `emulator/tools/ss_so.c`로 **정확 유효 튜플**(SCN self-shoot `(11,1,1,1)`, sub_126EB8의 그 값) + 토글 `(11,1,1,0)` 호출 → **둘 다 ret=0, 무크래시/무assert**, im.elf 생존. ⇒ **4-param 멀티 ParamList SET ABI 완전 실증**(단일 §16 + 멀티 §17, 안전·가역).

### 17.2 가시변경 블로커 2건 (규명) [V]
1. **`sub_189ED4` = 셀프촬영(self-shooting) 적용**: 호출부가 `enter/exitSelfShootingMode`(sub_126EB8/126F14) 둘뿐. `0x27E` = self-shoot 플래그. 에뮬에는 **리버스-디스플레이(화면 플립) GPIO가 없어** self-shooting UI가 렌더되지 않음 → `(11,1,1,1)`↔`(11,1,1,0)` 스크린덤프 **0 px 차이**(불가시). 모드도 SCN(11) 유지.
2. **0x12C(촬영모드)는 STILL_REC 0x15로 직접 변경 불가**: `0x12C=12` SET해도 read-back **11 유지**(`getModelInfo(.,15,0x12C)`) → 엔진이 무시. **촬영모드는 모드다이얼(0x2F1 RecViewFromDial) + 대규모 apply(@187919, CmnViewMgrSetSave에서 ~20 param)로 엔진이 파생**하는 값이지, 0x12C 직접쓰기로 바뀌지 않음. ⇒ 제안된 4-param 경로로는 **가시 모드변경 불가**(코드상 명확).

### 17.3 가시 SET의 올바른 경로 (문서화, 강행 안 함)
OSD 가시 변경은 둘 중 하나 필요(둘 다 추가 위험/작업이라 "확신 없으면 강행 금지"에 따라 미실행):
- **A. 깔끔한 설정 래퍼**(단일 enum, 명백 가시): `WrapperImageAspect::setImageAspect`(0x21F7DE, 16:9↔3:2), `WrapperImageSize::setImageSize`, `WrapperImageQuality::setImageQuality`, `WrapperDriveMode::setDriveMode`(0x21DA7E). **단 `ViewBaseProduct* this` 필요** — top view 포인터는 **ViewManager 워크**(`*(libObj+0xEEDDD8)` → `VM[9]+0x10*(VM[0]-1)` → `sub_70A55C/sub_70A56A`, getCurrentFuncEventMode가 쓰는 그 워크)로 획득 가능(오프셋 취약 → 신중 필요).
- **B. 모드다이얼 경로**: 0x2F1(RecViewFromDial) + 대규모 apply 정확 구성(~20 param) → 모드 전환.

### 17.4 종합 — route-b 인프로세스 제어루프 현황
우리 주입 코드가 순정 경로로 **그리기(§10-12)·스톡억제(§15, detach-가역·CPU회수)·엔진-파라미터 READ(§13,§16, OSD 일치)·파라미터 SET(§15-17, 단일+멀티 ParamList, ret=0·안전·가역)** 전부 구동 — SDCom 없이 인프로세스 read+set 루프 성립. **남은 1건 = "우리 SET의 OSD 가시 반영"**: 에뮬의 self-shooting 미렌더 + 0x12C 비직접설정으로 이번 방법은 불가, **올바른 경로는 §17.3(view-this 래퍼 or 다이얼 apply)로 문서화**.

### 17.5 안전
RAM-only, 각 SET ret=0 무크래시/무assert, 엔진/라이브뷰 건전(VM running·oops 없음). 콜드 리부트 원복(자명). **광범위 `pkill` 미사용** — 내 QEMU만 고유 nand(ss_test.nand)/monitor(46681)/PID로 종료, 동시 타 QEMU 무접촉.

### 17.6 산출물 / 다음
- `emulator/tools/ss_so.c` — 4-param `model/STILL_REC` SET + read-back(멀티 ParamList ABI·self-shooting 규명). (emulator)
- **다음**: §17.3-A(ViewManager 워크로 view-this → Wrapper 설정 세터로 aspect/quality 가시 변경) **또는** §3 autostart(부팅 재주입).

---

## 18. OSD 가시 SET 왕복 — **완성** [V] (2026-11-16) ⭐ route-b 가시 제어루프 종결
> §17.3-A 깔끔경로로 **우리 주입 코드가 카메라 설정(화면비)을 눈에 보이게 바꾸고 되돌림**을 실증. read-back + OSD 둘 다 확인. RAM-only.
> 고유 인스턴스(asp_test.nand/46691/PID)만 사용, 광범위 pkill 미사용.

### 18.1 접근 — 취약한 ViewManager 워크 **회피**, 전역 싱글턴 vtable 사용 [S][V]
`WrapperImageAspect::get/setImageAspect` 디컴파일: 둘 다 `sub_11BB1C(view)`로 **전역 싱글턴 `CmnViewProcessDataMgr`**(viewUnified2 `unk_47FFF8` @ `+0x47FFF8`, `_cxa_guard` `+0x47FFF4`)를 얻어 **그 객체의 vtable**로 화면비를 읽고/쓴다 — 스톡이 이 전역을 **현재 top view에 상시 바인딩**하므로 **view 포인터 워크 불필요**.
- READ = **vtable[3]**(off 12): `m(obj, 1, &out)`. WRITE = **vtable[14]**(off 56): `m(obj, 1, val)`. (getImageAspect/setImageAspect 본문에서 확인.)
- `emulator/tools/aspect_so.c`: 모든 역참조 전 **가드**(guard&1 생성됨 확인, vtable 포인터·메서드 주소가 viewUnified2 매핑 범위 내인지) — 비정상이면 **중단·로그**(강행 안 함, assert/크래시 회피).

### 18.2 왕복 결과 — read-back + OSD 둘 다 [V]
로그: `READ aspect=1` → `WRITE 2`(ret=1) → `READ=2` → `WRITE 1`(revert, ret=1) → `READ=1`. **전부 ret=1, 무크래시**, im.elf 생존, VM running, oops 없음.
- **OSD 스크린덤프**(HMP): before **`16:9` / `17M`** → toggled **`4:3` / `18M`**(화면비 배지 + 메가픽셀 둘 다 변화, 엔진이 화면비에서 파생) → revert **`16:9` / `17M`**.
- 픽셀 diff: before↔toggled **3438 px 변화**, **before↔revert `0 px`(완전 동일)** → 변화가 **우리 SET 때문**임을 확정(시변 노이즈 아님; §16/§17의 before≠revert와 대조).
- ⇒ **우리 주입 코드가 순정 인프로세스 경로로 실제 카메라 설정(화면비)을 가시적으로 바꾸고, read-back과 OSD 둘 다로 검증, 완전 가역**.

### 18.3 종합 — route-b **인프로세스 가시 제어루프 종결** ✅
우리 `.so`가 im.elf 안에서, SDCom 없이, 순정과 동일한 경로로:
- **그리기**(§10-12, OSD 더블버퍼 픽셀 경로), **스톡 draw 가역 억제 + CPU 회수**(§15, detach-가역·~29%),
- **엔진-파라미터 READ**(§13,§16, getModelInfo, OSD 일치), **파라미터 SET**(§15-17, requestModelExecute+ParamList 단일·멀티, 안전·가역),
- **가시 설정 변경 왕복**(§18, 화면비 16:9↔4:3, read-back+OSD 검증, 가역) — **전부 실증**. route-b 인프로세스 제어루프 end-to-end 성립.

### 18.4 안전
- RAM-only, 가드된 역참조(범위 밖이면 중단), 각 호출 ret=1 무크래시, 엔진/라이브뷰 건전. 값은 원복(1→2→1), before↔revert 0px = 순엔진상태 완전 복귀. NAND는 throwaway 클론.
- 화면비 vtable 세터는 **런타임 모델 연산**(라이브 엔진상태 즉시 변경 = 가시)이며, Backup 영속은 메뉴 커밋의 별도 단계(본 호출서 미유발). 엄격 RAM-only 요구 시에도 **값 원복 + throwaway 클론 + 콜드부트 원복**으로 무잔존.
- **광범위 `pkill` 미사용** — 내 QEMU만 고유 nand(asp_test.nand)/monitor(46691)/PID로 종료, 동시 타 QEMU 무접촉.

### 18.5 산출물 / 다음
- `emulator/tools/aspect_so.c` — 전역 CmnViewProcessDataMgr vtable로 화면비 가시 토글(read-back+가드). (emulator)
- **route-b 가시 제어루프 종결**. 다음: **§3 autostart**(부팅 재주입: 설치 APK 오토런 or `/setting/mode/dmode`)로 production 영속 — 매 부팅 우리 UI 재주입.

---

## 19. ⭐ 수렴 마일스톤 — **진짜 uicore UI가 im.elf 안에서 구동** [V] (2026-11-16)
> §10~§18의 route-(b) 메커니즘(픽셀경로·주입·오토스타트·억제·READ·SET)을 전부 **uicore(플랫폼 독립 UI)**에 결합.
> 테스트 사각형이 아니라 **실제 촬영 OSD**를 im.elf 안에서 그린다. 산출물 = `app/native/platform/a5000-inproc/`(ROOT repo).
> 전부 RAM-only·가역, 고유 인스턴스(inproc_verify.nand/shell 46777/monitor 46778/PID)만 사용, 광범위 pkill 미사용.

### 19.1 산출물 — in-process 플랫폼 백엔드 (`app/native/platform/a5000-inproc/`)
uicore를 **게스트 glibc `.so`(`a5kinproc.so`, 170KB)**로 크로스컴파일(a5k-cross, C++런타임 정적링크 → libstdc++ NEEDED 0,
libc/libpthread/libm만). uicore 코어는 플랫폼 헤더 0 유지, 새 백엔드만 im.elf 내부를 안다:
| uicore 인터페이스 | 구현 | 메커니즘 |
|---|---|---|
| `Display` | `display_osd.cpp` | 캔버스 RGBA8888 → OSD 더블버퍼(0x8f758000↔0x8f7ee000) RGBA4444 블릿, `/dev/mem` (§10) |
| `Input` | `input_event0.cpp` | `/dev/input/event0` EV_KEY → raw ISV; a5000 keymap(데이터) 재사용 |
| `CameraControl` | `camera_inproc.cpp` | READ `getModelInfo("model/CAMERA",15,…)`(§13/§16); SET 화면비 vtable(§18, 가드) |
| `Clock`/`Log` | `../a5000/sys_a5000.cpp` 재사용 | generic libc |
| 엔진 글루 | `engine.h` | libObj/vu 베이스 실시간 해석 + 고정 Thumb vaddr + gate 2b + OSD 프로브 |
빌드: `app/native/build.sh a5000-inproc` → `platform/a5000-inproc/build/a5kinproc.so`.

### 19.2 콜드부트 end-to-end 실증 [V]
pristine NAND COW 클론에 §17 오토스타트 3파일(`/setting/mode/dmode`·`/setting/env.txt`=`LD_PRELOAD=/setting/a5k/a5kinproc.so`·
`/setting/a5k/a5kinproc.so`) 설치 → **`system_reset`(콜드, ptrace 0)**. 재부팅 후 `/setting/a5k/ui.log`:
```
inproc: engine resolved libObj=0x5d2a000 vu=0x131fc000
osd: mapped double buffer phys 0x8f758000 len 0x12c000
input: /dev/input/event0 opened
app: init done display=osd-inproc camera=connected bindings=20
inproc: stock draw suppressed (gate 2b @0x66D442)
inproc: uicore UI running (frameMs=66 secs=0 assets=builtin)
cam: live getModelInfo(model/CAMERA,15,0x12C)=11  aspect(vtable)=1
```
- **주입**: `/proc/143/maps`에 `a5kinproc.so` **4매핑**(스톡 ld.so가 LD_PRELOAD로 네이티브 적재, ptrace 없음).
- **렌더(스크린덤프 `vph 0`, 640×480)**: **실제 uicore 촬영 OSD** — 박스 `P`·`AF-S`(상단), `120`·배터리·`85%`(우상단),
  `1/160  F5.6  0.0  ISO AUTO`(하단 SS/F/EV/ISO 행). **사각형 아님, 스톡 소니 UI 아님.** 스톡은 gate 2b로 억제되어 우리 OSD만 표시.
- **스톡 억제**: `sub_66D442`→return 0(§15 2b) 적용 로그 확인 → 우리 OSD가 패널 소유.
- **라이브 엔진 READ**: in-process `getModelInfo(model/CAMERA,15,0x12C)=11` = **SCN**(§16 일치) + aspect(vtable)=1 — 백엔드가 실엔진 상태를 읽음.
  (※ model-param↔uicore 카탈로그 A-side **값 매핑(R1 변환표)**은 미완 → 현재 OSD는 대표 CameraState 렌더. 라이브값은 로깅. 매핑 완성 시 readState에 drop-in.)
- **엔진 건전**: im.elf pid143 생존, uptime 211s 전진, **oops 0**, 2분+ 동안 readState 3사이클 무크래시.

### 19.3 가역성 — 삭제+콜드부트 = 순정 완전복원 [V]
3파일 삭제 → `system_reset`. 재부팅 후 `/proc/146/maps` `a5kinproc` **0매핑**, `/setting/a5k` 부재, 스크린덤프 =
**순정 소니 OSD 복원**(SCN·16:9·17M·RAW+J·98%·셔터/조리개 스케일). **우리 라이브 READ가 보고한 0x12C=11=SCN이
순정 화면의 SCN과 일치** → read 정확성 교차확인. `nflasha1/2`·`/usr`·`/system`·rootfs·커널 미접촉 → updater 도달 불변, 벽돌 0.

### 19.4 상태 요약
- ✅ **렌더(실제 촬영 OSD)·스톡 억제·입력 배선(event0 open)·오토스타트·라이브 엔진 READ·가역** — 전부 실증.
- ⬜ 남은 것: (a) model-param↔카탈로그 **R1 값 변환표**(라이브 READ를 OSD에 반영; M2 RE), (b) 에뮬 **키 입력 모델 부재**로
  라이브 키주입 미실증(백엔드는 배선됨 — event0 open), (c) 베이크 폰트/아이콘(현재 내장 폴백; `A5K_UI_ASSETS`로 적용 가능).
- 산출물: `app/native/platform/a5000-inproc/`(ROOT), 스크린덤프(우리 OSD / 복원된 순정) — 세션 스크래치.

---

## 20. 완성도 폴리시 — 스타일·라이브 값·키입력 [V] (2026-11-16)
> §19의 "메커니즘 입증"을 "보이고 라이브인 UI"로. 에뮬 전용, 고유 인스턴스(polish.nand/scan.nand, 포트 46977/46978, PID)만, 광범위 pkill 미사용, 콜드부트 가역.

### 20.1 베이크 에셋 ON — 완성 스타일 [V]
`env.txt`에 `A5K_UI_ASSETS=/setting/a5k/assets` + OSD 폰트(`font_osd_*`) + `icons.bin`을 `/setting`(13MB vfat, 6MB 여유 확인)에 설치.
`gfx.load`는 폰트별 폴백이라 OSD 폰트만 있어도 촬영 OSD는 **완성 스타일**: 모드/드라이브/AF 아이콘 + EV/ISO 글리프 + 베이크 숫자(`1/160 F5.6 ±0.0 ISO AUTO`) + 배터리 아이콘. (로그 `app: assets ... fonts=0 icons=1` — icons 로드; UI 폰트 미배포라 fontsOk=0이나 OSD 폰트는 적재돼 OSD는 styled. 5x7 폴백 대비 큰 향상.) `-DUICORE_ASSETS_MMAP`로 mmap 적재(저RAM).
- 주의: busybox `tar`가 macOS ustar/pax 헤더를 거부 → 에셋은 **파일 개별 push**로 설치(기록).

### 20.2 라이브 OSD 값 — 노출 **모드 라이브** [V]
`mdlscan_so.c`(신규 RE 도구)로 `getModelInfo("model/CAMERA", sel, id)`를 sel×id 스캔 → in-process로 읽히는 param/selector 실측(엔진 완전기동 후에만 ret=1; 초기 11s엔 ret=0). **param 공간은 SDCom 256+ 와 다른 viewUnified 공간**(0x12C=촬영모드).
- **핵심 매핑 확정**: `model/CAMERA` 0x12C = 엔진 `s_exposureMode` **D값**. `settings_sdcom_map.tsv`의 "SD s_exposureMode D=" ↔ uicore `kVals_EXPOSURE_MODE`로 LUT(`modeDtoUicore`): D1→P,2→A,3→S,4→M,6→auto,9→portrait,**11→landscape(3)**,12→night,13→night-portrait,14→sunset,16→sports.
- **실증**: 스톡 부팅 모드 = landscape. `cam: LIVE mode model0x12C(D)=11 -> uicore=3`(ui.log) → 우리 OSD가 **산(landscape) 아이콘**을 그림 = 스톡 OSD의 SCN landscape 아이콘과 일치. **라이브 엔진값이 OSD에 반영**(대표값 아님).
- 한계: SS/F/EV/ISO는 **AE-active 엔진(렌즈+카드) 필요** → 에뮬 미구동이라 대표값 유지. 읽히는 다른 param들은 로깅(스캔 산출물). model↔catalog 값 매핑 전체는 M2 R1 RE.

### 20.3 라이브 키입력 — 배선됐으나 in-process 미공급 [V, 기록]
`sendkey m/up/down/ret`(→ sub-micro/ADC → libInputServant → event0, docs/RE_WHEEL_ZOOM_DATE·sony_a5000.c) 주입 시 우리 UI 무반응.
- **진단**: 셸에서 event0 **raw 캡처가 호스트 18초 연타 중에도 공백**(event1/2 포함) → event0("D keyboard" uinput)는 **in-process(Android 앱/ABT 프런트뷰 미등록)에서 스톡 입력경로가 공급하지 않음**. 스톡 네이티브 UI는 **엔진/뷰 입력경로**로 키를 받음(event0은 app-mode Track A용).
- **결론**: `input_event0.cpp`는 event0 open/read 정상(백엔드 버그 아님). 라이브 키 전달엔 **ABT 프런트뷰 등록**(Track A 런처의 `AppInfoReceive`/`ABT_set_state`, CLAUDE.md §5) 또는 **엔진-입력 훅** 필요 — 후속 작업. (코디네이터 규칙: 불확실/리스크면 강행 말고 기록.)

### 20.4 산출물
- `app/native/platform/a5000-inproc/camera_inproc.cpp` — 라이브 모드(`modeDtoUicore` LUT). (ROOT)
- `emulator/tools/mdlscan_so.c` — getModelInfo param/selector 스캔 RE 도구. (emulator)
- 스크린덤프(styled OSD: landscape 라이브 아이콘 + 베이크 폰트/아이콘) — 세션 스크래치.

---

## 21. 라이브 키입력 — **엔진-입력 훅으로 in-process 수신·조작 실증** [V] (2026-11-16) ⭐ §20.3 해소
> §20.3 미해결("event0 in-process 공백 → ABT 등록 or 엔진-입력 훅 필요")을 **엔진-입력 훅**으로 해결. 우리 주입 UI가 **라이브 키를 받아 조작**됨. 에뮬 전용, 고유 인스턴스(key_test.nand/46701/PID)만, 광범위 pkill 미사용, 콜드부트 가역.

### 21.1 왜 event0가 비는가 — 네이티브 Diadem 키경로 확정 [S]
스톡 네이티브 UI는 event0이 아니라 **엔진 입력경로**로 키를 받는다(TRACKB_RUNTIME §2 재확인):
- `snd_InfraInputEvent_to_InputServant`(libObj 0x37F2D4)는 **`IsViewModeDiadem`이 거짓(=app 모드)일 때만** 입력을 InputServant(→event0)로 포워드 → **Diadem에선 event0에 아무것도 안 감**(디컴파일 직접 확인).
- 네이티브 경로: 물리키(sub-micro/ADC) → `InfraInput` → `InputService` 콜백(`callback_panel`0x381D94/`callback_convert`0x382220/`callback_direct`0x382050 등, 각 `_InfraInputEvent*`) → **`InputService::createEvent(id,type)`(libObj 0x37F854)**로 view Event 생성 → appFw 펌프(§14)로 view 디스패치. ⇒ **createEvent가 모든 입력 이벤트의 단일 합류점.**

### 21.2 훅 — createEvent 재구현(비파괴) [V]
`createEvent`는 작다(`e=operator new(0x10); Event::Event(e,id,type,1)(0x7042D4); return e`). 우리 스텁 `my_createEvent`가 **그대로 재구현 + id 기록** → createEvent 엔트리에 **절대 Thumb 점프**(`LDR.W PC,[PC,#0]`=`DF F8 00 F0` + target|1, 엔트리 4정렬) 패치. 트램펄린 relocation 불필요. **비파괴**(스톡 입력 계속 동작) — 우리가 모든 입력 id를 관측.
- **in-process로 받은 키맵**(실측): `id = 0x15010000 | (code<<4) | edge`(edge 1=press,2=release). code: **CENTER 0x06 · UP 0x07 · DOWN 0x08 · RIGHT 0x09 · LEFT 0x0a · DELETE 0x10 · MENU 0x11**. (idle noise 0x17005006 제외.) 각 `sendkey`에 정확히 대응 확인.

### 21.3 라이브 조작 데모 — 키로 메뉴 네비게이션 [V]
`emulator/tools/ui_so.c`: createEvent 키훅 + **gate 2b**(sub_66D442→return 0, 스톡 draw 억제·§15) + 메뉴 렌더를 한 `.so`에 결합. `my_createEvent`가 키 press로 상태(menu_open/highlight) 갱신, worker가 OSD 더블버퍼에 메뉴 오버레이 렌더.
- **스크린덤프 왕복**: closed → **MENU** → open(마젠타 타이틀 + 5바, highlight item0) → **DOWN×2** → highlight **item2** 이동 → **CENTER** confirm. 분석: title=MAGENTA(open), items highlight 위치가 `[0]→[2]` 이동 확정.
- ⇒ **우리 주입 in-process UI가 라이브 키(MENU/DOWN/CENTER)를 받아 메뉴를 열고 네비게이션**. "보임→조작됨" 달성. 기존 억제(gate 2b)와 **공존**. 무크래시·VM running·oops 0. worker 종료 시 훅 자동 복원 → 스톡 재개(가역).

### 21.4 §20.3과의 관계 / 안전
- §20.3이 남긴 두 옵션 중 **엔진-입력 훅**을 실증(ABT 프런트뷰 등록/app-mode **불필요**). event0 백엔드는 멀쩡했고(§20.3), Diadem에선 애초에 안 채워질 뿐 — createEvent 훅이 정답.
- RAM-only(libObj 자기코드 패치 + 힙), 가역(원바이트 복원), 플래시 미접촉, oops 0, 콜드 리부트 원복. **내 QEMU만** 고유 nand(key_test.nand)/monitor(46701)/PID로 종료, 동시 타 QEMU 무접촉.

### 21.5 산출물 / 다음
- `emulator/tools/key_so.c` — createEvent 훅 키-RE 도구(in-process 키맵 산출). `ui_so.c` — 키훅+gate+메뉴 렌더 라이브 조작 데모. (emulator)
- **다음**: 데모 메뉴 대신 **진짜 uicore `App`에 추상액션 공급**(MENU→캐스케이드 메뉴, 방향키→카테고리/항목, CENTER→선택) — 키맵(21.2)→uicore 액션 바인딩. 그러면 §19 uicore UI가 라이브 조작되는 완성형.

---

## 22. ⭐ 최종 배선 — **진짜 uicore UI가 라이브 조작 + 완성 한글 스타일** [V] (2026-11-16) · 완성형
> §21(createEvent 키훅)을 `platform/a5000-inproc`에 **최종 통합**: 데모가 아니라 §19의 진짜 uicore `App`이 라이브 키로 조작된다.
> 에뮬 전용, 고유 인스턴스(live.nand/font.nand, 46977/46978, PID)만, 광범위 pkill 미사용, 콜드부트 가역.

### 22.1 Input 백엔드 교체 — event0 → createEvent 훅 (`input_createevent.cpp`)
- §20.3의 event0 백엔드를 **엔진-입력 훅**으로 대체: `InputService::createEvent`(libObj **0x37F854**)에 비파괴 절대-Thumb 점프(`DF F8 00 F0`+target|1) 패치, `my_createEvent`가 원본 재구현(`malloc(0x10)`+Event ctor `0x7042D4`)+**id를 lock-free SPSC 링**에 기록. `Input::poll`이 uicore 워커 스레드에서 드레인.
- **키맵**: `id = 0x15010000 | (code<<4) | edge`(edge1=press) 디코드 → **code→a5000 ISV raw**(CENTER0x06→232, UP0x07→103, DOWN0x08→108, RIGHT0x09→106, LEFT0x0a→105, DELETE0x10→595, MENU0x11→514) → 기존 a5000 keymap이 raw→추상액션. idle noise 0x17005006(namespace 0x17)는 필터.
- `Input::shutdown`이 훅 원복(detach-가역). 스톡 입력 계속 동작(비파괴).

### 22.2 라이브 조작 실증 (스크린덤프) [V]
콜드부트 자동주입(§17 env.txt+dmode) + gate 2b 억제 + 베이크 한글 에셋(`A5K_UI_ASSETS`) 상태에서 **host `sendkey`(홀드 ~400ms)**로:
- **MENU**(`sendkey m 400`) → ui.log `action: MENU raw=514 value=1` → **uicore 캐스케이드 메뉴 열림**(스톡 타일메뉴 아님).
- **NAV_DOWN/RIGHT** → 카테고리 이동(1→3, #D34100/녹색 포커스) + 그룹 드릴인(breadcrumb "카메라 설정 › 화질/이미지", 솔리드 #D34100). **CONFIRM** 디스패치.
- 메뉴 그룹 라벨이 **완성 한글**(화질/이미지·동영상·드라이브·플래시·초점·노출/측광) + 우측 값(이미지 크기 L·종횡비 16:9·화질 RAW+JPEG·JPEG 화질 50) + 하단 "▶안으로 ◀이전 ●선택 MENU닫기" — **host 시뮬 골든과 일치**.

### 22.3 핵심 교훈 & 자산
- **키 홀드 필요**: `sendkey <k>`(기본 ~100ms)는 sub-micro/ADC 폴링이 못 잡음 → **`sendkey <k> 400`**(홀드)이라야 등록. (이것이 §20.3에서 event0뿐 아니라 짧은 sendkey도 무반응이던 2차 원인.)
- **한글 폰트**: `font_ui_19/20/24/26/27.bin`(한글 2350자 포함, ~4.8MB)을 `A5K_UI_ASSETS`에 배포해야 메뉴 한글 렌더(ASCII-only `font_osd_*`만 있으면 "/"+픽셀 폴백). 에뮬은 mmap 로드라 RAM 무리 없음(`-DUICORE_ASSETS_MMAP`). 실기는 지연로딩 별도 후속.
- **autostart 필수 2파일**: `/setting/env.txt`(LD_PRELOAD+A5K_UI_ASSETS) **그리고** `/setting/mode/dmode`(없으면 init이 env.txt를 안 읽어 .so 미로드).
- busybox `tar`가 macOS ustar/pax 헤더 거부 → 에셋 파일 **개별 push**.

### 22.4 종합 — 완성형 커스텀 UI (에뮬 카메라)
우리 `.so`가 im.elf 안에서, SDCom 없이: **자동주입(§17)·스톡 draw 억제(§15)·완성 한글 스타일(베이크 에셋)·라이브 엔진 모드값(§20, getModelInfo 0x12C)·라이브 키 조작(§21/§22, createEvent→uicore 캐스케이드 메뉴 열기·네비·드릴인)** 전부 동작. **"보임→완성스타일→라이브값→라이브조작"** end-to-end. RAM-only, 콜드부트 가역, oops 0, 복구 불변식 보존.

### 22.5 산출물 / 남은 것
- `app/native/platform/a5000-inproc/input_createevent.cpp`(신규)·`inproc.cpp`(Input 교체 + `A5K_UI_NOSUPPRESS` 진단 게이트)·`Makefile`·README. (ROOT)
- 스크린덤프(한글 캐스케이드 메뉴 열림/네비/드릴인) — 세션 스크래치.
- 남은 것: 값 변경(값 피커 CONFIRM)까지 스샷·전체 폰트셋(ui_12/15 포함 fontsOk), 실기 폰트 지연로딩, 라이브 SS/ISO(AE-active 엔진 필요).

---

## 23. ⭐ 설정 APPLY 확장 — model-param(group) in-process 경로로 SDCom 설정 실적용 [V] (2026-11-16)
> 과제: 48개 `sdcom` 설정 중 가능한 많은 수를 **in-process model-param 경로**로 에뮬에서 **실제 적용**(in-guest SDCom은 connect -5로 차단).
> §18이 화면비 하나만 쓰던 `CmnViewProcessDataMgr` vtable 경로를 **일반 per-GROUP 경로**로 확장. 전부 RAM-only·가역, 고유 인스턴스(grpscan.nand/46810/46811/PID)만, 광범위 pkill 미사용, 콜드부트.

### 23.1 핵심 발견 — CmnViewProcessDataMgr는 설정별 GROUP 번호를 받는 범용 read/write 슬롯 [S][V]
viewUnified2의 **모든 `WrapperXxx::setXxx(ViewBaseProduct*, enum)`** 가 §18과 동일한 전역 싱글턴(`sub_11BB1C`→`&unk_47FFF8`)을
얻어 **`vtable[14](obj, GROUP, internalValue)`** 로 쓰고 **`vtable[3](obj, GROUP, &out)`** 로 읽는다. GROUP은 설정마다 다르며
`setXxx` 본문에서 정적 추출:

| GROUP | 설정(Wrapper) | GROUP | 설정 |
|---|---|---|---|
| 1 | ImageAspect (화면비) | 26 | ArtFilter (사진 효과) |
| 2 | ImageQuality (화질 RAW/JPEG) | 29 | ObjectTracking |
| 7 | DriveMode | 31 | FaceDetectionSmileShutter (미소/얼굴) |
| 8 | FlashMode | 33 | AutoPortraitFraming |
| 9 | FlashCompensation | 35 | AutoMode |
| 13 | FocusMode | 36 | SceneMode |
| 20 | ExposureCompensation | 44 | AudioRecordingLevel |
| 22 | MeteringMode | 147 | RecordingMedia |
| 24 | DroHdr (DRO/자동HDR) | 187/188 | FocusArea (3-인자 변형) |
| — | — | 208 | StillSteadyShot · 225 NdFilter |

옵션별 **internal 값**은 getter 디코드(`sub_21FA82`/`sub_2219DE` 등: internal→Sony WRAPPER enum)와 **부팅 OSD 기본값 앵커**로 확정.
(⚠️ SDCom TSV의 `s_XXX D=` 값은 vtable internal과 **다른 공간** — metering multi는 D=1이나 internal=2. 추측 금지, D로 internal 대체 안 함.)

### 23.2 에뮬 라운드트립 검증 — `grpscan.so`/`grpscan2.so` [V]
각 GROUP에 `vtable[3]` READ(순수·안전) + 유효 대체값 `vtable[14]` WRITE → READ-back → 원복(§18 가드 전부 유지).
- **콜드부트 기본 모드 = SCN(장면, D=11)**. 이 모드에서 많은 설정이 **엔진 inhibit**(ret=0 거부) — **순정 메뉴가 회색처리하는 것과 동일**(`WrapperXxx::isInhOn`). feature parity.
- **P(program-auto)로 전환 후**(§19 `requestModelExecute("model/CAMERA",0x10,{0x12A=1})`, D=11→1) 재시도하면 잠금 해제.
- **라운드트립 VERIFIED(write→read-back 일치)**: SCN에서 **g1 aspect·g2 image-quality·g8 flash-mode**; P에서 추가 **g7 drive·g9 flash-comp·g22 metering·g24 dro-hdr·g31 smile**.
- **read-verified(슬롯 bound, 라이브값 읽힘)이나 에뮬서 write inhibit**(렌즈/AF/특정모드 필요): g13 focus·g20 exp-comp·g26 picture-effect·g29 object-track·g208 steadyshot·g35·g36·g44·g147·g187·g225. → 매핑은 정확(group 확정), 에뮬에 렌즈 없어 엔진이 적용 거부. 실기/렌즈연결 시 적용 예상.

### 23.3 배선 — 4개 설정 실적용(read-back 검증) [V]
`platform/a5000-inproc/engine.h`에 범용 `cvpdmGroupRead/Write(group,…)` 추가(기존 aspect는 group 1 래퍼). `apply.h`에 **IK_GROUP** + `inprocGroup` 필드.
`gen_apply.py` INPROC 딕셔너리 = `{key:(kind,group,{raw옵션:internal})}` → `apply_gen.cpp` 재생성. `camera_inproc.cpp::applySetting`이 IK_GROUP를 `groupSetChecked`(write+read-back)로 디스패치. **배선·확정 매핑**:

| catalog row | key | group | 옵션→internal | 검증 |
|---|---|---|---|---|
| 1 | image-aspect | 1 | 16:9→1 (3:2 internal 미관측→unmapped) | [V] §18 |
| 2 | storage-fmt | 2 | raw→0, rawjpeg→1, jpeg→2 | **[V] applytest 3/3 OK** |
| 29 | metering-mode | 22 | multi→2, center-weighted→1, center-spot→4 | **[V] applytest OK** |
| 56 | face-detection-mode | 31 | off→0, auto→1 | **[V] applytest OK** |

**콜드부트 end-to-end(`A5K_UI_APPLYTEST=1`, ui.log)**: uicore `applySetting()` 전체 경로로 위 전부 **APPLY_OK + read-back 일치**
(storage-fmt 3옵션 각 internal 0/1/2 왕복; P 전환 후 metering/face OK; SDCom-only(exp-comp/iso)는 정직하게 `NO_ROUTE`; beep=RECORDED; format/self-timer=LOCAL). 무크래시·oops 0·콜드부트 가역.

### 23.4 정직한 분류 — 왜 전부가 아닌가
- **에뮬 적용(검증 wired) 4**: image-aspect, storage-fmt, metering-mode, face-detection-mode.
- **route 검증됐으나 미배선(옵션별 internal 의미 확정 필요) [?]**: drive(g7)·flash-mode(g8)·flash-comp(g9)·dro/hdr(g24). 라운드트립은 성공 — 남은 건 각 메뉴 옵션↔internal 대응(디코드가 복잡: drive=셀프타이머/브라케팅 하위변형, dro/hdr=14값 결합, flash=auto/on internal 미확정; 부팅 앵커 off=internal2만 확실). **추측 배선 금지**(feature parity·정직성). 디코드 토큰(@Mxxx) 해석 또는 OSD 실측으로 확정 후 drop-in.
- **매핑 확정·에뮬 write inhibit(렌즈/AF 필요)**: focus-mode(g13)·exp-comp(g20)·picture-effect(g26)·steadyshot(g208) 등. 에뮬 한계일 뿐 매핑은 정확.
- **genuinely SDCom/native-only(model-param 등가물 없음)**: 피킹·제브라(VDF 0x59000E)·AF보조광·브라케팅 세부·ISO/WB/셔터/조리개(AE-active 엔진 경로)·색공간·HDMI/USB/REC폴더 등. 실기 SDCom 또는 VDF in-proc 별도 트랙.

### 23.5 산출물
- `emulator/tools/grpscan_so.c`·`grpscan2_so.c` — per-GROUP 라운드트립 검증 RE 도구(P 전환 포함). (emulator)
- `app/native/platform/a5000-inproc/engine.h`(cvpdmGroupRead/Write)·`camera_inproc.cpp`(IK_GROUP 디스패치 + applytest 확장). (ROOT)
- `app/native/uicore/include/uicore/apply.h`(IK_GROUP·inprocGroup)·`src/model/apply.cpp`·`tools/gen_apply.py`·`src/model/apply_gen.cpp`(재생성). (ROOT)
- ui.log(applytest: storage-fmt/metering/face OK + read-back) — 세션 스크래치.

### 23.6 OSD 검증 + 커버리지 확장 시도 — 방법 입증·한계 규명 [V] (2026-11-16)
`grpcmd_so.c`(신규 대화형 프로버: im.elf 상주 worker가 `/setting/a5k/cmd.txt` 폴링 → `G V`/`R G`/`MODE E` 실행, stock draw 미억제 → stock OSD 실측)로
GROUP↔internal 매핑을 **stock OSD 스크린덤프**로 실측. 고유 인스턴스(grpcmd.nand/46820/46821/PID)만, 광범위 pkill 미사용.
- **storage-fmt(g2) 완전 OSD 검증 [V]**: internal **0→"RAW"**, **1→"RAW+J"**(부팅 기본), **2→"X.FINE"(JPEG)** — 스크린덤프 3장으로 확정. §23.3 배선과 일치 ⇒ **decode(internal→Sony enum)+부팅 앵커 방법이 OSD로 검증됨**.
- **한계 1 — 모드 OSD 미갱신**: `requestModelExecute 0x10 {0x12A=1}`(→P)는 **엔진 상태는 바꿔(SCN서 잠긴 설정 write 해제, grpscan2) OSD 모드 표시는 SCN 유지**(0x12C가 OSD에 직접 반영 안 됨, §17.2). ⇒ P 전용 아이콘(플래시 등) OSD 확인 불가.
- **한계 2 — 아이콘 설정 판독 난**: 플래시는 SCN-landscape서 **아이콘 자체가 비표시**(장면이 플래시 비활성). drive/metering 아이콘은 표시되나 의미 식별이 reference 없이는 불확실. ⇒ 텍스트 OSD(storage-fmt=RAW/JPEG)만 신뢰성 판독.
- **한계 3 — idle warm-off**: 5분 무키입력 시 게스트 warm power-off(§11.5)로 세션 중단 → 장시간 대화형 매핑 제약.
- **2점 앵커 실측**(SCN/P 기본값, `R g` 순수 read): g8 flash=int2(SCN·P 동일), g24 dro=int1(SCN)/int2(P), g31 smile=int0(SCN)/int1(P), g13 focus=int6, g20 exp-comp=int15(=0EV). flash가 두 모드서 int2 동일이라 off/auto 의미 **단정 불가**(OSD 아이콘도 비표시) → **[?] 유지, 추측 배선 안 함**.

### 23.7 최종 상태 — 배선/검증 커버리지
- **배선 + 검증(에뮬 적용) = 4**: image-aspect(g1)·**storage-fmt(g2, OSD 3값 검증)**·metering-mode(g22)·face-detection-mode(g31). (총 `inproc 4`.)
- **route 라운드트립 검증·미배선([?], 옵션 의미 모호)**: drive(g7, 셀프타이머/브라케팅 하위변형)·flash(g8, SCN 아이콘 비표시+모드OSD 미갱신)·flash-comp(g9, int offset)·dro/hdr(g24, 14값 결합 enum·dro↔hdr 상호배타). 엔진 route는 동작 — **옵션↔internal 의미만 미확정**(실기 OSD 또는 아이콘 reference로 확정 후 drop-in).
- **매핑 확정·에뮬 write inhibit(렌즈/AF 필요)**: focus(g13)·exp-comp(g20)·picture-effect(g26)·steadyshot(g208)·object-track(g29). 순정 parity(모드/렌즈 의존 잠금), 실기/렌즈서 적용 예상.
- **산출물 추가**: `emulator/tools/grpcmd_so.c`(대화형 OSD 프로버). storage-fmt OSD 스크린덤프(RAW/RAW+J/X.FINE) — 세션 스크래치.
