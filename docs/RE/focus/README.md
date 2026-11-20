# focus — 초점 시스템 RE

a5000(ILCE-5000)의 **초점 시스템 전체**(AF 모드 전환, AF 영역, AF 트리거/완료, 포커스 확대, MF 어시스트/거리, 피킹)를 센서-엔진-OSD-외부제어면까지 end-to-end로 매핑하고, route-b(im.elf에 주입한 `a5kinproc.so`)가 **무엇을 읽고/바꾸고/트리거할 수 있는지** 판정한다.

전제: 라이브뷰/노출/캡처 전반은 [sdcom](../sdcom/README.md)·[imaging](../imaging/README.md)에 있고, 모델 읽기 게이트는 [camera/0002](../camera/0002.%20model-camera-read-gate.md), MF 초점거리 값은 memory `mf-focus-distance`에 이미 있다. 여기서는 **초점 고유 메커니즘**만 다룬다(중복 금지). Wi-Fi 끊김 부작용은 초점 주제 밖 → [network/0001](../network/README.md).

## 문서
- [0001. focus-system.md](0001.%20focus-system.md) — 초점 시스템 전체 지도(기능별 제어 id + 엔진 메커니즘 + OSD 경로 + route-b 판정).

## 상태 요약 (기능별)

| 기능 | 제어 id | 엔진 메커니즘 | OSD 렌더 | route-b 판정 |
|---|---|---|---|---|
| AF 모드(AF-S/AF-C/DMF/MF) | 메뉴노드 FocusMode / CVPDM eventId **13** / SDCom param 265 / ChangeAfMf 0x1025 | 콘트라스트 AF 구동 vs MF 수동 | ACT_IconFocus(엔진 OSD) | ✅ 읽기+설정(W1 group13, 실기 PROVEN) |
| AF 영역(Wide/Zone/Center/FlexSpot) | 메뉴노드 FocusArea / CVPDM eventId **187** / SDCom param 271·370 | AF 창 선택 | PAS_FocusWindow + ACT_IconAfArea(엔진) | ✅ 타입 읽기+설정(group187) / ⚠️ 라벨 device-seal |
| 플렉시블 스팟 좌표 | eventId **188** / PARAMID_AF_AREA_POINT_CTRL / SDCom param 317 focusPoint | 좌표를 AF 창으로 | 엔진 OSD | ⚠️ 가능(group188), 미배선·미검증 |
| AF 트리거 + "합초" | 반셔터 S1(0x1F1) / SDCom AutoFocus 0x1006·Cancel 0x1007 / 완료 이벤트 0x200A·0x200C~F·0x2001 | 콘트라스트 AF→렌즈 모터 | 엔진 초점 프레임(합초=녹색) | 🔶 트리거=순정 통과(설계), 상태읽기=이벤트 구독 가능·미검증 |
| 포커스 확대(focus zoom) | SDCom 0x1032/0x1033/0x1036 / PARAMID_CAM_EXPAND_FOCUS_* | 라이브 YUV 디지털 확대 | **엔진 영상평면 + 엔진 OSD 박스** | ⚠️ SDCom으로 명령 가능, 영상·박스는 엔진 렌더(우리 ch53 아님) |
| MF 어시스트 | 메뉴노드 MFAssist / PARAMID_MF_ASSIST_MODE / MSGID_SET_MF_ASSIST_MODE_CMD(비-SDCom) | 링 회전 시 자동 포커스확대 | 엔진 | ⚠️ 거리읽기 PROVEN(focusmem), 토글 미배선 |
| MF 초점거리 값 | im.elf 메모리 +148 (memory `mf-focus-distance`) | 렌즈 communicator 보고 | ACT_BarFocus_MF / 우리 OSD 가능 | ✅ 읽기 실기 PROVEN |
| 피킹(peaking) | **비-SDCom**. PARAMID_PEAKING_* / Backup 레벨 0x01070352·색 0x01070360 / 메뉴노드 54·55 | ObjRenderer→OSAL VDF 서비스(제브라 동형) | **엔진 VDF가 영상평면에 에지 렌더** | ⚠️ SDCom 불가. Backup(영속) 또는 in-proc OSAL 주입(미검증) |
| AF 보조광 | **비-SDCom**. PARAMID_AF_LIGHT_AUTO / Backup 0x0107005C / 메뉴노드 16 | 물리 LED | — | ⚠️ Backup 또는 W1 group route |

범례: ✅ 실기/에뮬 확인·배선 · 🔶 설계상 순정에 위임 · ⚠️ 가능하나 미배선/미검증.

## device-only 미확정 (요지)
- group187 AF영역 raw↔라벨(Wide vs Multi 등)의 실기 확정.
- 플렉시블 스팟 좌표(group188) 왕복, SDCom param 317 focusPoint 좌표계/범위.
- FocusDone 내부번호↔수신 msgId 왕복(0x1301→0x200?) 실기 캡처.
- 포커스확대/MF어시스트/피킹의 enum·좌표 값, 피킹 색 매핑(노랑↔흰=2↔0 외) 실기 토글-덤프.
- E 35mm F1.8의 Pre-AF/AF-미세조정 지원 플래그(uc_PreAF_Support).

> 원본 TSV·근거(디컴파일 발췌)는 `packages/data/re/`(gitignore). 본 문서는 동작·오프셋·id만.
