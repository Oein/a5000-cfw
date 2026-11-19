# imaging — 라이브뷰/영상 파이프라인 RE

라이브뷰(모니터링) 한 프레임이 **센서 → 캡처 프론트엔드(AIF) → ISP/RC 리사이즈 → VDF YC pin → VPH ch51 → 패널**로 흐르는 전체 경로를 센서에서 패널까지 추적한 카테고리. 장기 "lean stack"(Sony 군더더기 최소화) 방향에 입력으로, "이미징을 얼마나 우리가 직접 구동할 수 있는가"를 판정한다.

**핵심 판정**: 영상 생성(센서 P2S 시퀀스·SA DSP 펌웨어·ISP·RC 리사이즈)은 Sony 독점·공장 캘리브에 묶여 **재구현 비현실적 → 보존**. 표시 평면 합성(VPH)·OSD·입력·제어면(SDCom/Backup)은 레지스터/인터페이스가 전수 드러나 **우리가 지배 가능**. 따라서 lean stack 상한선 = "Sony 이미징 코어 유지 + Sony UI/Android 제거"이며, 영상 평면(ch51)은 읽기전용 관전자로 두고 그 위에 OSD만 올리는 route-b 모델이 유일하게 안전.

조사는 대부분 정적 RE(av-cam.bin, libObj, vdf_vph_driver.c/vdf_aif.c) + 에뮬 동적 실행으로 경로를 확정했고, **실기 검증은 아직 없다.**

| 번호 | 제목 | 한 줄 판정 |
|---|---|---|
| 0001 | [liveview-pipeline-overview](<0001. liveview-pipeline-overview.md>) | 센서→패널 A-to-Z. 구동주체=av-cam, 센서 ID read-back 없음, 두 타이밍 클럭(엔진 33367µs/패널 60Hz) |
| 0002 | [aif-isp-vdf-path](<0002. aif-isp-vdf-path.md>) | P2S(센서)·CamCore Pig·RC(ch23→ch26)·AIF 채널·VDF→VPH 레지스터 상세. ch51=Y8 1024×516 pitch1152 |
| 0003 | [sdcom-imaging-control](<0003. sdcom-imaging-control.md>) | 제어면: 라이브뷰/노출/AF/캡처 SDCom 명령. YC출력은 명령이 아니라 모드 상태 시퀀스 |
| 0004 | [lean-stack-feasibility](<0004. lean-stack-feasibility.md>) | 영상생성=Sony 마법(보존), 표시/OSD/입력/제어=우리 지배 가능. av-cam 유지+OSD만이 권장 |

## 교차 참조 (중복 금지)
- 패널 ch51⊕ch53 합성 자체: [../display/0003](<../display/0003. panel-routing.md>), present: [../display/0002](<../display/0002. present-pipeline.md>)
- SDCom 연결/카탈로그/Get·Shift/안전규칙: [../sdcom/](../sdcom/README.md)
- 카메라 읽기 게이트/뷰모드 enum: [../camera/](../camera/README.md)
- 제브라/피킹 영속(Backup prop): [../backup/](../backup/README.md)
- 에뮬 HW 모델·RE 원장(출처): `packages/emulator/docs/`의 `RE_SENSOR_ISP.md`, `RE_LIVEVIEW_PIPELINE.md`, `RE_LIVEVIEW_IMAGE.md`, `RE_LIVEVIEW_REQUEST.md`, `RE_DISPLAY_SCANOUT.md`, `RE_SA_DSP.md`, `RE_STILL_CAPTURE.md`, `HARDWARE.md`

## 신뢰도 표기
`observed`(실기 관찰) · `derived`(바이너리 정적 도출) · `inferred`(추정/에뮬 실행으로 경로 확정). 이 카테고리는 derived+inferred가 주이며 실기 검증은 미완.
