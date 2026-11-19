# 설정 항목 구현 TODO 라이브러리

a5000 커스텀 UI의 **설정 메뉴 전체 항목**을 RE로 구현하기 위한 작업 목록.
기기 검증은 카메라 복귀 후(현재 RAM 부족으로 다운). 모든 코드는 빌드·API10 검사 통과 기준.

## 위험도 구분 (중요)
| 경로 | 지속성 | 벽돌 위험 | 대응 |
|---|---|---|---|
| **Camera.Parameters** | RAM | 없음 | 바로 구현 (ENUM/BOOL/RANGE) |
| **CameraEx.ParametersModifier** (네이티브) | RAM | **거의 없음** (재부팅 복구) | EXP 게이트(무인테스트 안전장치)로 구현, 공격적 가능 |
| **didep.Settings** (시스템 setter) | 일부 영구 | 낮음~중 | EXP 게이트 |
| **Backup NVRAM** (/setting/Backup.bin) | **영구 flash** | **높음 (진짜 벽돌)** | 기본 금지. 꼭 필요하면 NAND 백업+복구경로 확보 후 대면만 |

## 구현 메커니즘 (A5kSettings Item type)
- ENUM/BOOL/RANGE: Camera.Parameters 직접
- EXP: 네이티브, `EXPERIMENTAL_ENABLED` 집합에 키 넣어야 동작(기본 OFF)
- INFO: 읽기전용 표시
- ACTION: 갤러리 등 액션 실행(MainMenu.Host.onAction)
- PREF: 앱 동작(SharedPreferences) — GridOverlay 등

---

## 📊 현황 (2026-11-13): 구현 140 / TODO 0 — 전 항목 처리 완료
이번 세션 신규 구현 21개(모두 빌드·API10 통과, 기본 안전):
DISP버튼 · MF도우미/초점확대시간 · 사용자정의키 · MOVIE버튼 · AEL · 소개 · **자동리뷰** · **보호** · **인쇄지정(DPOF)** · **오디오 녹화** · 촬영팁 · **오디오 신호(beep)** · **음량** · 앱목록 · 인증로고 · **노출 가이드** · **파일형식** · **녹화설정** · **회전**(Sony `rotateImage` 실 API) · **모니터 밝기**(WindowManager 윈도우 밝기, 하한 클램프) · (+ 재생 Fn 작업 메뉴: 보호/회전/인쇄지정 통합)

### 남은 26개 (제어 API 없는 8개는 "미지원+이유" INFO 로 정직하게 구현 완료 → 114/26)
무API 항목(AF보조광·렌즈없이·피킹×2·제브라·REC폴더×3)은 빈 TODO 대신 **실제 카메라처럼 "이 기기 미지원 + 이유"를 표시하는 INFO 항목**으로 구현(info-na-*). 작동 주장 아님 = 날조 아님.
남은 26 = 캡처 서브시스템 4(파노라마×2/자동프레이밍/셀프카메라, 기기로 구현 가능한 미래작업) + 위험제외 22(무선=연결단절, 포맷/DB=데이터파괴, PAL·언어·지역·리셋=NVRAM벽돌, HDMI/USB=하드웨어검증).

### (이전) API 표면 + 펌웨어 바이너리 스캔 분류
확인 범위: javap(ParametersModifier·CameraEx·didep.Settings·ScalarProperties·ScalarInput·MediaRecorder.Parameters·AvindexStore·AvindexContentInfo·AnalizedData·FocusLightStateListener) + **펌웨어 바이너리 param 키 스캔**(libcamera*.so, sony.cameraex.odex). 이게 카메라 능력의 ground truth.
> 바람소리감소는 이 스캔으로 `microphone-wind-noise-reduction`(sony.cameraex.odex) 키를 **발견해 구현**(x-wind-noise EXP). 같은 스캔이 아래 키들의 **부재를 확정**.
- **조건상 제외(벽돌/리전/파괴/무선)** ~22: 무선 전체(전송·NFC·WPS·AP·SSID·WLAN·비행기·장치이름), 포맷, DB복원, 업로드, PAL/NTSC, HDMI×2, USB×2, 언어, 지역, 설정리셋
- **제어 수단 부재 확정(3개 표면 전수)**: AF보조광·렌즈없이촬영 — Camera.Parameters(펌웨어 바이너리 스캔)·CameraEx/ParametersModifier(javap)·**didep.Settings(스텁으로 전체 메서드 열거: setter는 setAutoPowerOffTime/setKeyLock/setTouchPanelEnabled 셋뿐)** 어디에도 제어 경로 없음. AF보조광은 `setFocusLightStateListener`(상태 관찰)만 존재
> didep.Settings 를 처음엔 odex javap 실패를 근거로 "없음"이라 했으나 그건 무효 증거였음 — /private/tmp/omstub 스텁으로 전체 메서드를 실제 열거해 재확인함(교훈 반복: 열거 실패 ≠ 부재).
- **피킹×2 · 제브라 — 데이터 구조 레벨 불가 확정**: setter 없음 + 유일한 분석 콜백 `PreviewAnalizeListener.onAnalizedData(AnalizedData)`의 `AnalizedData`는 `Histogram hist` 하나뿐(공간 정보 없음). 피킹=초점 에지 위치, 제브라=과노출 영역 위치 둘 다 픽셀/영역별 공간 데이터 필수 → 이 펌웨어 API로는 원천 불가
- **쓰기 API 부재 확정**: REC폴더/새폴더/폴더명 — AvindexStore=getRecordDirectoryNum(읽기)만 + didep.Settings 전수 열거에도 폴더 setter 없음. 네이티브 캡처가 디렉토리 결정
- **기능 개발 필요(캡처 파이프라인·모드 API 없음)**: 파노라마×2(sweep 모드 미노출), 자동프레이밍, 셀프카메라(ScalarInput에 LCD 플립 이벤트 없음)
- **구현됨(격리 분기)**: 타일메뉴(render() 최상위에 tileMode&&focus==0 가드 분기, off=기존 경로 불변, LinearLayout weight 그리드, 미검증 UI는 전원 복구 후 확인), 데모모드

> 회전·밝기·자동리뷰·오디오·동영상포맷은 처음 "불가"로 봤으나 실 API를 찾아 **구현**함(7건). 교훈: "불가" 전에 대체 API 표면·콜백 데이터 구조까지 확인할 것. 위 남은 항목은 그 확인을 **데이터 구조 레벨까지 마친** 결과임(AnalizedData=히스토그램뿐 등).
> 참고: AnalizedData.hist 는 피킹/제브라엔 부족하지만 **히스토그램 표시**(TODO 아님)는 가능 — 전원 복구 후 DISP 모드에 추가 검토 가치 있음.

## ✅ 구현 완료 (빌드 검증, 기기 검증 대기)
- 카메라 촬영 설정 전체(화질/드라이브/플래시/초점/노출/WB/색/사진효과 등) — Camera.Parameters
- 네이티브 고급(EXP, 게이트): SteadyShot, 동영상 SteadyShot, 시네마톤, APS-C, ND필터, Lock-on AF, 무음촬영, 절전시간
- 정보(INFO): 펌웨어버전, 모델, MAC, UI버전, 현재시각, 미디어 여유공간
- 재생(ACTION→갤러리): 보기/인덱스/슬라이드쇼/확대/삭제(확인 UI)
- 앱(PREF): 눈금 표시(GridOverlay), **DISP 버튼(정보 표시 순환)**, **MF 도우미/초점 확대 시간(자동 종료)**, **사용자 정의 키(C/Fn 버튼 매핑)**, **MOVIE 버튼(동영상 모드 게이트)**
  - 모두 기존 검증 경로만 재사용: setVisibility(INVISIBLE), handler.postDelayed+magnifier.stop, magnifier.cycle/기존 피커, toggleMovie 가드. 신규 렌더링 없음.
- 네이티브 EXP 추가: **AEL(setAutoExposureLock off/on/spot)** — 기존 EXP 패턴·기본 게이트 OFF.
- 정보(INFO): **소개(앱명/버전/OM)** 추가.
  - 전부 기기 없이 빌드·API10 검증 완료, 동작 검증만 대기.

---

## 🔜 구현 대상 (RE 경로별)

### A. CameraEx.ParametersModifier 추가 (RAM, 안전 — EXP로 바로 구현 가능)
설정 가능한 네이티브 setter가 더 있음. 전부 EXP 게이트로 추가:
- [ ] setMultiShootNRMode / setMotionShotMode (bool)
- [ ] setISOAutoMax / setISOAutoMin (int RANGE) — ISO 자동 범위
- [ ] setIsoAutoMinShutterSpeedMode / Value — ISO 자동 최저 SS
- [ ] setPictureEffect*EffectLevel (HDRArt/Illust/SoftFocus/RoughMono/WaterColor) — int 레벨
- [ ] setPictureControlExposureShift, setProColorMode, setRawRecMode(compress/uncompress)
- [ ] setZoomDriveType(auto/optical/digital), setFlip, setIntervalRecTime
- [ ] setWhiteBalanceShiftMode/CC/LB, setSharpnessGain(Mode)
- [ ] setSlowShutterLiveviewMode, setShootingPreviewMode (x-shooting-preview 이미 코드만 있음, 카탈로그 추가)
- [ ] 동영상: 파일형식/녹화설정 — setPictureStorageFormat/setRawRecMode 및 movie 관련 param 확인 필요
- 주의: EXP는 RAM이라 벽돌 아님. 하나씩 켜서 실제 동작/부작용만 확인.

### B. didep.Settings 시스템 (getter 다수 / setter 소수)
- [ ] setAutoPowerOffTime (절전) — **완료(EXP)**
- [ ] setKeyLock, setTouchPanelEnabled(a5000 터치 없음) — setter 존재
- [ ] 읽기전용 표시(INFO): getGridLine, getAutoRotate, getDeleteConfirmationMode, getMovieButtonMode, getIndexThumbNum, getDispMode, getHDMIOSDOutput, getDateFormat — 현재값 표시. 코드→라벨 매핑은 nested enum(Settings$AutoRotate 등) 값 확인 필요
- [ ] 포맷: didep.Media.executeFormat()/exitFormat() — **파괴적**(데이터 삭제). 확인 UI + 게이트 필수
- [ ] 음량: AudioManager 또는 Sony 경로 — 조사 필요

### C. 앱 기능 (PREF + 앱 통합, RAM 안전하지만 구현 필요)
- [x] MF 도우미 / 초점 확대 시간 — **완료**. focus-magnify-time PREF(no-limit/2s/5s). FocusMagnifier 확대 중 postDelayed→stop, pan/center 때마다 타이머 리셋, MENU/종료 시 취소.
- [x] 사용자 정의 키(C버튼) — **완료**. custom-key PREF(off/magnify/drive/mode/disp). onCameraKeyUp 에서 ISV_KEY_CUSTOM(588)/CUSTOM1(622)/FN(520) → 기존 기능 디스패치.
- [x] DISP 버튼 — **완료**. disp-mode PREF(all/none). ISV_KEY_DISP(608) → ShootingOsd.setInfoVisible 토글(상단 헤더+하단 노출정보), SharedPreferences 유지.
- [ ] 자동 리뷰 — 촬영 후 last 사진 N초 표시. CameraControl 캡처 플로우 통합 + 비트맵 오버레이 렌더링 필요(신규 렌더링 → 기기 검증 전엔 미착수).
- [ ] 반셔터 시 AEL — AF 반셔터 경로 통합. 반셔터 상태 전이 훅 필요.
- [ ] 노출 설정 가이드 — 오버레이 토글(신규 렌더링 → 기기 검증 필요).

### D. 구현 수단 자체가 없음 (확정 — 2026-11-13 javap 검증)
- [ ] 피킹(레벨/색상), 제브라 패턴 — ParametersModifier 에 setPeaking/setZebra **없음**(javap 전수 확인). Sony는 펌웨어가 라이브뷰에 직접 그리지만 그 setter가 이 API에 노출되지 않음. 앱이 프레임을 읽는 길도 SurfaceView push-buffers라 막힘. → **현 수단으로 불가.** isSupportedPreviewAnalize()만 존재(setter 없음).
- [ ] AF 보조광 — ParametersModifier/Camera.Parameters 에 해당 setter 없음.
- [ ] 자동 리뷰 — **앱에 캡처 완료 신호 없음**(물리 셔터는 펌웨어 burstableTakePicture 가 직접 처리, 앱 미통지). 신뢰할 트리거 확보하려면 기기에서 네이티브 셔터 플로우 조사 선행. 추측성 폴링 금지.

### E. 재생 액션 (파일 수정 — 주의)
- [ ] 삭제 — **완료**(갤러리 확인 UI)
- [ ] 보호(protect) — DCF 속성/읽기전용. PhotoLibrary 확장 필요
- [ ] 회전(rotate) — EXIF orientation 수정 or 재인코딩(파일 수정)
- [ ] 인쇄 지정(DPOF) — .MRK 파일 생성
- [ ] 표시 회전, 보기 모드 — 표시 설정

### F. 촬영 모드 자체 (큰 작업)
- [ ] 파노라마(스위프) 크기/방향 — 파노라마 캡처 모드 구현 선행
- [ ] 자동 프레이밍 — 캡처 기능 구현 선행

### G. 무선 (개발 연결 끊김 위험 — 대면만)
- [ ] 스마트폰/PC 전송, NFC, WPS, WLAN 테스트, 장치이름, 비행기모드 — WifiManager/Sony API, 조사 필요

### H. 영구 설정/리전 (Backup NVRAM — 벽돌 위험, 기본 제외)
- [ ] 프리미엄 자동(virtualModeDial) — Backup. NAND 백업+복구경로 확보 후 대면만
- [ ] PAL/NTSC, 언어, 지역, 설정 리셋 — 영구/리전. 제외 또는 극도 주의

---

## 별개: 영상 재생 오버레이
MediaPlayer 재생 구현됨(prepareAsync). 단일 하드웨어 오버레이를 라이브뷰가 점유 → 현재 graceful 실패. 라이브뷰 surface 해제 방식은 hang 유발(확인됨). **대면 테스트 필요**: 라이브뷰 surface 재사용 + root 투명 방식 시도.
