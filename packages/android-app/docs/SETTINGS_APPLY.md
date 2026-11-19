# 설정 APPLY 경로 — 변환표(R1) + `CameraControl::applySetting` (2026-11-16)

피커 CONFIRM → `App.model().applySetting(item, value)` → `CameraControl::applySetting` → 플랫폼 실행기.
`item` = `kMenuItems` 인덱스(0..139, 140개 전부 도달 가능), `value` = 선택한 옵션 인덱스(MT_RANGE는 숫자 그대로).
**RAM/런타임 전용. Backup/flash 쓰기 없음.**

## 변환표
- 생성: `app/native/uicore/tools/gen_apply.py` (menu_gen.cpp + `settings_sdcom_map.tsv` + 문서화된 RE 맵) →
  `uicore/src/model/apply_gen.cpp`(`kApply[140]`) + 사람용 `app/docs/settings_apply_table.tsv`(140행: 방법·경로·SDCom id·Backup id·커버리지·근거).
- 데이터만 사용(신규 RE 없음). id/값을 **추정하지 않는다**: TSV가 `[? id]`/virtual key로 표시했거나 옵션이 TSV 값 행과 안 맞으면 `unmapped`.
- 방법: `inproc`(엔진 인프로세스 경로 실증) / `sdcom`(0x1003, movie는 0x300a 파라미터 + A쪽 값) / `backup`(propId 기록만, **미기록**) / `local`(앱 선호·읽기전용 정보) / `action`(게이트·재생·퀵슬롯) / `unmapped`.

| 방법 | 개수 | 비고 |
|---|---|---|
| inproc | 4 | **CmnViewProcessDataMgr per-GROUP 경로**(TRACKB §23): image-aspect(g1), storage-fmt(g2), metering-mode(g22), face-detection-mode(g31) — 전부 에뮬 read-back 검증 [V]. SDCom 경로도 병기 |
| sdcom | 45 | 옵션 전부 매핑(위 4개는 inproc로 승격, SDCom 경로는 유지). movie-format 2/3 등 부분 매핑 포함 |
| backup | 5 | beep 0x01070026, AF보조광 0x0107005c, 피킹 레벨 0x01070352/색 0x01070360, 제브라 0x010701b8(+b9) — 기록만 |
| local | 32 | 앱 선호 15 + 읽기전용 정보 17 |
| action | 16 | sys-*/pb-*/quick-slot |
| unmapped | 38 | native-only `info-na-*` 25(HDMI/USB/지역/REC폴더/전송/Wi-Fi설정…, 매핑 데이터 없음) + 값 선택지 없는 가상키 8(quick-af, mulit-shoot-nr-iso, color-depth-type, prv-mag, digital-zoom-type, lens-correction, remocon, ovf_preview) + [?] 5(movie-quality, movie-audio, x-wind-noise, x-tracking-focus, x-sys-poweroff) |

## 실행기 현황
| 플랫폼 | 실행 가능한 경로 |
|---|---|
| host-sim (`platform/common/fake_camera.cpp`) | catalog 브리지 항목은 `setSetting`(모드 게이팅 그대로), 그 외 매핑 항목은 가짜 엔진이 수락해 param-id 저장소에 기록. local/action/backup/unmapped는 적용 안 함 |
| a5000-inproc (`camera_inproc.cpp`) | **IK_GROUP** 4종(image-aspect/storage-fmt/metering/face) = `cvpdmGroupWrite(group,internal)` + `cvpdmGroupRead` read-back → `APPLY_OK`/`APPLY_REJECTED`(엔진 inhibit 시, 순정 parity). SDCom-only 매핑 항목은 인-게스트 SDCom 차단(ret -5)이라 `APPLY_NO_ROUTE`(가짜 성공 없음). Backup은 `APPLY_RECORDED`. 카탈로그 `SET_EXPOSURE_MODE` = `requestModelExecute("model/CAMERA",0x10,{0x12A})` + 0x12C read-back |
| a5000 (SDCom 앱모드) | 아직 없음 — 표의 `sdcomMsg/sdcomParam/sdcomValue`로 0x1003/0x300a 송신기를 붙이면 됨(첫 파라미터 id 52 규칙, SETTINGS_SDCOM_MAP.md 4.1) |

## 에뮬레이터 검증 (A5K_UI_APPLYTEST=1, 고유 NAND/포트, 콜드부트 자동주입)
- **IK_GROUP 실적용 [V]** (ui.log, read-back 일치):
  - `storage-fmt` RAW/RAW+JPEG/JPEG → group 2 internal 0/1/2 각 `APPLY_OK` + read-back 0/1/2 (3/3).
  - `metering-mode` → group 22 internal(multi=2/center=1/spot=4) `APPLY_OK` + read-back 일치 (P 모드서).
  - `face-detection-mode` → group 31 internal 0/1 `APPLY_OK` + read-back 일치 (P 모드서).
  - `image-aspect` 16:9 → group 1 `APPLY_OK`(read-back 1); 3:2는 internal 미관측이라 `NO_ROUTE`(정직).
- `setSetting(EXPOSURE_MODE=P)` → `model/CAMERA` cmd 0x10 `{0x12A=1}` → `getModelInfo 0x12C` D 11 → 1 **read-back 변화 확인**. SCN(6)으로 복원.
- **SCN inhibit = 순정 parity**: drive/focus/dro/metering/picture-effect/steadyshot은 부팅 SCN 모드서 엔진이 write 거부(ret=0, 순정 메뉴 회색처리와 동일). P 전환 후 drive/metering/dro/smile 등 해제(grpscan2 검증).
- EV/ISO/flex-spot 등 SDCom-only → `NO_ROUTE`(인프로세스 실행기 없음), self-timer → `LOCAL`, beep → `RECORDED`, format → `LOCAL`(액션).
- 못 하는 것: SS/F/EV/ISO·focus/steadyshot/picture-effect 등은 **AE-active/렌즈 엔진** 필요(에뮬 미구동) → group 매핑은 확정이나 에뮬서 write inhibit. 나머지 SDCom 매핑 설정의 실제 엔진 효과는 SDCom 실행기(앱모드/실기)에서만 검증 가능.
- RE 상세·GROUP 전체 표·라운드트립 근거: TRACKB_INPROCESS.md §23 (도구 `emulator/tools/grpscan_so.c`·`grpscan2_so.c`).
