# docs/RE/playback

재생(동영상 플레이백) · 갤러리/리뷰(저장 이미지, 특히 RAW .ARW)의 순정 처리 경로 역설계. route-b 커스텀 UI가 재생/갤러리를 직접 구동·대체하기 위한 근거. 상태: 2건 작성, 정적 분석(로컬 Hex-Rays 캐시 + 에뮬 RE 문서 교차) 중심. 디바이스 확인 필요 항목은 각 문서에 명시.

| 번호 | 제목 | 한 줄 요약 |
|---|---|---|
| 0001 | [video-playback](<0001. video-playback.md>) | 동영상 재생 파이프라인: 컨테이너(AVCHD/MP4) → InfraMovieDecoder → SA ZIMA(AVC) → VDF YUV 핀 → VPH ch51, 오디오 SA 코덱, 트랜스포트/컨트롤패널, route-b 통합 모델 |
| 0002 | [raw-gallery-thumbnail](<0002. raw-gallery-thumbnail.md>) | 갤러리 데이터 모델(MprIf/ObjMedia/AV 인덱스 DB), funcMode-21 뷰(ViewIndexPb/ViewSinglePb), RAW 썸네일/프리뷰 = 매립 ScreenNail(재현상 아님), InfraCacheMgr 썸네일 캐시, route-b 통합 모델 |

## 핵심 결론 (요약)
- **재생 영상 평면 = VPH ch51(panel YUV)** — 라이브뷰와 동일 평면. 우리 OSD(ch53 panel OSD)는 그대로 분리 유지 → route-b 촬영 패스스루와 동일 구조로 재생에도 오버레이 가능.
- **순정 재생/갤러리는 별도 미디어 서브시스템**(엔진 ModelManager → ObjMedia/ObjCntMgr/MprIf → libmpr)이다. 에뮬에서 이 PLAY 경로는 EE_STOP 핸드셰이크 의존 때문에 역사적으로 불안정했고(현재 null-SA로 완료됨), 실기 구동은 벽돌 민감.
- **RAW 썸네일/프리뷰는 재현상(re-develop)이 아니라 ARW에 캡처 시 매립된 ScreenNail/썸네일 JPEG을 디코드**한다. 엔진 RAW 재현상(InfDecoder ExecCmd_IMG_RawDevelop)은 별도 기능(RAW→JPEG/확대)일 뿐 기본 갤러리 표시 경로가 아니다.
- **route-b 권장 모델**: 정지화상 갤러리 = 우리가 DCIM을 직접 열어 매립 JPEG 디코드(이미 `playback_inproc.cpp`로 구현, 벽돌-안전). 동영상 = 순정 디코더 구동은 비현실적 → 순정 재생을 트리거하고 우리 OSD만 오버레이(촬영 패스스루와 동일).
