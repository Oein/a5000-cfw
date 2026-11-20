# RE 문서 인덱스 (Sony a5000 / ILCE-5000 route-b)

## 프로젝트 RE 현황
a5000의 순정 UI를 더 나은 커스텀 UI로 교체하되 모든 기능을 보존하는 것이 목표이며, 이를 위한 역설계는 **정적 분석으로 인터페이스 식별을 사실상 끝낸** 상태다. 화면은 im.elf의 Rigel Window Server(합성) / VDF(영상 평면), 카메라 제어는 SDCom(큐 0x1540432), 입력은 evdev + ABT, 영속 설정은 `/setting/Backup.bin`(서브시스템 0x107의 메뉴-쓰기 173개가 안전 집합)으로 정리되었다. SDCom Get/Shift, 키맵, 다이얼 방향, 네이티브 설정 propId 6개, route-b 주입 메뉴와 present 해결은 실기/에뮬로 검증됐다. 미확정은 standalone 프로세스의 D2A 획득, 일부 키(휴지통/Fn/DISP) 코드, ABT 전키 네이티브 송신, 이벤트 id 왕복 등이다. 모든 보고서는 공개 가능하도록 동작과 오프셋만 기술하며, 원시 덤프·캡처·Sony 파생 표는 `packages/data/re/`(gitignored)에 둔다. 1순위 제약은 **벽돌 위험 0**(플래시 미기록, RAM tmpfs 실행, SDCom 휘발 경로, 복구 불변식 유지).

신뢰도 표기: `observed`(실기 관찰) · `derived`(바이너리에서 정적 도출) · `inferred`(추정).

## 카테고리

| 카테고리 | 한 줄 현황 | 비고 |
|---|---|---|
| [display](display/README.md) | GPU 합성, present 파이프라인, 패널 라우팅, Rigel 클라, OSD 제출경로, cached 매핑, **GPU 렌더매니저/핸드오프(0007)** — 보고서 있음 | 순정 OSD는 GPU(GLES2) 렌더 / 핸드오프보다 Recipe A 우선 |
| [imaging](imaging/README.md) | 라이브뷰 센서→패널 파이프라인(AIF/ISP/RC/VDF/VPH), 이미징 제어면, lean stack 타당성 — 보고서 있음 | 영상생성=Sony 보존, 표시/OSD=지배 가능 |
| [camera](camera/README.md) | 카메라 읽기 게이트, 모드 enum 등 — 보고서 있음 | 다른 에이전트 소관 |
| [settings](settings/README.md) | 설정 적용 경로, CVPDM 그룹 테이블 등 — 보고서 있음 | 다른 에이전트 소관 |
| [input](input/README.md) | evdev 키맵 observed, ABT 436B 메시지 derived, 다이얼 방향=degree 부호(실기 정정) | 휴지통/Fn/DISP 코드 미확정 |
| [menu](menu/README.md) | 노드 트리(root 0x4822D4), 실측 106항목, 액션/앱 실행 경로, 구현 맵 | 7번째 탭/항목 주입은 RAM-only 불가 |
| [sdcom](sdcom/README.md) | 연결·메시지 146종·Get/Shift 실기 확인, D2A 상태머신, 안전 규칙 | standalone D2A 미확정 |
| [backup](backup/README.md) | Backup.bin Tier A/B/C, propId 6개 실측, 제브라 SDCom 불가 | CVPDM 그룹 쓰기는 settings |
| [boot](boot/README.md) | 파티션/부팅 체인, 공장 WBI 상시 재개, dmode+env.txt 자동 실행, 복구 불변식 | 교체 전환만 위험 |
| [persistence](persistence/README.md) | **cold-boot 생존 패치 타당성(0001)** — a5000 코드 서명 없음, LD_PRELOAD가 1순위 | 최고 벽돌위험 경로 조사(절차서 아님) |
| [memory](memory/README.md) | **RAM 풋프린트/트림(0001)** — Linux 175.8MB, 회수가능 ~48MB, 진짜 위험=OOM시 im.elf가 먼저 죽음(oom_adj=0) | 우리 UI +9.8MB → ~3MB 감량 가능 |
| [playback](playback/README.md) | **동영상 재생(0001)·RAW 갤러리 썸네일(0002)** — 재생영상=ch51(라이브뷰 평면), RAW는 매립 JPEG 디코드(재현상 아님) | 정지=우리 직접디코드(구현됨), 동영상=순정 트리거+ch53 오버레이 |
| [network](network/README.md) | **AF→WiFi 끊김(0001)** — 의도적 아님, PREEMPT_RT 스케줄링 기아(Wi-Fi가 우선순위 맨 아래) | RAM-only 수리 가능(IRQ affinity·ksdioirqd 승격), 실기 계측 1회 필요 |
| [shooting](shooting/README.md) | **드라이브/릴리즈(0001)·룩/화질/WB(0002)** — 제어면 param 256~388, set=`CameraSetParameters 0x1003` | 순정 전기능 켜기 OK, 새 룩/이펙트 주입 불가(Artfilter/ColorMode 독점) |
| [focus](focus/README.md) | **초점 시스템(0001)** — AF모드(CVPDM g13)·영역(g187)·스팟(g188)·트리거(S1/0x1006)·확대·MF어시스트·피킹 | 데이터면=엔진 소유, 모드/영역은 route-b 배선·검증됨 |
| [movie](movie/README.md) | **녹화(0001)·SteadyShot/줌(0002)** — Movie팩토리 0x3000/02/03, H.264=SA DSP ZIMA, 바디IS 없음, 줌 3종 ch51 | 전부 패스스루(SDCom 토글+ch53 오버레이) |
| [connectivity](connectivity/README.md) | **Wi-Fi·NFC·USB·앱(0001)** — 리모트=orb-server WebAPI :10000(Camera Remote API), DLNA/전송/MTP/NFC=im.elf 내부 | 리모트는 route-b 직접 구동 가능, 나머지 순정 트리거 |
| [scene](scene/README.md) | **장면·자동지능(0001)** — iAuto/SCN(param 263/select), 얼굴검출(303/304·0x1015), 스마일·소프트스킨(355)·오토프레이밍 | set 가능, 인식·검출·프레임=엔진 passthrough |
| [misc](misc/README.md) | RE 범위 원장, 트랙 B 임계 경로, 기기 워크플로, UI 리소스, 운영 제약 | |

## 읽는 순서 제안
1. [misc 0001](misc/0001.%20re-scope-surfaces-and-ledger.md) — 범위와 정정 이력
2. [misc 0002](misc/0002.%20trackb-critical-path.md) — 임계 경로
3. 필요한 영역: input / sdcom / menu / backup / boot

## 공통 규칙
- 보고서 이름: `NNNN. <kebab-name>.md`, 카테고리별 `README.md`에 상태 표.
- 원시 자료(IDA 덤프, 캡처 로그, Sony 파생 표, 개인 데이터)는 `packages/data/re/`(gitignore)에만 둔다. 이 디렉터리에는 동작 설명·오프셋 인용만 담은 공개 안전 보고서만 둔다.
