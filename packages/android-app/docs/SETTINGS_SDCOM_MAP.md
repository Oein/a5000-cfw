# 설정 키 ↔ SDCom 파라미터 변환표 (NATIVE_PORT_PLAN 공백 R1)

데이터: `app/docs/settings_sdcom_map.tsv` (386행, 열 = setting_id / android_key / android_value / sdcom_paramId /
sdcom_value / source / verified / notes). 이 문서는 표를 만든 방법, 커버리지, 값 인코딩, SDCom 경로가 없는 항목을 설명한다.
확정되지 않은 내용은 `[?]`로 표시했다. 2026-11-15, 펌웨어 1.10 기준.

## 1. 결론 요약
- **변환은 HAL 한 곳에 있다.** `android_libcamera.so`(`/system/lib/libcamera.so`)의 `CameraHardwareDiadem::s_paramTable`
  (0x2a848, 16바이트 × 133항목)이 표 전체다. 항목 i = SDCom paramId **256+i**.
  `convertFromCamParams`(0x23920)가 `Camera.Parameters`를 이 표로 SDCom Int/IntArray 파라미터로 바꿔
  `setParameters`(0x21d10)가 **0x1003 CameraSetParameters**로 보낸다. `getParameters`는 **0x1002**를 보내고 같은 표를
  거꾸로 쓴다(`convertToCamParams` 0x23dcc). 지원 목록(`*-values`, `min-*`/`max-*`)은 **0x100a**
  (`s_supportedParamsTable` 0x2b098, 별도 id 공간 256~)로 온다.
- **SDCom에 실리는 값은 "A 쪽" 값이다.** enum = HAL 문자열 목록의 **인덱스**, bool = 0/1, int = 숫자 그대로,
  IntArray = CSV 정수. 엔진 값(D)으로 바꾸는 일은 ScalarDaemon이 한다(§4). 네이티브 a5kui는 HAL과 똑같이
  A 값을 보내면 된다. D 값은 표의 notes에 참고로만 적었다.
- **에뮬레이터 동적 검증 결과**: 13개 0x1002 스냅샷(설정 변경 전·후)에서 **101개 파라미터**의 0x1002 정수와
  앱의 `Camera.Parameters` 문자열이 이 표대로 일치했고, 불일치 0건(특수형 9개는 형식만 다름, §5).
  set 경로(앱 `POST /api/camera` 10건 + `sdprobe 0x1003` 24건)로 **30개 (paramId, 값) 쌍**을 set→get으로 확인했다.
- **앱 쪽 문제 발견**(코드 수정은 범위 밖, 보고만): ① 앱 ENUM 5개(`quick-af`, `mulit-shoot-nr-iso`,
  `digital-zoom-type`, `prv-mag`, `color-depth-type`)는 HAL에 `*-values`만 있고 **설정 키가 없다**. 앱의
  `p.set(key)`는 HAL이 버리고, 현재값도 항상 없음(`--`). ② `x-tracking-focus`(EXPERIMENTAL_ENABLED에 들어 있음),
  `x-nd-filter`, `x-silent-shutter`, `x-raw-rec`, `x-motion-shot`이 부르는 ParametersModifier 메서드는 a5000
  `sony.cameraex`에 **존재하지 않는다**(OpenMemories 스텁에만 있음) → 실기에서 `NoSuchMethodError`(Error라서
  `catch (Exception)`에 안 잡힘) [? 앱 크래시 여부 실기 확인 필요]. ③ `x-wind-noise`는 카메라 키가 아니라 오디오 키라
  `Camera.Parameters`에 넣으면 버려진다. ④ `movie-quality=std`가 쓰는 `VIDEO_ENCODING_BIT_RATE_STD`는 a5000 목록에 없다.

## 2. 방법
### 2.1 정적
| 대상 | 무엇을 뽑았나 | 도구 |
|---|---|---|
| `nflasha16/lib/libcamera.so` | `s_paramTable` 133항목 {key char*, type, values char**, count}, `s_supportedParamsTable`. 키/값 문자열은 `.rel.dyn` R_ARM_ABS32(→`libcamera_client.so`의 `CameraParameters::KEY_*`)와 RELATIVE로 해석 | 자작 ELF 파서(`python3 -I`), IDA 디컴파일(`re/ida/scalar/android_libcamera.so.c`) |
| `/usr/lib/libScalarDaemon.so` | `CommandProcessorCameraSetParameters::s_ADParamKeyConvTable`(0x1590e0, 20B × 111) = {paramId, 엔진 키, kind, 변환표 id, 저장 변수}; `g_ConvParamKeyTable`(0x168000, 12B × 81) = {변환표 id, (D,A) 쌍 배열, 개수}. 개수는 파일에 0이고 정적 생성자가 채운다(디컴파일 0xe5bf4 부근 `dword_168008 = 19` …) | 같은 파서 + 디컴파일 |
| `framework/sony.cameraex.odex` | `CameraEx.ParametersModifier` 각 setter가 쓰는 `Camera.Parameters` 키와 상수 문자열. odex 헤더가 `dey\n100`(소니 변형)이라 사본 헤더를 `036`으로 바꾼 뒤 baksmali 2.5.2로 디오덱스 | baksmali (jadx는 quick 명령어 때문에 본문 실패) |
| `libaudio.so`, `libmediaplayerexservice.so`(+`libmediaex.so`) | 오디오(`microphone-*`), 동영상(`output-format` 등) HAL 표 | 같은 파서 |

### 2.2 동적 (에뮬레이터, 실기 미사용)
- `qemu-system-arm-sgc1m` 사본, 런마다 pristine NAND 복제, 포트 46520~46522.
- Track A 디버그 APK를 USB(PMCA `install -d socket`)로 설치 → MENU → 애플리케이션 → 애플리케이션 목록 → a5000 UI 실행
  (이 상태에서만 `sdprobe connect(0x1540432,12)`가 0, 앱 없으면 -5 — 실기와 같음).
- 게스트에서: 기존 `app/native/sdcom/SDPROBE`(0x1002 읽기, `0x1003:52=0,<id>=<v>` 쓰기)와 임시 정적 HTTP 클라이언트
  (zig로 빌드한 musl 정적 바이너리, scratch에만 있음)로 앱의 `/api/camera/params`(=`Camera.Parameters.flatten()`)와
  `POST /api/camera`를 호출. 매 단계 0x1002 + params를 같이 떠서 표로 교차 검증.
- 주의: `am start`로 ILCE Remote를 직접 띄우면 앱이 널 쓰기로 죽고 Sony 예외 모니터(`Exception>`)에 걸렸다. 런처로
  띄우면 정상. 또 한 번은 유휴 중 av-cam(LIRO) 예외가 났다(기존 `RE_ENGINE_OOPS` 계열로 보임).

## 3. 커버리지 (A5kSettings.CATALOG 140항목)
| 유형 | 개수 | SDCom 경로 |
|---|---|---|
| ENUM/BOOL/RANGE | 53 | **48개 = 0x1003 파라미터 직결**(표에 paramId·값 전부). 5개는 가상 키(§6.1) |
| EXP | 6(+ EXP_VALUES 정의 13) | `x-steadyshot`→331, `x-movie-steadyshot`→372, `x-apsc`→350, `x-cinema-tone`→373, `x-shooting-preview`→333, `x-ael`→329, `x-hdr-level`→275는 0x1003. `x-wind-noise`는 0x3100(오디오). 5개는 a5000에 경로 없음(§6.2). `x-sys-poweroff`는 didep.Settings |
| PREF | 19 | 앱 자체 설정. 예외: `movie-format`/`movie-quality` → 0x300a MovieSetParameters(1792~1796), `movie-audio` → 0x3100(1024) [?] |
| INFO / ACTION | 46 / 16 | SDCom 0x1003 아님(§6.3) |

표에 나온 서로 다른 SDCom paramId는 68개. 0x1003 파라미터 중 앱 카탈로그가 안 쓰는 것(렌즈 보정 세부 307~315,
WB 시프트 374~376, RGB 매트릭스 377~378, 샤프니스 게인 386~387, 컬러 깊이 379~384, `pe-*-level`, 브라케팅 세부
319/321/324/325/327, `camera-gain` 371, `face-frame-rendering` 303, `auto-wb-lock` 305 등)도 `s_paramTable` 전체는
scratch 덤프에 있고 인코딩 규칙은 §4와 같다.

## 4. 값 인코딩
### 4.1 0x1003 메시지 형식 (HAL `setParameters` 그대로)
1. **첫 파라미터는 반드시 Int id 52(0x34) = `atogachi-mode`(0/1)**. ScalarDaemon `CommandProcessorCameraSetParameters`는
   첫 파라미터가 52가 아니면 아무것도 적용하지 않는다(`SyncProcessor::extractSDComMessage` 0xf5d90: `getId(first) != 52 → return`). 1이면
   `s_bForceSetMode`.
2. 이어서 바꿀 파라미터만 넣는다(HAL도 `createEmptyParameters`에 들어 있는 키만 보냄). 여러 개를 한 메시지에 넣어도 된다.
3. 응답: Int id 1(결과), Int id 90. HAL은 id 90 값을 저장(`this+120`). 에뮬레이터에서 성공한 set도 id1=0이었고,
   모드 때문에 무시된 set도 id1=0이었다 → **결과 코드로 적용 여부를 알 수 없으니 0x1002로 다시 읽어 확인할 것.**

### 4.2 타입별
| HAL type | SDCom 파라미터 | 값 |
|---|---|---|
| 1 enum | Int (type 1) | HAL 문자열 목록의 인덱스. 예: whitebalance `daylight`=4, `shade`=7, `color-temp`=15 |
| 3 bool | Int | `"true"`=1, 그 외 0 |
| 4 int | Int | `getInt` 값 그대로(ISO=숫자 100/400…, 0=AUTO; EV=1/3 스텝 정수 −9..9; 색온도=K) |
| 5 intarray | IntArray (type 5) | `"a,b,c"` → [a,b,c] (`color-select-channels` 302, `rgb-matrix` 378) |
| 10 special | — | 256 `picture-size` → IntArray [w,h] (`getPictureSize`); 317 `focus-point-x` → IntArray [x,y] (x/y 두 키를 합침). 268 `shutter-speed-nume`, 347~349 `max-mag-*`, 357~369 `image-pixel-size-*`은 **읽기 전용**(set 시 로그만) |
| 0 none | — | 288 `auto-review-time`, 289 `picture-destination`, 337/339~342 UNDEFINED: HAL이 보내지 않음 |

### 4.3 ScalarDaemon 쪽 (참고, a5kui가 할 일 아님)
`s_ADParamKeyConvTable`의 kind: 1 = `g_ConvParamKeyTable[변환표 id]`의 (D,A) 쌍으로 A→D, 표에 없는 A는 **거부**
(`convertAtoDValue` 반환 1); 2 = 정수 그대로; 4/5 = 전용 처리(점프 테이블 0xf6e66, 예: ISO는 변환표 5에서
A=ISO 숫자); 0 = 무시(258, 262, 289). 표 notes의 `SD s_<변수> D=<n>` / `NOT in conv table -> rejected`가 이것이다.
WB는 A 0..16 중 HAL 목록 전부 변환표에 있음(underwater 포함, 기종 지원은 별도).

### 4.4 상호 의존 (동적으로 관찰)
- **장면 모드(263)가 잠그는 항목**: `landscape`일 때 WB·ISO·EV·DRO·크리에이티브 스타일·사진 효과 set은 0x1002에 반영 안 됨.
  `aperture-priority`로 바꾸자 미리 보낸 WB 4, ISO 400, EV 3이 한꺼번에 나타났다(모드별 기억 값과 함께 272/304/318/320/355/388도 바뀜).
- 사진 효과(286)가 켜져 있으면 color-mode(282) set이 무시됨. RAW(330=1,2)이면 사진 효과 set 무시(330=0 후 성공).
- self-timer(328)=2를 넣자 drive-mode(318)가 burst→single로 바뀜.
- 한 번은 282=3이 반영된 뒤 다음 읽기에서 1로 돌아감 [? 앱의 재적용인지 엔진인지].
⇒ a5kui는 "set → 0x1002 재읽기 → UI 갱신"을 기본으로 하고, 모드 전환 뒤 전체 재읽기를 해야 한다.

## 5. 동적 검증 상세
- 읽기 경로: 0x1002 정수 ↔ `Camera.Parameters` 문자열이 101개 파라미터에서 모든 스냅샷 일치. 형식만 다른 특수형:
  256/358~367(IntArray [w,h] ↔ `"WxH"`), 268(`[n,d]` ↔ nume/deno 두 키), 317(`[x,y]` ↔ x/y 두 키).
- set 경로 확인 쌍(표 `verified=dyn(set+get)`): 260={4,7,15}, 263=18, 266={3,−3}, 269=400, 270=2, 272=0, 280=5500,
  281=−1, 290=2, 283=2, 286=1, 304=1, 306=1, 318={0,1}, 328=2, 329=1, 330=0, 333=3, 335=2, 344=1, 350=0, 351=1,
  355=2, 356=0, 388=0, 282=3.
- 에뮬레이터에서 반영 안 된 set(`dyn(rejected)`, 원인 [?]): 331=2(anti-blur onetime, OSS 렌즈 없음 추정),
  336=1(multi-shoot-nr), 370=2(flex-spot 크기), 앱 `focusMode=auto`(AF 렌즈 없음), 앱 `liveViewEffect`(332 그대로).
- 에뮬레이터가 보고한 a5000 지원 목록(`*-values`, min/max)을 notes에 `a5k-supported` / `not in …-values on a5000`로
  붙였다. 예: whitebalance 13종(fluorescent/twilight/underwater-blue·green 없음), color-mode 7종, flash-mode
  auto/on/off, image-aspect 3:2/16:9, focus-area-type multi/center/flex-spot/local/spot/wide, burst-drive-speed low만.
  [? saturation −16..16, max-pe-rough-mono 0 등 일부 범위는 실기와 다를 수 있음 — 실기 0x100a로 재확인]

## 6. SDCom 0x1003 경로가 없는 항목과 필요한 것
### 6.1 가상 키(앱 ENUM이지만 HAL 설정 키 없음)
| 앱 키 | 실제 경로 |
|---|---|
| `quick-af` | `CameraEx.setQuickAutoFocus` → HAL `sendCommand(0x1000001C)` `setQuickFocus` → **0x1006 CameraAutoFocus** + Int 66(0x42): af_wiae→0, af_woaf→1, afc_woaf→2. AF 트리거이지 저장 설정 아님. 메뉴의 "사전 AF" 저장 값은 [? Backup prop 후보] |
| `mulit-shoot-nr-iso` | 값 목록(ISO)만 존재. 다중 촬영 NR = 336 `multi-shoot-nr` + 269 ISO [?] |
| `digital-zoom-type` | `setDigitalZoomMode` → 344 smart / 345 super-resolution / 346 precision (bool). 배율은 0x1030 CameraSetDigitalZoom + Int 102(≥100), 해제 0x1031 |
| `prv-mag` | 353 `prv-mag-mode` bool + 354 `prv-mag-time` int. 실시간 확대는 `setPreviewMagnification`(네이티브 명령) [? msgId] |
| `color-depth-type` | `setColorDepth(c,n)` → 379~384 `color-depth-<색>` int −7..7 |

### 6.2 a5000에 경로 자체가 없음
`x-nd-filter`, `x-tracking-focus`, `x-silent-shutter`, `x-raw-rec`, `x-motion-shot`: a5000 `sony.cameraex`에 메서드가
없고 HAL `s_paramTable`에도 해당 키가 없다. 네이티브에서도 0x1003 경로 없음 [? 다른 메시지 존재 여부는 미조사, 기종상 기능 부재 가능성 높음].

### 6.3 0x1003이 아닌 다른 통로
| 항목 | 통로 |
|---|---|
| `x-wind-noise` | 0x3100 AudioSetParameters, param 1025 `microphone-wind-noise-reduction` off=0/on=1 ("auto" 없음) [? id: Audio AD 표 1024/1025 순서로 추정] |
| `movie-audio` | 0x3100 param 1024 `microphone-enable` false/true [?] (앱은 `setAudioSource`로 처리) |
| `movie-format` / `movie-quality` | 0x300a MovieSetParameters: 1792 output-format AVCHD/MPEG4, 1793 video-size, 1794 video-aspect, 1795 video-enc-bit-rate LP/SP/HQ/FS/FH/FX/PS, 1796 frame-rate 60i…24p. `std`는 목록에 없음 |
| `x-sys-poweroff` | didep.Settings → 네이티브는 Backup prop [? propId, `re/static/bkid_whitelist.md`] |
| SS / 조리개 | 0x101a / 0x1019 등(설정 파라미터 아님). 267/268은 0x1002 읽기 전용 |
| `info-na-*`(제브라, 피킹, AF 보조광, 언어/지역/PAL, HDMI, USB, REC 폴더) | Backup prop 또는 VDF/시스템 서비스(CLAUDE.md §5, `bkid_whitelist.md`) |
| `sys-*` ACTION, 재생 항목, Wi-Fi | 각각 시스템 서비스/파일 작업(NATIVE_PORT_PLAN §2, §5) |
| PREF(그리드, DISP, 비프, 밝기 …) | 앱 내부 상태, SDCom 불필요 |

### 6.4 앱 값 라벨 불일치 (표에 `app value label only` 행)
앱 VAL 표가 HAL에 없는 문자열을 라벨로 갖고 있다: image-size `middle`(HAL `midium`), flex-spot `s/m/l`(HAL
`small/midium/large`), burst-drive-speed `mid/hi`(HAL `middle/high`), color-mode `bw/night-scene/autumn-leaves`
(HAL `mono/night/red-leaves`), flash-mode `slow-sync/rear-sync`(flash-type 값). 실제 선택지는 `*-values`에서 오므로
동작에는 문제가 없고 라벨만 빠진다. `jpeg-quality`는 HAL에서 int(50/25)인데 앱 라벨은 extrafine/fine/standard.

## 7. 미결 [?]
1. 0x1003 응답 id 1/id 90의 의미(성공/무시 구별 불가). HAL은 id1==0을 오류로 처리하는 분기가 있어 해석이 엇갈림.
2. 위 rejected 5건이 에뮬레이터 한계인지 실제 제약인지 — 실기 확인.
3. Audio 1024/1025, Movie 1792~1796 id 배정(HAL `convertFromCamParams` 기준 id 확인 필요) 및 동적 검증.
4. `setPreviewMagnification` / `setColorSelectToChannel`의 네이티브 메시지 id(libcameraex_jni → HAL sendCommand 코드).
5. 사전 AF·절전 시간 등 저장 설정의 Backup propId.
6. 에뮬레이터 지원 목록 범위값 일부(saturation ±16 등)의 실기 일치 여부.

## 8. 재현
scratch(`re-r1/scripts/`): `dump_paramtable.py`(HAL 표), `dump_sd_tables.py` + `conv2.py`(데몬 표), `xcheck.py`
(0x1002↔params 교차), `gen_map.py`(이 TSV 생성). 디오덱스: odex 사본 헤더 `036` 패치 후 `baksmali x -a 10 -d <framework 사본>`.
디컴파일·디오덱스 출력은 저장소에 넣지 않았다.
