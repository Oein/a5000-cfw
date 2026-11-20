# shooting — 촬영 제어 (드라이브/릴리즈 · 화질/룩/WB)

> a5000의 **촬영 기능 end-to-end 매핑**: 어떻게 동작하고, route-b 주입 UI가 무엇을 set할 수 있는가.
> 캡처(파일 쓰기)까지가 이 카테고리의 범위. 재생/갤러리/RAW 썸네일 디코드는 **playback 카테고리**(다른 담당).

## 보고서

| # | 제목 | 상태 | 요지 |
|---|---|---|---|
| 0001 | [drive-and-release](<0001. drive-and-release.md>) | derived+observed | 싱글/연속(버스트 레이트·버퍼)/셀프타이머/브래킷(연속·WB·DRO)의 제어 id와 릴리즈 시퀀스 |
| 0002 | [looks-quality-wb](<0002. looks-quality-wb.md>) | derived+observed | RAW/JPEG/크기/비율, 크리에이티브 스타일, 픽처 이펙트, DRO/오토HDR, 컬러스페이스, 화이트밸런스(커스텀WB·색온도/필터) |

## 핵심 결론 (route-b 관점)

- 촬영 제어의 **대부분은 SDCom `CameraSetParameters`(0x1003) 파라미터**(id 256~388)로 set 가능 — 휘발 RAM 경로라 벽돌 안전.
- **값표(enum)가 없는 소수**(조리개·셔터)는 Shift 명령으로만. 커스텀 WB·셀프타이머·브래킷 릴리즈는 전용 CameraEx 메시지.
- **엔진 메커니즘은 전부 Sony 독점**: av-cam이 RAW 현상/디모자이크/JPEG 인코딩을, MPR 서브시스템이 DCF 파일 오브젝트/버스트 그룹을, OBJEFFECT가 픽처이펙트("Artfilter") 파이프라인을 소유. 우리는 **enum/파라미터를 선택할 뿐 룩 자체를 재현하지 못한다**.
- **Picture Effect / Creative Look 룩테이블은 재현 불가**(ZV-E10 Astra 한계와 동일) — 단, a5000의 Creative **Style**(Vivid/Portrait 등)과 DRO/HDR/컬러스페이스는 **순정 엔진 param 선택**이므로 "순정에 지시"는 100% 된다. 우리가 자체 룩을 만들 수 없다는 뜻이지, 순정 룩을 못 켠다는 뜻이 아니다.

관련: [../sdcom/](../sdcom/README.md), [../imaging/](../imaging/README.md), [../backup/](../backup/README.md)
