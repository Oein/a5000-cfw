# Track A 골든 레퍼런스 스크린샷 (a5000 UI 앱)

네이티브 포팅(`app/native/uicore/`)의 픽셀 비교 기준이 되는, 실행 중인 Track A 앱(`app/camera-ui`,
패키지 `dev.oein.a5kui`, v`a5k-2.2-complete`)의 화면 캡처 모음.

- **`<screen>__<state>.png`** = 앱 자체 디버그 API(`/api/debug/screenshot`)로 받은 **853×480 실제 앱 픽셀**.
  네이티브 UI 비교의 1차 기준.
- **`hmp/<screen>__<state>.png`** = QEMU HMP `screendump`으로 받은 **640×480 실제 패널** 합성 결과
  (라이브뷰 비디오 평면 = 컬러바가 OSD 뒤에 깔림). 레이아웃/합성 확인용 2차 기준.

캡처 환경: `emulator/`의 `sony-a5000` QEMU 머신(`qemu-system-arm-sgc1m`), fw 1.10, pristine NAND 복제본,
SD 카드(아래 "SD 카드" 참고), `-global cxd90014-aif.lv-source=bars`(라이브뷰 소스 = 컬러바).

---

## ⚠️ 중요: 캡처에 쓴 앱 빌드 변형 (골든 변형)

실기용 Track A APK를 **그대로** 쓰면 에뮬레이터에서 메뉴/모드 전환 시 앱이 멈춘다. 원인:
`LiveView.pausePreview()` → `CameraSequence.stopPreviewSequence()`가 에뮬의 카메라 엔진(av-cam)에서
완료 통지를 못 받아 UI 스레드가 네이티브에서 블록됨(엔진 측 미구현, `emulator/docs/RE_PLAYBACK_SWITCH.md`
계열). 그래서 캡처용으로 **`LiveView.startSequence()`를 no-op 처리한 변형**을 빌드해 사용했다
(`<scratchpad>/golden/variant/`). 결과:

- 메뉴/모드픽커/퀵바 등 `pausePreview`를 부르는 전환이 더 이상 멈추지 않음.
- 앱 **853×480 스크린샷은 배경이 검정**(프리뷰 프레임 시퀀스를 안 돌려 라이브뷰 JPEG 배경이 없음).
  → **UI 오버레이 픽셀만 깔끔하게** 담겨 네이티브 비교에 오히려 이상적.
- OSD/메뉴/위젯 **레이아웃·색·폰트·배치는 실기 앱과 동일**(UI 코드는 손대지 않음, 시퀀스만 비활성).
- HMP 640 스크린덤프에는 여전히 컬러바(라이브뷰 평면) + OSD가 합성되어 보인다.

즉, 색/레이아웃/타이포그래피 비교는 그대로 유효하고, "라이브뷰 위 OSD" 합성만 HMP 쪽을 참고하면 된다.

---

## 입력(키) 주입 방법 — 리플레이 스크립트용

두 경로가 **다르게** 동작하므로 구분이 중요하다. 캡처는 주로 **(B) 앱 디버그 키 API**로 했다.

- **(A) HMP `sendkey <k> 300`** (`emulator/tools/mon.py <monport> "sendkey down 300"`):
  가상 evdev(event0)로 들어가고, 앱의 `DialReader`(event0 직접 읽기)가 방향 입력을 **휠 회전(dial)**으로
  해석한다 → `onDial` 경로. 촬영화면에서 `down`을 보내면 퀵바가 **안 열리고** 값만 바뀐다.
  단 픽커/그리드/메뉴 안에서는 `onDial`이 그 화면 네비게이션으로 연결돼 이동에는 쓸 수 있다.
  셔터 완전누름(S2)·줌 등 GPIO/ADC 키는 이 경로로만 주입 가능(예: `sendkey spc 300` = S2).
- **(B) 앱 디버그 키 API** `GET /api/debug/key?code=<ISV_KEY>` (디버그 빌드, 포트 8080):
  곧바로 `onCameraKeyUp(code)`로 전달 → **D-pad 누름(press)** 의미. DialReader를 우회하므로
  퀵바 열기/메뉴 네비 등 결정적 동작에 사용. 아래 시퀀스의 키는 모두 이 **ISV 코드**다.

ISV 키 코드: **UP=103, DOWN=108, LEFT=105, RIGHT=106, ENTER(가운데)=232, MENU=514, PLAY=207, SK1=229**.
S2(셔터 완전누름)는 (A) HMP `sendkey spc 300`.

에뮬 게스트 안에서 API를 치려면 작은 정적 ARM HTTP GET 바이너리 `hget`를 tmpfs에 올려 썼다
(`<scratchpad>/golden/hget/hget`, `/android/a5k/hget "/api/debug/key?code=108" <out>`). 호스트에서
Wi-Fi 포워딩이 되면 그냥 `wget`/`curl`로 `:8080`에 접근해도 된다.

시작 상태 전제: **앱을 막 실행한 촬영 화면**(기본 장면모드=풍경/landscape, 메뉴 스타일=기본(3단) 캐스케이드).
설치·실행 경로는 `emulator/docs/RE_USB_APP_INSTALL.md`(USB `install -f`) → MENU→애플리케이션→애플리케이션 목록→`a5000 UI`.

---

## 캡처 목록

표기: 키 시퀀스는 (B) ISV 코드. "그리드/리스트 이동"은 픽커 내부 이동(코드 또는 HMP 화살표 모두 가능).

### 촬영 OSD
| 파일 | 설명 | 촬영화면에서의 키 시퀀스 |
|---|---|---|
| `shooting__initial` | 기본 촬영 OSD(풍경 모드, MF, 배터리/남은장수, 30"·±0.0·ISO AUTO) | (없음, 실행 직후) |
| `shooting__program` | 프로그램 자동(P) 모드 OSD | `ENTER` → `UP UP LEFT` → `ENTER` |
| `shooting__manual` | 수동노출(M) OSD, 셔터값 1/640 노란 활성 하이라이트 | `ENTER` → `DOWN`(P→M) → `ENTER` |
| `shooting__manual_iso_target` | M 모드에서 휠 타깃=ISO로 토글(ISO AUTO 노란 하이라이트) | (manual 상태에서) `RIGHT` |

### 모드 픽커
| 파일 | 설명 | 키 시퀀스 |
|---|---|---|
| `modepicker__grid` | 촬영 모드 선택(14개 타일 그리드, 현재=풍경 하이라이트) | `ENTER` |
| `modepicker__program_sel` | 그리드에서 "프로그램 자동" 하이라이트(확정 전) | `ENTER` → `UP UP LEFT` |

### 퀵바 / 값 모달 (DOWN 키)
| 파일 | 설명 | 키 시퀀스 |
|---|---|---|
| `quickbar__open` | 하단 퀵바(WB/DISP/초점모드/초점영역 타일, 푸터 안내) | `DOWN` |
| `quickbar__value_modal` | 퀵바 CENTER→전체 값 모달(화이트 밸런스 옵션 라디오 리스트) | `DOWN` → `ENTER` |

### 드라이브 / 셀프타이머 픽커 (LEFT 키)
| 파일 | 설명 | 키 시퀀스 |
|---|---|---|
| `drivepicker__open` | 드라이브 모드(좌) + 셀프타이머(우) 2열 픽커 | `LEFT` |

### 메인 메뉴 — 캐스케이드(밀러) 변형 ★네이티브 설계 타깃★
좌측 카테고리 레일(focus 0) ↔ 그룹 layer2(focus 1) ↔ 항목 layer3(focus 2). `RIGHT`=더 깊게, `LEFT`=이전.
| 파일 | 설명 | 키 시퀀스 |
|---|---|---|
| `menu__cascade__root` | 메뉴 루트 = 카테고리 포커스(카메라 설정, 1/7, 레일+그룹미리보기) | `MENU` |
| `menu__cascade__group_focus` | 그룹(layer2) 포커스: 화질/이미지 오렌지, layer3 dim, breadcrumb | `MENU` → `RIGHT` |
| `menu__cascade__item_focus` | 항목(layer3) 포커스: 이미지 크기(L) 오렌지, layer2 dim(번호배지 회색) | `MENU` → `RIGHT` → `RIGHT` |
| `menu__cascade__expo_group` | 노출/측광 그룹 포커스(6/8), 항목값 미리보기 | `MENU` → `RIGHT` → `DOWN×5` |
| `menu__cascade__cat_camera` | 카테고리 레일=카메라 설정(1/7) | `MENU`(레일에서 `LEFT`로 돌아온 상태) |
| `menu__cascade__cat_custom` | 카테고리=사용자 설정(2/7). INFO 항목(제브라/피킹 "—") 노출 | 레일에서 `DOWN` |
| `menu__cascade__cat_wireless` | 카테고리=무선(3/7), 비행기 모드 액션 | 레일에서 `DOWN×2` |
| `menu__cascade__cat_application` | 카테고리=애플리케이션(4/7), 소개=버전 표시 | 레일에서 `DOWN×3` |
| `menu__cascade__cat_playback` | 카테고리=재생(5/7), 삭제/보기/슬라이드쇼 액션(▶) | 레일에서 `DOWN×4` |
| `menu__cascade__cat_setup` | 카테고리=설정(6/7), 타일 메뉴=기본(3단) 보임 | 레일에서 `DOWN×5` |
| `menu__cascade__cat_mymenu` | 카테고리=마이 메뉴(7/7), 퀵바 슬롯 1~6 | 레일에서 `DOWN×6` |

레일 포커스로 가려면 항목/그룹 포커스에서 `LEFT`를 반복(캐스케이드는 항목 네비가 그룹·카테고리 경계를
넘어 하나의 스크롤로 흐른다 — 예: 무선 마지막 항목에서 계속 `DOWN` 하면 애플리케이션·재생 항목으로 넘어감).

### 메뉴 — 설정 유형별 (항목 포커스 & 값 픽커)
| 파일 | 설명 | 키 시퀀스 |
|---|---|---|
| `menu__picker_enum` | ENUM 값 픽커 모달(이미지 크기: S/Midium/L) | `MENU`→`RIGHT`→`RIGHT`→`ENTER` |
| `menu__picker_range` | RANGE 값 픽커(노출 보정, 스크롤 숫자 리스트, 0 선택) | `MENU`→`RIGHT`→`DOWN×5`→`RIGHT`→`ENTER` |
| `menu__item_exp_bool` | EXP 항목 "자동 HDR 레벨(EV) = 실험(비활성)" 포커스(게이트 off 표시) | `MENU`→`RIGHT`→`DOWN×5`→`RIGHT`→`DOWN×6` |
| `menu__item_bool` | BOOL 항목 "장시간 노출 NR = 켬" 가 보이는 상태(고감도 ISO NR 포커스) | `MENU`→`RIGHT`→`DOWN×5`→`RIGHT`→`DOWN×9` |
| `menu__picker_bool` | BOOL 값 픽커(장시간 노출 NR: 꺼짐/켬) | 위 상태에서 `DOWN`→`ENTER` |
| `picker__option_list` | OptionPicker 그리드(장치 이름 선택, 취소+프리셋). 라이브뷰 위 표시 | `MENU`→`DOWN×2`→`RIGHT`→`RIGHT`→(장치 이름 편집까지 `DOWN`)→`ENTER` |

INFO 유형: 전용 픽커 없음. `—` 회색 비선택 행으로 표시되며 네비 시 건너뜀.
다수의 예가 `menu__cascade__cat_custom`(제브라/피킹), `cat_wireless`(전송/NFC 등), `menu__cascade__root`(파노라마)에 보임.

### 메뉴 — 타일 변형 (참고 전용, 네이티브 설계 타깃 아님)
> 코디네이터 결정(2026-11-15): **네이티브 메뉴는 캐스케이드/밀러만 사용**. 아래 타일 캡처는
> 인벤토리/참고용일 뿐 네이티브 구현 목표가 아니다.

| 파일 | 설명 | 키 시퀀스 |
|---|---|---|
| `menu__tilepref_item` | 설정>소리/표시>"타일 메뉴=기본(3단)" 항목 포커스(스타일 토글 설정) | `MENU`→`DOWN×5`→`RIGHT`→`RIGHT`→`DOWN×3` |
| `menu__tilepref_picker` | 타일 메뉴 값 픽커(기본(3단)/타일) | 위에서 `ENTER` |
| `menu__tiles__root` | 타일 메뉴 루트(7색 카테고리 타일) | 타일 메뉴=타일 선택 후 메뉴 닫았다 `MENU` 재오픈 |
| `menu__tiles__drilldown` | 타일에서 카테고리 진입 → 그룹 리스트(캐스케이드로 전환) | 타일 루트에서 `ENTER` |

### 갤러리 (PLAY 키)
| 파일 | 설명 | 키 시퀀스 |
|---|---|---|
| `gallery__empty` | 갤러리 빈 상태("사진이 없습니다") | `PLAY`(207) |

### 셀프타이머 카운트다운
| 파일 | 설명 | 키 시퀀스 |
|---|---|---|
| `hmp/shooting__countdown` | 셀프타이머 카운트다운 오버레이(라이브뷰 위 큰 오렌지 숫자 "9") | 드라이브 픽커(`LEFT`)→셀프타이머열(`RIGHT`)→10초 선택(`DOWN×5`)→`ENTER`, 그 뒤 **HMP `sendkey spc 300`**(S2) → 카운트다운 중 캡처 |

> ⚠️ 카운트다운은 **HMP(640) 버전만** 숫자가 또렷하다. 853 앱-API 스크린샷은 캡처 지연(awaitFrame ~3s)
> 때문에 특정 숫자 프레임을 못 잡아 제외했다. 오버레이 레이아웃/색은 `hmp/shooting__countdown.png` 기준.

---

## 에뮬레이터에서 도달 불가(미캡처) — 사유

네이티브 포팅 시 이 화면들은 별도(실기/목업) 레퍼런스가 필요하다.

- **갤러리 이미지 상태(그리드-사진有 / 단일 / EXIF / 확대/줌)** — **도달 불가.**
  앱 갤러리는 소니 미디어 인덱스(`AvindexStore`/`com.sony.scalar.providers.avindex`)에만 의존하는데,
  에뮬은 **SA DSP(JPEG 디코드·썸네일·얼굴 코어)를 모델링하지 않아**(`emulator/docs/RE_SD_MEDIA_SERVICE.md`,
  `RE_PLAYBACK_SWITCH.md`) 카드에 DCF JPEG를 넣고 "이미지 DB 생성"을 해도 인덱스가 **0건**으로 남는다.
  DCF 경로/파일명/EXIF(Make=SONY, Model, DateTimeOriginal 등)를 맞춰 넣어도 동일(`total:0`). → 앱은 "사진이
  없습니다"만 표시. 썸네일/디코드 자체는 앱이 BitmapFactory로 하지만, 리스트에 올릴 인덱스를 못 받는다.
- **삭제 확인 모달(ConfirmModal)** — 갤러리 삭제(DELETE 키, 사진 있을 때)에서만 열림 → 위 사유로 불가.
  (시스템 액션 포맷/리셋/비행기모드는 기본 `EXPERIMENTAL_ENABLED`에 없어 모달 자체가 안 열림.)
- **진행 오버레이(BusyOverlay)** — 코드상 `doRecovery()`(이미지 DB 복원)에서만 `busyOverlay.show("이미지 DB 복원",
  "복원 중… 잠시 기다려 주세요")` 호출. 그런데 이 a5000 런타임엔 `didep.Media`가 없어 **"이미지 DB 복원" 항목이
  비활성(회색) → 선택 불가**(설정>저장/미디어에서 확인). 따라서 BusyOverlay는 일반 UI 경로로 도달 불가.
- **자동 리뷰(촬영 직후 미리보기)** — **도달 불가.** 실제 스틸 캡처가 에뮬에서 완료되지 않음
  (`emulator/docs/RE_STILL_CAPTURE.md`: RAW 버퍼 DMM 게이트/SA DSP). S2를 눌러도 앱 로그에
  `Remote shutter: {"status":"timeout","ms":30334}` 후 촬영화면으로 복귀(앱은 정상, 캡처만 타임아웃) → 리뷰 없음.

## SD 카드

`emulator/tools/mksd.py` 기반으로 FAT32 1GiB 이미지를 만들고 `DCIM/100MSDCF/DSC0000{1..6}.JPG`
(합성 JPEG 6장, 그라데이션/체커/링/컬러바/풍경, sips 인코딩 + exiftool로 SONY/ILCE-5000 EXIF 부여)를 넣었다.
생성 스크립트: `<scratchpad>/golden/mksdjpg.py`(mksd.py를 import). 위 "도달 불가"대로 갤러리엔 반영되지 않지만,
DB 생성 프롬프트→생성("완료")까지는 정상 동작한다.
