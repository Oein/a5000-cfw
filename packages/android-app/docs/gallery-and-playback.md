# 갤러리 · 재생 세션 정리 (2026-11-13)

트랙 A(Android 앱, `camera-ui/`) 갤러리/재생 전면 개선 세션. 실기 검증은 telnet/adb + 디버그 API(:8080) 키주입+스크린샷 루프로 수행.
깊은 RE 근거는 `NOTES.md` · `TODO_TRACKB.md`, 설정 항목은 `TODO_library.md`.

---

## 0. 핵심 아키텍처 사실 (이 세션에서 확정)

### HW 사진 디코드 = OptimizedImage / OptimizedImageView
- `com.sony.scalar.graphics.OptimizedImageFactory.decodeImage(avindexKey, Options)` → `OptimizedImage`(DeviceMemory = HW 메모리, Bitmap 아님).
- `Options.imageType`: **THUM=1**(EXIF 썸 160×120), **SCN=2**(스크린네일 ~1080p), **MAIN=3**(풀 20MP).
- 표시는 `OptimizedImageView`(SurfaceView/HW YUV 오버레이) — `setOptimizedImage` / `setScale(float, BoundType)` / `translate(Point, TranslationType)` / `setPivot(Point)` / `setDisplayRotationAngle`.
- **제약 1**: OptimizedImage → Bitmap 변환 경로 없음(getWidth/Height/ImageFormat/isValid/release만). → grid 12장 동시 HW 표시 불가.
- **제약 2**: HW 오버레이 평면이 1개 → OptimizedImage는 "한 번에 하나"만 안전(2개 동시 보유 시 hang). 라이브뷰·동영상 VDF 평면과도 경합.
- 디코드 키: `avindex://...` (`PhotoLibrary.hwIndexKey(folder, file)` = AvindexStore 쿼리). 디코드 전 `AvindexStore.getExternalMediaIds()` 초기화(grain-lab 패턴).

### 순정 재생 전략 (RE 확정, `rootfs_usr/lib/libObj.so`)
- 순정은 **2단계**: 평소 단일뷰는 **SCN(스크린네일)만**, **MAIN(풀)은 줌할 때만** 디코드. (`pbCapSize` 640×480/640×360/1080p 상한, `"Already keep Main YC → 없으면 ReqDeliver"`.)
- 프리페치(PreCache) + 비동기 콜백 + 선행 디코드 취소(ClearPriorReadCache) + 미스 플레이스홀더(setReadingImageCenter).
- 우리 앱은 프리페치는 HW 1개 제약(hang)으로 **미채용**, 2단계 줌은 채용.

---

## 1. 갤러리 단일뷰 (`Gallery.java`, `HwPhotoView.java`)

| 항목 | 내용 |
|---|---|
| **HW 디코드** | 이동/fit 표시는 **SCN 우선**(853×480 패널에 과충분, MAIN의 수분의 1 비용). `HwPhotoView.decodeKey` SCN→실패시 MAIN. |
| **Progressive 로딩** | 이동 시 이전 이미지 유지 + "불러오는 중" 표시, **멈춤 0.2초 디바운스** 후 SCN 디코드 → 교체. 빠른 스크롤 시 디코드 스킵(토큰). (중간에 썸네일 플레이스홀더를 시도했으나 디코드가 오히려 느려져 제거) |
| **중앙 기준 줌** | `OptimizedImageView` 기본 pivot=(0,0) 좌상단이라 좌상단 확대됨 → **줌 직전 `setPivot(뷰 중앙)`**. (펌웨어 역어셈블 근거: ViewInfo.pivot = Point(0,0)) |
| **줌 2단계(MAIN 교체)** | 1.7배부터 백그라운드로 MAIN 디코드 → 준비되면 조용히 교체(SCN 화면 유지, "확대 중" 텍스트 없음). one-at-a-time: 교체(applyImage)가 SCN release + MAIN set을 묶어 순간만 2개 → hang 회피. STEP 1.15. |
| **pan** | `translate(Point, TRANS_TYPE_INNER)`. 방향 반전(화면 이동 방향에 맞춤). |
| **EXIF 오버레이** | **OK(ENTER) = EXIF 토글**(확대 아님, 확대는 T 레버). `F1.8 / 1/1600 / ISO3200` + `35mm / 날짜`. 좌상단 반투명 박스. 사진 넘겨도 유지·값 갱신, 로딩중·동영상·그리드엔 숨김. 데이터는 `PhotoLibrary.details(id)`(aperture/exposureTime/iso/focalLength/date). |
| **HALT 방지** | 매 이동 `OptimizedImageView.release`(StopRendering) 반복이 HAL 불안정→죽음 유발 → `hideHwQuick`(setVisibility GONE만)로 변경. 실제 clear()는 close/showGrid/동영상 전환 시 1회. DB 쿼리(hwKey/orientation)도 io 스레드로. |

## 2. 갤러리 그리드(인덱스) 뷰

| 항목 | 내용 |
|---|---|
| **UP 버그** | grid에서 UP이 `toggleGrid()`로 단일뷰 탈출하던 것 → **윗 행 이동**(`moveGrid(-COLS)`). 단일뷰 진입은 OK 또는 T 레버만. |
| **페이지 넘김** | 선택 칸이 속한 **12장 페이지 단위 정렬**(`gridTop = (index/perPage)*rows`). 마지막 칸에서 ▶ → 다음 12장(커서 첫 칸), 첫 칸에서 ◀ → 이전 12장(커서 마지막 칸), 윗줄 UP → 이전 12장. 한 행씩 스크롤 안 함. |
| **커서 우선 로딩** | 썸네일을 커서 거리순으로 제출(단일 스레드 io → 제출 순서=로딩 순서). |
| **썸네일 경량화** | `PhotoLibrary.micro()` 소스를 1080p 스크린네일 → **EXIF 썸(수 KB) 우선**으로. 최초 캐시 생성이 대폭 빨라짐(1080p 디코드/재인코딩 제거). 레터박스는 CENTER_CROP이 가림. 캐시 `/sdcard/ILCEREMO/THUMB`. |

## 3. 동영상

### 썸네일
- `.THM` DCF 사이드카 = **160×120 저해상 JPEG**이 원본 → 원래 저화질(우리 재인코딩 아님). 사진처럼 1080p 스크린네일이 없어 한계.

### 재생 — **트랙 A 불가 확정** (상세 `TODO_TRACKB.md §5`)
실기 4경로 전부 **기기 하드 리셋**:
1. `android.media.MediaPlayer`(stagefright/OMX)
2. `com.sony.scalar.media.MediaPlayer` + `setDisplay` + `setZOrderOnTop(true)`
3. 위 + `setZOrderOnTop` 제거(hole-punch)
4. 위 + `setDisplay`도 제거(surface 미전달, Sony 레퍼런스 `MediaPlayerManager`와 동일 시퀀스)

- **원인**: HW 비디오 디코더(VDF 평면)는 im.elf/MprIf 엔진 전용. scalar MediaPlayer는 올바른 엔진 경로(`libmediaex.so` IMediaPlayerExService→MprIf)지만, **우리 Android 앱 프로세스 윈도우 안에서** prepare/start 하면 VDF 평면이 Android 합성과 충돌 → 커널/엔진 다운. 레퍼런스 앱이 되는 건 순정 네이티브 렌더링 컨텍스트라서.
- **현재**: `Gallery.MOVIE_PLAYBACK_ENABLED=false` 가드 — OK 누르면 "동영상 재생은 준비 중입니다" 메시지만(리셋 방지). 썸네일·삭제·네비게이션 정상. scalar 재생 코드는 트랙 B 참고용 보존.
- **트랙 B 숙제**: 네이티브 `MprIf::PlayMovie`(libmpr.so)로 VDF 평면에 직접 재생. "프리뷰 프레임 취득(VDF 평면)"과 같은 평면 소유권 문제와 묶임.

## 4. 라이브뷰 복구 (갤러리 닫은 뒤)

- 갤러리 진입 시 `liveView.stop()`(카메라 release) → 닫을 때 재취득. 그런데 **hwView(OptimizedImageView) 오버레이가 반환 안 돼 라이브뷰가 blank**였음.
- 수정: 닫을 때 `hwView`를 **뷰 트리에서 removeView**(surface 강제 파괴→오버레이 반환), 열 때 재추가. + `LiveView.openCameraOnly()`(카메라만 재취득) 후 **라이브뷰 SurfaceView GONE→VISIBLE 재생성** → 새 surface의 `surfaceCreated`가 `startPreview`. 옛 surface 오버레이 경합 해소.

## 5. 기타

- **노출바 dim** (`ShootingOsd.java`): 현재 모드에서 조정 불가한 노출 항목을 회색+반투명(auto 느낌). M: 셔터·조리개·ISO 조정O, EV는 ISO AUTO일 때만 / A: 셔터 dim / S: 조리개 dim / P: 셔터·조리개 dim / AUTO·장면: 전부 dim.
- **디버그 마커 제거**: 단일뷰 infoBar의 `[HW]/[SW]` 표시 삭제.
- **힌트 수정**: "OK 확대" → "OK 정보   T 확대"(OK가 EXIF 토글로 바뀜).

---

## 6. 설정 — a6400(트랙 C) 대비 EXP 코드 보존

- a5000에 없는 EXP 설정(**APS-C/ND필터/시네마톤/무음촬영/RAW압축/모션샷**)은 a5000 메뉴 Item만 미등록(안 보임), **`applyNative`/`EXP_VALUES`/라벨 코드는 트랙 C(a6400 앱)용으로 유지**. 상위 기종 공유 SDK라 코드 보존 가치. (삭제했다가 복원함.)
- a5000 실측 메뉴 기준 = `re/static/stock_menu_inventory.md`(디컴파일 settings_tree는 상위기종 공용 카탈로그라 부정확).

## 7. 로드맵 (사용자 확정)

- **트랙 A** — a5000 Android 앱(현재 주력, 거의 완성)
- **트랙 B** — a5000 네이티브 Rigel+SDCom standalone UI (동영상 재생 포함)
- **트랙 C** — a6400 앱 지원
- **트랙 D** — a6400 CFW

### 트랙 A 남은 것
1. **실기 동작 검증** (대부분 빌드만 됨): EXP 게이트 설정(SteadyShot·ND·Lock-on AF·AEL·절전 등) 토글, 게이트 ACTION(DB복원·비행기·장치이름 — 안전한 것부터), 타일메뉴·데모모드 UI.
2. **캡처 서브시스템 4개**(파노라마 스윕×2·자동프레이밍·셀프카메라) — 캡처 파이프라인 신규 개발.
3. 자잘한 폴리시(동영상 썸네일 ARGB 색 등).
- 동영상 재생은 트랙 B로 이관.

---

## 8. 안전/운영 메모

- **기기 리셋 = 물리 재부팅 필요**(원격 복구 불가). 이 세션 동영상 재생 실험으로 수 회 리셋. HW 평면/비디오 디코더 접근이 주 위험.
- 설치: 백그라운드 앱 띄우기 → `sync; echo 3 > /proc/sys/vm/drop_caches` → `adb install -r` → 재실행. 설치 중 앱 foreground면 hang.
- 매 빌드 `tools/check-api-level.py` "0 reference(s) above API 10" 통과 필수.
