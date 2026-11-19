# 트랙 B 네이티브 포팅 계획 (Java 앱 → 네이티브 프로세스)

> 작성 2026-11-15. 원천: 포팅 계획 분석(읽기 전용) + `NATIVE_DEPLOY.md`(배포 RE) 조정.
> 관련: `NATIVE_DEPLOY.md`(USB 배포·root 실행), `TRACKB_RUNTIME.md`(Rigel/SDCom/입력 런타임, 작성 중),
> `emulator/docs/RE_WIFI.md`, `emulator/docs/RE_NFC.md`(작성 중), CLAUDE.md §1·§2·§5.

## 0. 조정된 결론 (두 분석의 충돌 정리)

| 쟁점 | 배포 RE | 포팅 계획 | **채택** |
|---|---|---|---|
| root 실행 | Sony 네이티브 디버그 커맨드 서비스(osal_uipc, Tweak `nativeExec`와 같은 경로)로 한 줄 명령 → **chroot 밖 root** | G-ROOT 미해결 | **해결됨**: 런처가 이 서비스로 명령을 보낸다 |
| 툴체인 | glibc(chroot 밖) | bionic(chroot 안) — Rigel 클라 라이브러리(`gralloc.diadem.so` `rgl*`, `libdihal.so` `ws_hal_*`, `libma.so`), `libjpeg.so`, `libarbiter_proxy.so`가 `/android/system/lib`에만 있음 | **메인 UI = bionic**, root로 `busybox chroot /android /a5k/a5kui` 실행(백도어로 시작). **glibc 헬퍼** = glibc 전용 라이브러리(`libmpr.so` 동영상 재생 등) |
| 공통 | `libsdcom.so`·`libosal_uipc.so`는 양쪽에 있음. event0은 Android `/dev`에서도 읽힘(Track A `DialReader`) | | |

남은 핵심 공백: Rigel UX_DEBUG_CLIENT 창 합성 세부(alpha/슬롯), D2A(state 5) 진입 조건, **R1** 설정 문자열 키 ↔ SDCom (paramId, 값) 변환표.

## 1. 프로세스 구성
```
[런처 APK dev.oein.a5knative]  Java ~300줄, minSdk10, 투명 테마
   onResume: AppInfoReceive(ABT) 통지 → 엔진 D2A + 전키 수신, 키는 전부 삼킴
             Sony 디버그 커맨드 서비스로: 번들 lib*.so → /android/a5k/ 복사 → chroot /android 로 a5kui 실행(root)
   onPause:  SIGTERM → a5kui가 10초 안에 SDCom disconnect·창 해제
   [임시] Wi-Fi 브리지: WifiManager/DirectManager를 유닉스 소켓으로 노출
      │ 유닉스 소켓 /a5k/ctl
[a5kui]  bionic C++ 메인 UI (단일 스레드 poll 루프 + 워커 1)
      │ 유닉스 소켓
[a5kmpr] glibc 헬퍼 (libmpr 동영상 재생 등 glibc 전용)
```

## 2. Java 앱 인벤토리 → 네이티브 대응 (요약)
`app/camera-ui/.../ilceremote/` 49파일 12,814줄.

| 기능 | Java API | 네이티브 경로 |
|---|---|---|
| 키/다이얼 | ScalarInput, event0, AppInfoReceive | event0 EV_KEY = ISV_KEY(CLAUDE.md §5), ABT 통지 [GAP: 휴지통/Fn/DISP/C 코드] |
| 화면 | Android View | Rigel `rglCreateWindow("UX_DEBUG_CLIENT",640,480)` + `ws_hal_alloc`×2, 소프트 RGBA 캔버스, 논리 853×480 → X 0.75 압축 [GAP: alpha/슬롯] |
| 라이브뷰 | startPreview, CameraSequence | SDCom `0x1000`/`0x1004`, VDF 평면 아래 깔림, `0x0604`/`0x0603`; MJPEG용 `0x0124`/`0x0127` [GAP] |
| 설정 140항목 | Camera.Parameters, ParametersModifier, didep.Settings | `0x1003`/`0x1002` param 256~388 [GAP R1 변환표], UIGet* `0x08xx`, 네이티브 전용은 Backup prop(EXP 게이트) |
| SS/F/EV, 셔터, AF, 줌, 확대 | CameraEx | `0x101a/0x1019/0x102d`, `0x100b/0x100c/0x1008`, `0x1006/0x1007`, `0x102e/f`, `0x1032/3/6` |
| 이벤트 | 리스너 | `RegisterEvent 0x0001` [GAP: 등록↔수신 ID 왕복, StoreImageComplete] |
| 동영상 녹화 | scalar MediaRecorder | `0x3000`→`0x300a`→`0x3002/3003` [GAP: 모드 전환] |
| 갤러리 | AvindexStore, OptimizedImage | DCIM 스캔 + EXIF 썸네일 + libjpeg 스케일 디코드 [GAP: 삭제 시 IMDB 일관성 → 삭제는 게이트] |
| 동영상 재생 | (Track A 불가) | a5kmpr: `MprIf::PlayMovie` [GAP: 인자·VDF 소유권] |
| 배터리/온도/방향 | BatteryManager 등 | `0x0706` 온도 [GAP: 배터리·방향 osal 경로] |
| Wi-Fi / HTTP API | WifiManager, DirectManager, NanoHTTPD | §5 |

## 3. 아키텍처
- C++ 부분집합, `-Os -fno-exceptions -fno-rtti`, `-march=armv7-a -mthumb -mfpu=vfpv3-d16`(NEON 없음).
- 레이어: platform(rigel, sdcom, input, backup, wifi, mpr, host 시뮬레이터) → gfx(소프트 캔버스, 구운 폰트/아이콘 아틀라스) → ui(위젯·화면 스택, Track A 시각 규격 그대로, Track A 스크린샷 = 골든) → model(CameraState, `A5kSettings.java`에서 생성한 SettingsCatalog) → app(MainActivity 로직 이식).
- 이벤트 루프: `poll()`로 event0, SDCom self-pipe, timerfd, 워커 pipe, ctl/HTTP 소켓.
- 영속: SD `ILCEREMO/`(Track A와 같은 형식), `/setting` 쓰기 없음, 시작 시 마지막 설정 재적용(A2D 복원 대응).
- 크래시 안전(CLAUDE.md §1): SEGV/BUS/ILL/FPE/ABRT/TERM/INT/HUP 핸들러 → 이벤트 해제 → SDCom disconnect → 창/버퍼 해제 → `_exit`. libsdcom 객체 크기 `static_assert`(IntParam 0x10, IPC 0x44, Msg 0x48). `SD_DRYRUN=1`, `RGL_DRYRUN=1`. 상태 변경 명령은 `--allow-write` + EXP 게이트.
- 메모리: a5kui RSS ≤ 6MB(폰트 ~400KB, 아이콘 ≤1MB, 썸네일 ~1MB, 단일뷰 ~1.6MB), 화면 버퍼는 MA 공간. 20MP 풀 디코드 금지.

## 4. 마일스톤 (각 단계 에뮬레이터에서 검증)
- **M0** 창 + 키 + 런처 + 안전장치: 호스트 시뮬레이터, UX_DEBUG_CLIENT 테스트 패턴, event0, ABT, 시그널 정리, `a5kctl key/shot`, 런처 APK. 검증: USB 설치 → App List 실행 → 순정 위에 패턴 → `sendkey` 로그 → 앱 종료 시 프로세스 정리.
- **M1** SDCom 코어 + 촬영 OSD: 상태 읽기, 휠로 SS/F/EV/ISO, 반셔터 AF, 셔터, 이벤트, 설정 복원.
- **M2** 메뉴 + 설정 140항목: 선행 R1 변환표(정적 HAL/ScalarDaemon 표 + 에뮬레이터 동적 diff), 카탈로그 생성기, 메뉴(캐스케이드)/퀵바/모달/피커, set→get 자동 순회.
- **M3** 갤러리/재생: DCIM, 썸네일, 스케일 디코드, 줌, 슬라이드쇼; M3b a5kmpr 동영상(실기 전용 가능성).
- **M4** 라이브뷰 연동: 레이아웃 추종, 확대, 그리드, 히스토그램, 초점 프레임, MJPEG.
- **M5** 나머지: 동영상 녹화, 셀프타이머, 드라이브, 리뷰, 비프, 밝기, Wi-Fi/HTTP(§5), NFC, 시스템 액션.
- **M6** standalone(런처 없이)·교체 전환: CLAUDE.md §2 조건 전까지 착수 금지.

에뮬레이터 한계(현재): 라이브뷰는 테스트 패턴, 정지영상 캡처는 RAW 현상 단계에서 멈춤(진행 중), 렌즈 UART, 휠 회전, 오디오, Wi-Fi/NFC 모델 없음(RE 진행 중).

## 5. Wi-Fi (OpenMemories 비의존, 직접 설계)
**설계 결정 (2026-11-16): OSD Wi-Fi 상태 = 4상태 아이콘 1개(NONE 숨김·공간 없음 / CONNECTING 호 순차 점등 / CONNECTED 3호 풀 / AP 풀+"AP" 라벨)**. `CameraState.wifiStatus`, 우상단 그룹(남은장수 · Wi-Fi · 배터리 · %), NONE이면 자리 없음, 호/점은 4x4 커버리지 AA, 디버그 텍스트 줄은 재현하지 않음. 호스트 fake: `A5K_FAKE_WIFI=none|connecting|connected|ap`.

카메라 계층: `ath6kl_sdio.ko` + `rootfs_usr/wlan/ath/*` 펌웨어, `wpa_supplicant` v2.0-devel(P2P), `dhcpd`, 네이티브 WCM `libNetService.so`, Android `libhardware_legacy.so`는 osal로 네이티브 CT 서비스(`CT_ID_*`: START_WPA_SUPPLICANT, GET_MAC_ADDRESS, SET/GET_DEVICE_NAME, SET_NETWORK_ENABLE, 저장 AP, GO credential, WPS PIN …)에 요청. 순정 서비스: orb-server(:10000, 원격 제어 WebAPI), DLNA, PTP.

| 단계 | 기능 | 방식 |
|---|---|---|
| W1 (M1~2) | 상태 읽기 | CT GET 계열 [GAP W-CT 큐/형식], 임시 `wpa_cli status` |
| W2 (M5) | 스테이션 | 임시 런처 Java 브리지(`StationMode`), 최종 CT START_WPA_SUPPLICANT. 자체 supplicant 금지 |
| W3 (M5) | AP(Wi-Fi Direct GO, 192.168.122.1) | 임시 브리지(`AccessPointMode`), 최종 CT GO credential + P2P_GROUP_ADD + dhcpd [GAP] |
| W4 (M5) | 네이티브 HTTP 서버 | NanoHTTPD 대체, Track A `/api/...` 전부, 원본 스트리밍, 디버그 API는 이중 게이트 |
| W5 (M5) | 개발 접속 | 개발 설정 시 `busybox telnetd`, HTTP PUT 업로드 |
| W6 | 순정 무선 기능(스마트폰/PC 전송, TV, NFC, Smart Remote, WPS …) | 1단계 순정 화면으로 넘김, 2단계 CT/orb-server/DLNA/PTP RE 후 네이티브 |

NFC 원터치: `emulator/docs/RE_NFC.md` 결과로 W3과 묶어 설계.

## 6. 디렉터리
```
app/native/a5kui/
  Makefile (TARGET=ndk|host), src/{core,platform,gfx,ui,model,app,net,ctl}/,
  helper/a5kmpr.cpp (glibc), gen/ (생성물), tools/ (gen_catalog.py, gen_sdcom.py, extract_icons.py, bake_fonts.py; python3 -I),
  assets/ (펌웨어 파생 — 공개 저장소 커밋 금지), launcher/ (Gradle APK, check-api-level.py), test/{host,emu}/
```

## 7. RE 공백 우선순위
1. Rigel 창 합성 세부 + D2A 조건 (M0) — `TRACKB_RUNTIME.md` 진행 중
2. R1 설정 변환표 (M2)
3. 이벤트 ID 왕복
4. W-CT 프로토콜
5. DCIM 삭제와 IMDB 일관성
6. `MprIf::PlayMovie` 인자
7. 배터리/방향/AutoPowerOff osal 경로
8. 포맷·DB 복원 (in-process 외 방법 없음 → 비활성 유지)
9. 미캡처 키 코드

## 8. 이식성: UI 코어 라이브러리 (2026-11-15 추가)
a6400 이식을 위해 UI 코어를 플랫폼과 분리한다(`cfw/docs/A6400_PORTABILITY.md`: a6400에는 SDCom·Rigel·Android가 없고, ivica00 방식 RAM mod loader + im.elf 후킹이 진입로 — 사용자 실기 확인).
```
app/native/uicore/            libuicore (정적 라이브러리) — 플랫폼 헤더 0개
  include/uicore/platform.h   Display / Input / CameraControl / Storage / Clock / Log 인터페이스
  src/{core,gfx,ui,model,app}/
app/native/platform/a5000/    Rigel 디스플레이, event0 입력, SDCom 카메라 제어, 크래시 안전, main → a5kui
app/native/platform/host/     맥 시뮬레이터(SDL2/PPM + 가짜 카메라)
app/native/platform/a6400/    (예정) im.elf 후킹 기반 어댑터
```
- CameraControl은 설정 카탈로그 id 단위로 추상화: a5000 = `settings_sdcom_map.tsv`로 SDCom 변환, a6400 = 기종별 매핑 표.
- 키 코드는 플랫폼 중립 코드 + 플랫폼별 변환표.
- 입력은 **추상 액션**(NAV_*, CONFIRM, BACK, MENU, SHUTTER_HALF/FULL, DIAL_MAIN/SUB_CW/CCW, ZOOM_T/W, FN, CUSTOM_n, TOUCH 등)만 화면이 소비한다. 기종별 버튼 차이(a5000: 컨트롤 휠·MENU/PLAY/MOVIE/휴지통/줌 레버, a6400: 전·후면 다이얼·C1~C3·AEL·AF-ON·Fn·터치 등)는 `platform/<model>/keymap.*`의 **데이터 표**(물리 키 → 액션, 화면 문맥별 오버라이드, 사용자 재매핑 저장)로만 다룬다.

## 9. 텍스트 입력 (순정 키보드보다 개선) — 2026-11-15 추가
물리 키보드가 없으므로 글자 입력 설정이 필요한데, 순정 그리드 키보드는 2D로 칸을 헤매서 느리다. **베끼지 말고 개선**한다(설계 북극성: 조작 개선). uicore `ui/widgets`에 적응형 텍스트 입력 위젯을 둔다.
- **대상(TEXT 입력)**: 장치 이름(`sys-devicename`), Wi-Fi SSID/비밀번호(`info-na-wifi-config`), (가능 시) 폴더명/커스텀명. 설정 카탈로그에 TEXT 타입 추가.
- **a5000 주력 = 휠 기반 1D 입력 + 어시스트**: 2D 격자 대신 컨트롤 휠로 가로 문자 스트립을 스크럽 → CONFIRM 확정. 그룹(abc/ABC/123/기호)은 FN/좌우 전환, DELETE=백스페이스. 상단에 최근값·추천·프리셋 줄(장치 이름은 트랙 A 프리셋을 빠른 선택으로 유지 + 자유 입력). 자동완성.
- **a6400 = 터치 QWERTY**: 터치가 있으므로 전체 쿼티. 같은 위젯 인터페이스(`editText(field, 제약)`)를 쓰고, 플랫폼 `InputCapabilities`(has_touch/has_dial)로 입력 방식을 자동 선택 → Input 추상화에 능력 플래그만 추가.
- **라이브 검증·마스킹**: Wi-Fi 비밀번호 8~63자/SSID 길이 입력 중 검사, 비밀번호 마스킹+보기 토글.
- **순정 키보드 = 기능 기준선(참고용), 설계 목표 아님**(골든은 "이겨야 할 기준").
- 적용 경로: 장치 이름=CT `SET_DEVICE_NAME`, Wi-Fi 자격증명=CT/Direct(§5 W3) — 재확인 [?].
