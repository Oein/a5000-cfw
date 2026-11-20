# scene — 신(SCN)·자동 지능·검출 RE

a5000(ILCE-5000)의 **장면 인식/선택 + 피사체 검출** 계열을 end-to-end로 매핑한다: iAuto/Superior Auto(장면 인식·다중프레임 합성), SCN 개별 신, 얼굴 검출(+등록/우선순위), 스마일 셔터(+감도), 소프트 스킨, 오토 오브젝트 프레이밍(a5000=PortraitIFraming).

공통 모델 = [focus](../focus/README.md)와 동일: **엔진(av-cam/MPR/DetectCh)이 인식·검출·합성을 소유**, **ObjRenderer가 프레임/아이콘을 표시 평면에 직접 렌더**, 뷰(UI)는 모드 set + 상태 read만. 노출/드라이브/룩 축은 [shooting](../shooting/README.md), 모드 enum은 [camera/0001](../camera/0001.%20funcevent-mode-enum.md)에 있고 여기선 중복하지 않는다.

## 문서
- [0001. scene-auto-and-detection.md](<0001. scene-auto-and-detection.md>) — 기능별 제어 id + 엔진 메커니즘 + OSD/검출-이벤트 경로 + route-b 판정.

## 상태 요약 (기능별)

| 기능 | 제어 id(우리가 set) | 엔진 메커니즘 | OSD/이벤트 | route-b 판정 |
|---|---|---|---|---|
| iAuto / Superior 진입 | param 263 exposureMode / `IAutoMode` 노드 | — | 엔진 | ✅ set(enum device-dump) |
| 장면 인식 아이콘 | 읽기: 이벤트 **0x2+0x12c3**, `PARAMID_SCENE_NUM`, `MprIf::GetHighlightSceneInfo` | **MPR 독점** | 엔진 아이콘 / 우리 read-overlay 가능 | 🔶 인식=엔진, 아이콘 read 가능 |
| Superior 다중프레임 합성 | `SuperiorAutoSettings`(ContShooting/ImageExtract), `PARAMID_AUTO_ADVANCE_IMAGE_EXTRACT` | **엔진 독점(HHT/HDR)** | 엔진 | 🔶 Settings만 선택 |
| SCN 개별 신 | 노드 `SceneSelection` / `PARAMID_SCENE_SELECT` / 이벤트 **0x1211** (CVPDM group=IDA-TODO) | 엔진 프리셋 강제 | 엔진 | ✅ set(신 enum device-dump) |
| 얼굴 검출 on/off·우선순위 | **SDCom param 304 faceScanMode + 303 displayFaceFrame**, msg **0x1015/0x1016**, `PARAMID_FACE_DETECTION_MODE`/`_PRIORITY_TRACKING_MODE` | **DetectCh(av-cam)** | **엔진 ObjRenderer**(PANEL/LINE/EVF), 이벤트 **0x1e01**/`MSGID_FACE_DETECT_EVT`, 모드 **0x12a6** | ✅ set(SDCom 직결), 프레임=순정 passthrough |
| 얼굴 등록/삭제/우선순위 | `ObjFaceRecorder` MSGID 군 | `modelIndivRecognition.so` | 엔진 | ⚠️ 호출 가능·미배선, 데이터 device-private |
| 스마일 셔터 + 감도 | 노드 SmileShutter(Slight/Normal/Big), `PARAMID_SMILE_SHUTTER_MODE`/`_DETECTION_SENSITIVITY` | DetectCh | 엔진 `BKID_DISPLAY_SMILE_*` | ✅ set, 릴리즈=엔진 |
| 소프트 스킨 | **SDCom param 355 softSkinEffect**, 이벤트 **0x135e** | `DetectCh::beautyEffectDetection` / OBJEFFECT Artfilter | 엔진(캡처 후처리) | ✅ 레벨 set, 룩 재현 불가 |
| 오토 오브젝트 프레이밍 | 노드 `PortraitIFraming`, `PARAMID_AUTO_FRAMING_MODE`, `MSGID_SET_AUTO_FRAMING_MODE_CMD` | `pvt_CalcAutoFramingArea`(엔진) | 엔진(영역 통지 `MSGID_NOTIFY_AUTO_FRAMING_AREA_EVT` + 2차 이미지 저장) | ✅ on/off set, 크롭=엔진 |

범례: ✅ 설정 경로 확정(정적) · 🔶 엔진 위임/read만 · ⚠️ 가능하나 미배선.

## 핵심 결론
- **검출·인식·합성·프레임 렌더는 전부 순정 엔진 소유**(얼굴=DetectCh, 장면=MPR, 합성=av-cam HHT/HDR, 프레임=ObjRenderer). route-b는 **모드만 set**하고 결과는 **순정 OSD passthrough**로 받는다 — focus/shooting과 동일 설계.
- 우리가 직접 쓰는 가장 깨끗한 제어면: **SDCom 0x1003 param 303/304/355** + msg **0x1015/0x1016**(얼굴). 나머지(SCN 신·스마일·오토프레이밍)는 엔진 PARAMID / 메뉴노드(CVPDM group) 경로.
- 우리 OSD가 장면 아이콘을 직접 그리고 싶을 때만 이벤트 0x2+0x12c3 read. 얼굴 프레임은 엔진이 이미 그리므로 재구현 불필요.

## device-only / IDA 미확정
1. enum 수치(exposureMode/faceScanMode/smile sens/softSkin/auto-framing/SCN 신) — device-dump.
2. SceneSelection·FaceDetect_SmileShutter·PortraitIFraming 노드의 **CVPDM group eventId** — IDA(Wrapper getter / settings_tree `13=`).
3. 얼굴 좌표 payload(0x1e01↔MSGID_FACE_DETECT_EVT) — 우리가 직접 그릴 때만, 실기 캡처.
4. SCN 각 신이 잠그는 프리셋, 장면 인식 클래스 전체 목록, 등록얼굴 상한·포맷(device-private).

관련: [camera](../camera/README.md), [focus](../focus/README.md), [shooting](../shooting/README.md), [sdcom](../sdcom/README.md), [imaging](../imaging/README.md), [settings](../settings/README.md), [menu](../menu/README.md).
