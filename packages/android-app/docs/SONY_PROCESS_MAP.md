# Sony 프로세스·스레드·커널모듈 지도 — "무엇을 남기고 무엇을 들어낼 수 있나" (Track B)

> 작성 2026-11-15, **RE 전용**(코드 커밋·펌웨어 수정·실기 접촉 없음). 측정은 전부 **에뮬레이터**(`emulator/`,
> QEMU `sony-a5000`, 순정 펌웨어 1.10 무수정, 사용자 NAND pristine 클론, `A5K_SHELL` UART2 루트 셸)에서.
> 표기: **[V]** = 에뮬 동작 관찰, **[S]** = 정적(디컴파일/IMDB/심볼) 근거, **[?]** = 미확정.
> 배경: CLAUDE.md §1(벽돌 위험)·§2(두 트랙)·§5; `re/NOTES.md` §8·§10·§11; `re/static/SUMMARY.md`;
> `app/docs/NATIVE_PORT_PLAN.md`·`NATIVE_DEPLOY.md`·`TRACKB_RUNTIME.md`; `emulator/docs/RE_USERLAND_SUBCPU.md`.
> **CPU 수치는 에뮬 상대값**(에뮬은 실기 ~0.35× 속도, HZ 집계도 다름). 비율(%)만 의미. 메모리는 /proc 실측(KB).

---

## 0. 한 줄 결론 + 사용자 지침 반영

목표 = **Sony의 소프트웨어 "처리"는 100% 그대로**(카메라 엔진·이미지 파이프라인·JPEG/RAW/동영상 인코딩·AF/AE/AWB·
렌즈/센서 제어·미디어 DB·화질), **UI 레이어(순정 뷰/메뉴)·Android·그 외 군더더기만** 교체/제거. 각 항목을
**`[유지=Sony 처리]`** / **`[교체·제거=UI/Android/군더더기]`** 로 분류하고, 유지 항목에는 **"동일 출력 보장하 최적화 가능?"**
열(아이디어 + 에뮬에서 등가성 증명법)과 **flash 변경 필요(벽돌 티어, 설계만)** 표시를 달았다.

**핵심 측정(순정 Diadem 촬영화면 유휴, §1):**
- 유휴 전체 CPU 중 **av-cam 엔진(liro, 127 ITRON 태스크) ≈ 28%**, **im.elf ≈ 24%**, **커널스레드 ≈ 7%**,
  **Android 전체 ≈ 0.2%** [V]. → Android는 유휴 시 CPU를 거의 안 쓰지만 **RAM을 ~77 MB 점유**(아래).
- 메모리(PSS): **Android 13개 프로세스 합계 = 77.3 MB** [V] (총 175 MB 중 44%). im.elf 자체 RSS ≈ 28.8 MB
  (+ `/dev/stream` 엔진 DMA 매핑 ~34 MB는 하드웨어 버퍼라 순 RAM 아님).
- **런타임 제거 실험 [V]**: `kill -STOP`으로 **zygote·system_server·mediaserver·vold·netd·installd 전부 동결** →
  **순정 촬영화면이 그대로 렌더되고 엔진(liro)·im.elf는 정상 계속**(스크린덤프 `r5/stop.png`, 엔진 22%/im.elf 19%).
  ⇒ **Diadem(촬영) 경로에서 Android는 critical path가 아니다** = 런타임 제거 가능. (단 Scalar "앱 모드" 진입 =
  SDCom D2A·순정 런처 UI에는 Android가 관여 — §4.)

---

## 1. 전체 프로세스/모듈 표 (측정 + 분류)

메모리 PSS(KB), CPU = 65.7 s 유휴 윈도에서 전체-CPU(4코어) 대비 %. "제거?"는 **런타임**(플래시 비기록) 기준.

### 1.1 네이티브(비-Android) 사용자 프로세스

| 프로세스 | 역할 | PSS(KB) | CPU% | 필요 주체 | 분류 | 런타임 제거? (증거) |
|---|---|---:|---:|---|---|---|
| **im.elf** (pid≈145) | Sony 메인 멀티스레드 프로세스: Rigel Window Server·ScalarDaemon(SDCom)·엔진 object 에이전트(ObjCamera/Renderer/Player/MovieRecorder)·미디어/네트워크 Infra·**그리고 순정 UI(appFw+MINT+viewUnified)**. §3 내부 분해 | 64931\*(RSS 28872) | ~24 | 거의 전부 | **혼합** — 대부분 유지, UI 스레드만 교체 | ✗ 통째 제거 불가(엔진·Rigel·SDCom 숙주). UI 부분만 교체(§3) |
| **av-cam** (liro-kliro_N, 127 ITRON 태스크, `liro.ko`가 커널 내 실행) | **카메라 엔진 RTOS**: 센서/ISP, AF/AE/AWB, 노출 시퀀스, 캡처/현상, 라이브뷰 VDF, 렌즈·서브마이컴 링크 | 커널(§1.3) | ~28 | 촬영 전부 | **유지(Sony 처리 코어)** | ✗ 절대 불가(엔진 그 자체) |
| **bootin.elf** (130) | im.elf 부트 스테이지 전달자(IMDB `0x940005` system()) | 575 | ~0 | im.elf 기동 | 유지(부팅 글루) | ✗(부팅 전용, 유휴 무해) |
| KIKILOG_* (157/566) | Sony 로그 링버퍼 | 소 | ~0 | 진단 | 군더더기(진단) | △ 런타임 제거 가능하나 무해, 저비용 |

\* im.elf PSS 64931 KB는 smaps 합(= `/dev/stream` 엔진 DMA 매핑 34 MB 포함). 순 익명+라이브러리 RSS는 28.8 MB.

### 1.2 Android(chroot `/android`, CPU3 핀, memcg 113 MB) — **전부 "군더더기" 후보**

| 프로세스 | 역할 | PSS(KB) | CPU% | 왜 존재 | 분류 | 런타임 제거? |
|---|---|---:|---:|---|---|---|
| **system_server** (424, 47스레드) | Android 프레임워크(Window/Input/Power/Battery Manager 등) | **40277** | 0.1 | SF+앱 런처 구동 토대 | 교체·제거 | ✓ 동결해도 엔진/UI 무사 [V] |
| **zygote** (172) | Dalvik VM 포크 토대(surfaceflinger 내장) | 13633 | 0 | 앱·SF 프로세스 생성 | 교체·제거 | ✓ [V] |
| **scalaralauncher** (497, 9스레드) | 순정 "애플리케이션 목록" 런처 앱 | 9872 | 0 | 앱 모드 UI | 교체·제거 | ✓ [V] |
| **arasoftkeyboard** (475, 7스레드) | 소프트 키보드(네트워크 입력 등) | 6014 | 0 | 텍스트 입력 | 교체·제거 | ✓ [V] |
| **avindex.process** (492, 7스레드) | Android측 미디어 인덱싱 보조 | 4301 | 0 | 갤러리/공유 | 교체·제거(※DB 본체는 네이티브 InfraDatabase) | ✓ [V] |
| **mediaserver** (173, 5스레드) | Android StageFright(스트리밍/썸네일 보조) | 2249 | 0 | Wi-Fi 전송/재생 | 교체·제거 | ✓ [V] |
| vold/netd/installd/servicemanager/debuggerd/ueventd/init | Android init 계열 | ~1000 합 | 0 | Android 부트 | 교체·제거 | ✓ [V] |
| **Android 합계** | | **77333** | ~0.2 | | **교체·제거** | ✓ 전부 동결 시 촬영화면·엔진 정상 [V] |

> **런타임 제거 실험(§0)**: Android 6개 프로세스 `kill -STOP` 후 `r5/stop.png` = 완전한 촬영 OSD, 엔진·im.elf 정상.
> **Diadem 경로에 Android 불필요**. Android가 실제로 필요한 유일 경로 = **Scalar 앱 모드 진입(SDCom D2A/런처)**,
> 그리고 이는 Track B에서 **네이티브 런처 APK + 네이티브 UI**로 대체(§4, `TRACKB_RUNTIME.md` §3).

### 1.3 커널 모듈(lsmod, 33개) — Sony 드라이버

| 모듈 | 크기 | 역할 | 분류 | 제거? |
|---|---:|---|---|---|
| **dmm** | 2.41 MB | **DRAM 관리자**(엔진 물리연속 버퍼 할당) | 유지(엔진 필수) | ✗ |
| **liro** | 50 KB | av-cam 엔진 로더/커널내 실행 호스트 | 유지(엔진) | ✗ |
| **backup** | 59 KB | `/setting/Backup.bin` propId R/W (영구 설정) | 유지(설정 영속) | ✗ |
| **osal_uipc/osal_utm/osal_ulogio/utimer** | 86 KB | OSAL IPC 메시지큐·타이머(SDCom·Rigel·엔진 전송계층) | 유지(IPC 백본) | ✗ |
| **grm_ma / grm_gles** | 27 KB | GPU 메모리(물리연속)·GLES2 커널 | 유지(합성/디스플레이) | ✗ |
| **nand / lld / ldec / dmac / devno** | ~190 KB | NAND FTL·LZ 디코더·DMA | 유지(저장/부팅) | ✗ |
| **ms_drv / mmc_drv / mcmn_drv** | 319 KB | SD/MS 카드 드라이버 | 유지(저장매체) | ✗ |
| **sprop** | 10 KB | 센서 prop | 유지(센서) | ✗ |
| **input / indctr / sircs / i2c / udate** | 64 KB | evdev 입력·인디케이터·리모컨·I2C·RTC | 유지(입력/HW) | ✗(input=우리 UI도 event0로 사용) |
| **hdmi / cec** | 171 KB | HDMI 출력·CEC | 유지(처리/IO) | △ HDMI 미사용 시 비로드 가능(부팅 트리밍, flash) |
| **usb_portMonitor / usbg_sen / usb_extcmd** | 51 KB | USB 게이트웨이(MTP/충전/확장명령) | 유지(USB) | △ |
| **nfc** | 9 KB | NFC 리소스 | 유지(무선) | △ NFC 미사용 시 비로드(flash) |
| **accel / mag / compass** | 18 KB | 자이로/지자기(손떨림·수평계·지오태그) | 유지(처리) | △ |
| **stream / stream2** | 3 KB | 엔진 영상 스트림 노드(`/dev/stream*`) | 유지(라이브뷰/캡처) | ✗ |
| **kikilog** | 24 KB | 진단 로그 | 군더더기(진단) | △(flash 트리밍) |

**결론(모듈)**: 33개 중 **"UI 전용" 모듈은 없다**. 전부 엔진·저장·입력·IO 처리 계층. 제거 가능한 것은
**미사용 주변장치(HDMI/CEC/NFC/kikilog)뿐이고 그나마 IMDB 비로드 = flash 변경(벽돌 티어)**. 런타임 이득 ≈ 0.

---

## 2. 분류 요약: "Sony 처리 — 유지" vs "UI/Android/군더더기 — 교체·제거"

### 2.1 유지 (Sony 소프트웨어 처리, 출력 동일 보장 필수)
- **av-cam 엔진 전체**(liro 127 태스크): 센서/ISP, AF/AE/AWB, 노출·캡처 시퀀스, RAW 현상, 라이브뷰.
- **im.elf 내 처리 서비스**: ObjCamera/ObjRenderer/ObjPlayer/ObjMovieRecorder/ObjStillRecorder/ObjAudio* (엔진 object
  에이전트), libmpr(동영상/정지화 코덱·DCF 미디어매니저), InfraStillPool/Database/CacheMgr(미디어 DB), InfraMediaConfig/
  Common/Fsys/Mount/Detect(저장), InfraAvbbc/MovieEncoder/StillEncoder/Decoder(인코딩), libsencore(센서코어),
  libScalarDaemon(SDCom 서버), **Rigel Window Server + libObj GPU 드라이버**(합성/디스플레이), InputServant(evdev 생성),
  네트워크/USB/HDMI Infra(IO).
- **커널 모듈 33개 중 32개**(§1.3).

### 2.2 교체·제거 (UI 레이어 + Android + 진단 군더더기)
- **Android 전체(77 MB)**: zygote/system_server/scalaralauncher/arasoftkeyboard/avindex/mediaserver/vold/netd/installd/…
  → Track B 네이티브 UI + (필요시) 네이티브 런처 APK로 대체. 최종 standalone 단계에선 완전 제거.
- **im.elf 내부 순정 UI 레이어**(§3.2): appFw.so + MINT(libmintx) + viewUnified2-8 + cmn_view_*/BizFw + Caution*.
  → 우리 UI(Rigel 클라이언트)가 대체. **단 im.elf 통째 교체 = flash(벽돌 티어)**; 런타임에는 "유휴화"만 가능(§3.3).
- **진단**: kikilog/KIKILOG(저비용, 선택적 flash 트리밍).

### 2.3 혼합(주의)
- **im.elf**: 한 프로세스 안에 처리 서비스(유지)와 UI(교체)가 **같은 주소공간**. 런타임엔 분리 불가 — 프로세스를 죽이면
  엔진 글루·Rigel·SDCom까지 죽는다. §3 참조.
- **libObj.so**(15 MB 파일, RSS 8 MB): GPU 드라이버 + Rigel WindowServer + 엔진 object 에이전트 + 일부 렌더 = **대부분 유지**.
  순수 "뷰 트리"는 viewUnified*/appFw 쪽이고 libObj는 그 아래 처리/합성 계층.
- **avindex.process**(Android): 인덱싱 "보조"일 뿐, **미디어 DB 본체는 네이티브 InfraDatabase/StillPool**(유지). avindex는 제거.

---

## 3. im.elf 내부 분해 (UI 스레드/라이브러리 vs 우리 UI가 필요한 서비스)

im.elf = 158 스레드. 이름·소속 라이브러리는 `re/ida/libIMDB.imdb_raw.txt`(IMDB 스테이지 테이블) + `/proc/145/maps`
smaps + 스레드명 실측(`r1`, `r5`)으로 교차 확인.

### 3.1 유휴 촬영화면에서 CPU를 쓰는 im.elf 스레드 [V]

| 스레드(tid명) | 전체-CPU% | 소속 | 성격 |
|---|---:|---|---|
| **THREAD_INFRA0** | ~11 | libObj/Infra 워커 | 합성/디스플레이·엔진 글루 (OSD→GRM→VDF 매 프레임). **대부분 유지** |
| **Appframework** | ~11 | **appFw.so** | **순정 UI 애플리케이션 프레임워크**(뷰/메뉴 엔진). 유휴에도 OSD 재그리기. **교체 대상** |
| grm_dn_vdf_if | ~1 | libObj(grm) | GRM→VDF 디스플레이 인터페이스. 유지(Rigel 경로) |
| I-InputRepeat / monio(input) | ~0.5 | InputServant | evdev 입력. 유지 |
| uipc_proxy_0..3 | ~0 | osal_uipc | IPC. 유지 |

> **의미**: 순정은 **아무 조작 없는 촬영화면에서도 Appframework가 ~11% CPU를 상시 소모**(OSD 상태 재그리기). 이게
> UI 교체로 줄일 수 있는 CPU의 핵심. 엔진(28%)과 합성(THREAD_INFRA0 11%)은 유지라 남는다.

### 3.2 im.elf 주소공간의 **순정 UI 라이브러리**(= 교체 대상) [S, smaps]

| 라이브러리 | 파일크기 | RSS(KB) | 내용 |
|---|---:|---:|---|
| **appFw.so** | — | (소) | 앱 프레임워크(Application_System_Init, AppConfig.so 로드) — UI 수명/뷰 시스템 |
| **libmintx.so** (MINT) | 1.49 MB | 868 | Sony **MINT UI 툴킷**(MintUser/MintString/MintMap, 위젯/씬) |
| **viewUnified2.so** | 4.72 MB | 3136 | **메인 메뉴/뷰 트리 본체**(6탭/20페이지/106항목; ViewBase/CmnView) |
| viewUnified3-8.so | 0.23–0.55 MB | ~0.2 each | 하위 뷰(재생/설정/네트워크 등) |
| cmn_view_processDataMgr / cmnviewViewModel / CmnViewIroiroConDataMgr / cmnviewMRUtil | 0.56 MB+ | 소 | 뷰 데이터 모델/유틸 |
| libBizFw.so / libBizFw2.so | 0.31 MB | 260/196 | 뷰-비즈니스 로직 |
| CautionConfig / CautionRsrcConfig / ModelCautionConfig | 1.61 MB | 1404 | 경고/주의 다이얼로그 |
| `/usr/share/app/*.uxc,.uxb` | — | ~1.4 MB(디스크) | UI 리소스(이미지/문자열/스타일/레이아웃) |

이들이 **순수 UI** = Track B에서 우리 Rigel 클라이언트 UI로 대체. 합계 RSS ≈ 6–7 MB + 리소스.

### 3.3 im.elf 서비스 라이브러리(= 우리 UI가 의존, 유지)
libObj(Rigel WS+GPU+엔진 에이전트), libScalarDaemon(SDCom), libmpr/libmprctrl/libclema(코덱·미디어), libSysDef,
libMWF, InputServant, libInfra*(Database/StillPool/CacheMgr/Media*/Fsys/Usb*/Hdmi/Nfc/Wlan/Net/LiveViewClient…),
libHapiLib/libStm(Scalar HAPI = Android 카메라 HAL 브리지), libsencore, arbiter(ABT), libosal_*.

### 3.4 im.elf UI를 **런타임에 유휴화**할 수 있나? vs flash 필요
- **런타임 가능(벽돌 위험 0)**: UI를 "안 그리게" 만들려면 **Scalar(앱) 모드 진입** = im.elf가 순정 뷰 대신 외부
  클라이언트(우리 Rigel 창)를 합성. 이때 appFw의 메인 서피스가 떨어지고(`ctx+8=0→유효`) 우리 창이 최상단
  합성됨 [V, `TRACKB_RUNTIME.md` §1.3]. 순정 뷰 스레드(Appframework)는 여전히 상주하지만 **전면 그리기를 멈춰 CPU↓**.
  단 "앱 모드"로 바꾸는 정식 트리거는 런처 APK(Android) 또는 외부 `ABT SET_A`([V]지만 상태머신 파손 위험, §4 주의).
- **flash 필요(벽돌 티어, 설계만)**: appFw/viewUnified를 **아예 로드하지 않게** 하려면 IMDB 스테이지 테이블(스테이지 `a`,
  row 102 `appFw.so Application_System_Init`)을 수정 → **im.elf 교체 or `/usr` flash**. 이건 CLAUDE.md §1 **최고 위험(진짜
  벽돌)**. standalone 전환 단계 전까지 금지. 런타임 유휴화로 CPU 대부분을 이미 회수 가능하므로 우선순위 낮음.

---

## 4. Android가 실제로 필요한 유일 경로 + Track B 대체

| 순정 기능 | Android 의존 | Track B 대체 | 상태 |
|---|---|---|---|
| Scalar 앱 모드 진입(SDCom `connect` 허용 = D2A state 5) | im.elf가 "앱 목록" 진입 시 arbiter `SET_A`→ScalarDaemon open | **네이티브가 ABT cmd=2 직접 송신**(앱 없이 SDCom 오픈) [V] / 정식은 투명 런처 APK | [V] (단독 SET_A는 상태머신 파손 위험 → 런처 APK가 안전, `TRACKB_RUNTIME.md` §3.3) |
| 전 키 수신(MENU/셔터/재생/줌) | 전면 앱의 `AppInfoReceive`→ABT cmd 3 | 투명 런처 APK + 네이티브 event0 읽기(+직접 ABT cmd 3) | [V] (§`TRACKB_RUNTIME.md` §2.3) |
| SurfaceFlinger 합성 | zygote/system_server | **불필요** — Rigel WindowClient 직접(`rglCreateWindow("UX_DEBUG_CLIENT")`) | [V] (SF는 Rigel 클라 하나일 뿐) |
| 화면 출력 | — | libObj/libdihal dlopen(bionic, chroot 안 root 실행) | [V] |
| 네이티브 UI 프로세스 실행 | OpenMemories 백도어(nativeExec, chroot 밖 root) | APK 번들 바이너리 → tmpfs → exec | [V] (`NATIVE_DEPLOY.md`) |

⇒ **현실적 Track B**(실기 벽돌 위험 0): Android **대부분**(system_server/launcher/keyboard/avindex/mediaserver)은
`kill -STOP`/미기동으로 유휴화 가능하고 엔진·UI에 무해 [V]. 단 **얇은 Android 런처 APK 1개**는 남겨
앱 모드 진입·전키·백도어 실행을 담당(투명 Activity). **완전 standalone(Android 0)**은 im.elf가 Android 없이
ScalarBase+서피스를 붙이는 진입 메시지 RE 미완([?], `TRACKB_RUNTIME.md` §3.3 단서) + WBI 콜드부트 RE 필요 → flash 티어.

---

## 5. "유지" 항목별 — 동일 출력 보장하 최적화 가능? (+ 에뮬 등가성 증명법)

> 원칙: Sony 처리의 **출력/동작은 비트 동일**해야 함. 아래는 그 전제에서의 자원 절감 아이디어와 **증명 방법**.
> **flash** 표시 = 벽돌 티어(설계만, standalone 전까지 금지).

| 유지 항목 | 최적화 아이디어(출력 불변) | 등가성 증명법(에뮬 A/B) | flash? |
|---|---|---|---|
| **av-cam 엔진** | 손대지 않음(엔진=출력 그 자체). 유일 여지: 미사용 모듈 비로드로 엔진 외 부하↓ | 해당 없음(엔진 코드 불변) | — |
| **im.elf UI 상시 재그리기(Appframework 11%)** | 앱 모드로 전환해 순정 뷰 전면그리기 중단, 우리 UI는 **변경 프레임만** post(dirty-rect) | A: 순정 OSD / B: 우리 UI. `-trace dmp_smaph_job`의 `p3d-draw <target>` **프레임 카운트 A/B 비교** → 우리 쪽이 적어야 하고, **SDCom 메시지 트레이스 동일**(설정/촬영 명령) | 런타임(앱모드)=아니오 / 완전 비로드=**flash** |
| **Rigel 합성(THREAD_INFRA0/grm 11%)** | 유지. 단 우리 UI가 **투명 영역(alpha0)**로 라이브뷰 평면을 가리지 않으면 OSD만 소량 합성 | 패널 레지스터 `0xF3440144`(YUV/OSD on 비트) + ch51/ch53 base **A/B 동일** 확인(라이브뷰 평면 불변), OSD 합성량만 비교 | 아니오(런타임) |
| **미디어 DB(InfraDatabase/StillPool)** | 유지. Android `avindex.process` 보조만 제거 | SD에 생성되는 **DB 파일(DCF/인덱스) 바이트 동일** diff(avindex 유/무 A/B) | 아니오 |
| **동영상/정지화 인코딩(libmpr/Avbbc/Encoder)** | 유지(화질 불변). Android mediaserver(StageFright) 보조만 제거 | 동일 촬영 입력으로 **생성 JPEG/MP4 바이트/해시 A/B 동일** 확인 | 아니오 |
| **설정 적용 경로(SDCom)** | 유지. 우리 UI→SDCom 메시지를 순정과 **동일 msgId/param**으로 | 순정 메뉴 토글 전후 **SDCom 트레이스 + Backup.bin diff** vs 우리 명령 → 바이트 동일 (`re/scalar/*.tsv` 맵) | 아니오 |
| **미사용 주변장치(HDMI/CEC/NFC)** | IMDB에서 해당 Infra 스테이지·`.ko` 비로드 → RAM·부팅시간↓ | 부팅 로그·lsmod A/B, 촬영/재생/설정 **기능 회귀 없음** 확인(해당 기능 미사용 전제) | **flash**(IMDB/`/usr` 수정) |
| **진단 로그(kikilog/ULOGIO)** | 비로드 또는 `dmesg -n 1`급으로 억제 | 기능 회귀 없음 + RAM/CPU A/B | **flash**(비로드) / 런타임(억제) |
| **Android 전체(77 MB RAM)** | 유휴화(kill -STOP) → 최종 비기동 | §0 실험: 동결 후 **촬영화면·엔진·SDCom A/B 정상**(`r5/stop.png`) | 런타임=아니오 / 부팅서 비기동=**flash**(init 스크립트) |
| **im.elf 자체를 경량 프로세스로 재작성** | 처리 서비스만 담은 slim im.elf (UI 라이브러리 제외) → RAM↓ | 전 SDCom/Rigel/엔진 **메시지 트레이스 + 캡처 출력 바이트 A/B 동일** | **flash**(바이너리 교체=진짜 벽돌, standalone 전까지 금지) |

---

## 6. Track B standalone 단계 권장 최소 집합

### 6.1 유지(필수, 벽돌 위험 0으로 실기 가능)
- 커널 + 33 모듈(미사용 주변장치 제외는 flash라 **일단 전부 유지**).
- **av-cam 엔진**(liro) — 불가침.
- **im.elf** 전체 프로세스(Rigel WS·ScalarDaemon·엔진 에이전트·Infra·HAPI). UI 라이브러리는 **상주하되 유휴**(앱 모드).
- **우리 네이티브 UI**: bionic 바이너리(chroot `/android` root 실행) = Rigel 클라이언트 + event0 + SDCom(glibc 헬퍼).
- **얇은 런처 APK 1개**(투명 Activity): 앱 모드 진입·전키·백도어 exec 담당.

### 6.2 런타임 제거(벽돌 위험 0) — 실기 즉시 적용 가능
- Android 중 **system_server/scalaralauncher/arasoftkeyboard/avindex/mediaserver** 유휴화 또는 미기동 →
  **~65 MB RAM 회수**(런처 APK용 zygote 최소분은 유지 가정). 엔진·UI 무사 [V].

### 6.3 flash 변경 필요(벽돌 티어 — 설계만, 전면 검증·NAND 복구경로 확보 전 금지)
- IMDB 스테이지 테이블에서 **appFw/viewUnified 비로드**(순정 UI 완전 제거) → im.elf 교체 or `/usr` flash.
- **Android 완전 비기동**(init `s_start_android.sh` 수정) → 77 MB 전부 회수.
- 미사용 모듈(HDMI/CEC/NFC/kikilog) 비로드 → RAM·부팅시간.
- slim im.elf(UI 제외) 재작성 → 추가 RAM.
- **전부 `/system`·`/usr`·NAND flash = CLAUDE.md §1 최고 위험**. standalone 전환 단계에서만, WBI 재생성 RE 완료 후.

---

## 7. 재현 (이 세션)
- 바이너리: `<scratchpad>/re-procs/qemu-procs`(=shared `qemu-system-arm-sgc1m`), pristine NAND 클론 per run.
- 기동: `A5K_SHELL=46581 A5K_NAND=… A5K_QEMU=… ./run.sh -display none -monitor tcp:127.0.0.1:46580,server,nowait -M usb-socket=127.0.0.1:46582`.
- 측정 도구(스크래치 `re-procs/`): `src/psnap.c`(단일 프로세스 /proc 스냅샷: 수백 fork 없이 전 태스크 stat/smaps 덤프 —
  ARM musl static, `psnap c`=CPU, `m`=메모리, `s PID`=매핑별), `tools/parse.py`·`agg.py`(CPU 윈도·그룹 집계), `tools/run.py`.
- 데이터: `r5/cw_a.txt`·`cw_b.txt`(65.7 s CPU 윈도), `r5/mem_diadem.txt`(메모리), `r5/stop_*.txt`·`r5/stop.png`(Android 동결 실험),
  `r2/smaps_im.txt`(im.elf 매핑별), `re/ida/libIMDB.imdb_raw.txt`(스테이지 테이블).
- 주의: 호스트 부하 높으면 엔진 oops `PC 0x4394aa4c, liro-kliro_113`(에뮬 vCPU 기아, `emulator/docs/RE_ENGINE_OOPS.md`,
  펌웨어/우리코드 무관) → Exception 모니터로 떨어짐. 유휴 5분 후 전원절약(키 주입으로 유지).

## 8. 남은 unknown [?]
1. Android 없는 정식 Scalar 진입 메시지(im.elf가 ScalarBase+서피스 attach) — standalone 핵심(`TRACKB_RUNTIME.md` §3.3 단서).
2. WBI 콜드부트/스냅샷 재생성(standalone 부팅) — `re/static/SUMMARY.md` §5.
3. THREAD_INFRA0 11% 중 "합성(유지)" vs "appFw 렌더 글루(교체)" 정확한 분해 — 스레드명이 세분 안 됨.
4. 미사용 모듈 비로드 시 엔진 부작용 유무(HDMI/CEC/NFC가 엔진 초기화에 암묵 의존하는지) — flash 전 실측 필요.
