# flash — 내장 팝업 플래시 (스트로보) RE

> a5000(ILCE-5000)의 **내장 팝업 플래시**를 end-to-end로 매핑한다: 플래시 모드(끔/자동/강제/저속동기/후막동기), 플래시 보정(광량), 적목 감소, 그리고 팝업·충전·발광(fire) 기구와 OSD용 상태 이벤트.

공통 모델 = focus/shooting/scene와 동일한 **엔진-소유 데이터면**이다: 플래시의 충전·팝업·발광 타이밍은 **순정 엔진(libObj `ObjCamera`/`CamMgr` + 전원 서브보드)** 이 소유하고, 뷰(UI)는 **모드/보정/적목 set + 상태 read**만 한다. 발광은 촬영 시퀀스에서 엔진이 셔터와 동기해 수행 — route-b는 건드릴 필요가 없다(촬영 passthrough).

상태: **정적 분석 확정**(로컬 Hex-Rays 캐시 `packages/data/re/ida/libObj.so.c` + SDCom 카탈로그 `packages/data/re/scalar/`). 저수준 HV 충전/트리거(발광 소자 구동)는 디컴파일 집합(av-cam)에 **없음** → 전원 서브보드/서브MCU 소유로 device-only. enum 일부 raw 수치는 실기 라이브덤프 필요(문서에 명시).

## 문서
- [0001. flash-modes-and-fire.md](<0001. flash-modes-and-fire.md>) — 플래시 모드/보정/적목 control id + enum + 팝업/충전/발광 기구 + OSD 상태 경로 + route-b 판정.

## 한 줄 결론 (feature별 control id + 기구 + route-b)

| 기능 | control id (우리가 set) | 엔진 기구 | OSD/이벤트(read) | route-b 판정 |
|---|---|---|---|---|
| 플래시 모드 | **SDCom param 264 `strobeModeType`** (set=`CameraSetParameters 0x1003`), 반영본 `276 strobeModeType2` / 엔진 `PARAMID_STROBE_MODE` · `MSGID_SET_STROBE_MODE_CMD` | 엔진이 모드에 따라 발광/동기(앞막·뒤막) 타이밍 결정 | SDCom 이벤트 `0x1319`(+`0x1346 0x134b`), `CameraEventFlashModeParamList` | ✅ set(SDCom 직결), enum=inventory 확정 |
| 플래시 보정(광량) | **SDCom param 278 `strobeEvShift`** / 엔진 `PARAMID_STROBE_EV_SHIFT` · `MSGID_SET_STROBE_EV_SHIFT_CMD` (스텝=`PARAMID_STROBE_EV_STEP`) | 발광량 보정(±3.0EV) | — | ✅ set(SDCom 직결), enum 32값 확정 |
| 적목 감소 | **SDCom param 277 `redEyeReduction`** / 엔진 `PARAMID_RED_EYE_REDUCTION_MODE` · `MSGID_SET_RED_EYE_REDUCTION_MODE_CMD` | 본발광 전 예비발광 | 상태 `NID_AUTO_RED_EYE_REDUCTION_STATUS` | ✅ set(On/Off), raw값 주의(§아래) |
| 팝업 상태(올림/내림) | 엔진 `PARAMID_STROBE_POPUP` (set=`MSGID_SET_STROBE_POPUP_CMD`) | 팝업 모터(엔진/서브보드 소유) | **`NID_STROBE_POPUP`**(EID `0x201E`) = 0 내림/1 올림 → PARAMID 38396(bool) | 🔶 read로 OSD 표시, 올림은 엔진 위임 |
| 충전 상태(발광 준비) | — (read 전용) | 커패시터 충전(전원 서브보드), 과열 보호 `OFF_BY_CHARGE_MOTOR_THERMAL` | **`NID_STROBE_CHARGE`**(EID `0x201F`) = 0/1/2 (미충전/충전중/완료) → PARAMID `0x95FD`; SDCom 이벤트 `0x1352` `CameraEventFlashChargeEventList` | 🔶 read로 "발광준비" OSD 가능 |
| 자동발광 예측 | — (read 전용) | AE가 자동모드서 발광 여부 결정 | **`NID_AUTO_STROBE_STATUS`** | 🔶 read로 자동모드 발광 아이콘 |
| 발광(fire)/동기 | (제어 불필요) | 엔진 촬영 시퀀스가 셔터와 동기 발광; 앞막=일반, 뒤막=모드4/5, EXIF 플래그 `PARAMID_NOTIFY_EXIF_FLASH`(EXIF 0x980A) | 엔진 | ✅ 순정 유지(촬영 passthrough), 조치 불필요 |
| 테스트 발광 | 엔진 `MSGID_SYNC_STROBE_TEST_CMD`(cat `0x3100` msg `0x6512`) | 즉시 1회 발광 | — | ⚠️ 호출 가능하나 불필요 |

범례: ✅ 설정 경로 확정 · 🔶 엔진 위임/read만 · ⚠️ 가능하나 미사용.

## 핵심 결론
- **플래시 모드/보정/적목은 SDCom `0x1003` param(264/278/277)으로 직접 set** — shooting 파라미터와 완전히 같은 검증된 경로. route-b GREEN.
- **팝업·충전·발광은 전부 순정 엔진/전원 서브보드 소유**. 우리는 상태(`NID_STROBE_POPUP`/`NID_STROBE_CHARGE`/`NID_AUTO_STROBE_STATUS`)를 **read해서 OSD 아이콘**(팝업됨/충전완료/자동발광예정)에만 쓴다. 발광 타이밍(앞막/뒤막)은 모드가 결정하며 촬영은 순정 그대로.
- **a5000은 핫슈/외장 스트로보 하드웨어가 없다**. 펌웨어엔 `CA_EXT_STROBE`/`HOTSHOE_STROBE`/`WIRELESS`/`HSS`/`FEL`/manual-level(모드 7~21) 경로가 전부 있으나 **a5000 바디에선 inert**(공유 펌웨어 잔재). a5000 유효 모드 = 0 끔·1 자동·2 강제·3 저속동기·4/5 후막동기 + 22~24 적목 변형.

## device-only / 미확정
1. **저수준 HV 충전·트리거 구동**(발광 소자 GPIO/전원 레일): av-cam 디컴파일에 없음 → 전원 서브보드/서브MCU 소유. 정적 불가, 실기 계측 필요(촬영 passthrough면 불필요).
2. `strobeModeType`(264)의 **런타임 raw enum ↔ inventory 메뉴 enum** 대응 — 기준 바디덤프 `264=2`(강제)와 inventory `2=Fill-flash` 일치하나 전체 매핑은 라이브덤프로 확정.
3. `redEyeReduction`(277) 기준덤프 **raw=2** 관측 vs inventory `On=0/Off=1` — raw 2 = "N/A(팝업 내림)" 추정, 실기 토글 덤프로 확정.
4. `NID_STROBE_CHARGE` 3값(0/1/2)의 정확한 의미(미충전/충전중/완료 추정) — 실기 발광 후 이벤트 캡처로 확정.

관련: [shooting](../shooting/README.md), [sdcom](../sdcom/README.md)(이벤트 구독 0004), [camera](../camera/README.md), [scene](../scene/README.md), [settings](../settings/README.md), [menu](../menu/README.md).
