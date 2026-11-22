# mobile-remote 설계

CFW 단계에서 카메라(α5000)와 짝을 이루는 스마트폰 앱. **iOS 우선, Android 동시**(Expo/React Native, 하나의 코드). 카메라 통신 규격은 아직 미정 → 지금은 placeholder 프로토콜(v0/v0+, [PROTOCOL.md](PROTOCOL.md))과 **mock 카메라**(`mock/`)로 앱 전체를 실기 없이 구현·검증한다.

UI 베이스: [sony-remote-reboot/viewer](https://github.com/Oein/sony-remote-reboot) (같은 저자의 Expo 앱)를 가져와 확장.

---

## 기능 4종

| # | 기능 | 위치 | 상태 |
|---|---|---|---|
| 1 | 백그라운드 GPS 응답 (카메라가 EXIF에 기록) | `src/features/geotag/`, `src/features/session/` | 구현, mock E2E 검증 |
| 2 | 백그라운드 사진/영상 동기화 + 오프라인 보관함 | `src/features/sync/`, `src/app/(tabs)/library.tsx` | 구현 |
| 3 | 사진 레시피 (Recipe Lab 호환) | `src/features/recipes/`, `src/app/(tabs)/recipes.tsx` | 구현, 공유코드 벡터 검증 |
| 4 | 원격 조작 (모드/셔터/조리개/ISO/WB/초점/줌/녹화) | `src/app/(tabs)/index.tsx` (viewer 그대로) | viewer 기능 유지 |

### 1. GPS — 폰은 "답변만" 한다
- **EXIF는 카메라가 쓴다.** 폰은 사진 파일을 건드리지 않는다.
- 흐름: 카메라가 촬영 → `gps.request {reqId, shotId, capturedAt}` → 폰이 `gps.response {reqId, source, fix}` → 카메라가 그 사진 EXIF에 기록.
- 응답 선택(`geotag/answer.ts`, 순수 함수·테스트됨):
  1. **위치 기록(track)에서 촬영 시각 위치**(±5분 보간, 10분 넘게 떨어지면 없음) — 최신 fix보다 우선. 촬영 직후 폰이 이동했으면 최신 fix는 이미 다른 곳이기 때문. 방금 찍은 사진이면 `live`, 재연결 후 묻는 예전 사진이면 `track`으로 표시.
  2. 기록이 없으면(포그라운드 전용 모드 등) **최신 fix**가 촬영 ±60초 이내 & 정확도 ≤200m일 때.
  3. **none**. 방금 찍은 사진(2분 이내)만 OS에 새 fix를 요청(최대 8초) 후 재시도. `reason`에 이유(트랙 읽기 실패 포함).
- **연결이 끊긴 동안 찍은 사진**: 카메라가 요청을 큐에 쌓아뒀다가 재연결 시 원래 `capturedAt`으로 다시 묻는다 → 폰은 track으로 답한다. 그래서 카메라가 범위 밖이어도 폰은 저전력으로 위치 기록을 계속한다.
- **시계**: 카메라는 타임존이 없다(벽시계를 UTC처럼 읽은 epoch ms). `hello.clock`으로 `offset = cameraClock − phoneUtc`를 측정해 타임존+드리프트를 한 번에 보정(`trackMath.ts`).
- 위치 기록은 폰에만(`Documents/track/YYYY-MM-DD.jsonl`, 보관 기간 설정, 기본 14일). 카메라에는 사진 한 장의 위치만 간다.

### 2. 동기화 — 이벤트 구동, 폴링 없음
- 카메라가 `photo.added`를 보낼 때만 그 사진을 받는다(라이브뷰 중이어도 카메라 부하 = 썸네일 몇 KB GET 1회).
- 수준: 끔 / 썸네일(기본) / 미리보기 / 원본(+사진 앱 자동 저장).
- 예산: 포그라운드 40장·60초, 백그라운드 8장·20초 per pass. 남으면 이어서 조금씩.
- 이벤트 채널이 없는 카메라 앱(구 ILCE Remote)에는 포그라운드 15초 폴링 fallback. OS 주기 작업(`expo-background-task`, ≥15분, best-effort)은 따라잡기용.
- 보관함 탭: 폰에 받은 사본 — 카메라 꺼져 있어도 열람.
- **라이브뷰(MJPEG)는 촬영 탭이 화면에 있고 앱이 포그라운드일 때만** 받는다(카메라에서 가장 무거운 작업). 탭 이동·백그라운드 즉시 끊김.
- ⚠️ 실기 사실: 순정 펌웨어는 라이브뷰 진입 시 Wi-Fi가 끊기는 현상이 있다(루트 메모리 `device-liveview-wifi-drop`). CFW 쪽에서 해결해야 할 전제 조건 — PROTOCOL.md "CFW 요구사항" 참고.

### 3. 레시피
- 모델/범위는 [Recipe Lab](https://github.com/voxivoid/recipe-lab-sony-pmca)의 공개 파일/링크 스펙(format 1·2, family `sony-creative-style`)을 따라 **상호 호환**: `.YML` 가져오기/내보내기, `recipelab.io/r#<code>` 공유 링크 encode/decode (그 레포의 공개 테스트 벡터로 검증).
- **독립 구현**: Recipe Lab은 GPL-3.0 → 코드·76개 레시피 표는 포함하지 않는다. 기본 레시피는 우리 것 10개(순정 스타일 + 몇 가지 룩). 큰 라이브러리는 "가져오기"로.
- **필름 룩 내장(개인 사용 빌드)**: Recipe Lab의 77개 레시피를 앱에 내장. 공개 레포라 표 자체는 커밋하지 않고 `npm install`(postinstall)/`npm run recipes:bundle`이 GitHub 원본을 받아 `recipeLab.ts`로 파싱해 `src/features/recipes/bundled/recipelab.json`(gitignore)을 만든다. 오프라인이면 기존 파일 유지, 없으면 빈 목록. 읽기 전용(편집=사본), id는 제목 기반(표 갱신 후에도 즐겨찾기 유지), 24자 카메라 이름 규칙에 맞게 이름 정리, 목록엔 원제목.
- **많은 레시피 관리**: 그룹(최근 사용·즐겨찾기·내 레시피·기본·브랜드별) 접기/펼치기(기억, 첫 실행 시 브랜드는 접힘, 접힌 그룹은 색 미리보기), 그룹 칩으로 한 그룹만 보기, 어디서나 다단어 검색(이름·브랜드·스타일·이펙트, 한글 라벨 포함), 그룹 ⋯ 메뉴(이 그룹만 보기·빠른 전환에서 숨기기·모두 즐겨찾기), 선택 모드(일괄 즐겨찾기/링크 공유/삭제). 촬영 화면 빠른 전환 시트: 검색 + 최근·즐겨찾기 우선, 그룹은 접힌 채 시작, 숨긴 그룹 제외, 길게 눌러 즐겨찾기.
- **편집 중 제스처 보호**: WB 패드를 드래그하는 동안 스크롤·뒤로 스와이프 차단(응답자 유지), 저장 안 한 변경이 있으면 뒤로 스와이프 비활성(뒤로 버튼은 저장/버리기 확인).
- 적용: 레시피 → `CameraChanges`(v0+: creativeStyle/saturation/contrast/sharpness/pictureEffect/option/dro + WB/색온도/AB/GM + EV). 부호 변환 주의: 파일은 앰버·그린이 +, 카메라 링크는 AB=앰버(−)…블루(+), GM=그린(−)…마젠타(+) → 둘 다 뒤집음. B&W 포스터화는 파일 `posterization-bw` ↔ 런타임 `posterization-mono`.
- 값 검증은 "고치지 않고 거부 + 이유"(Recipe Lab 규칙과 동일). 픽처 이펙트는 JPEG에서만 동작 경고.
- 나중에 CFW 자체에도 넣을 기능 — 같은 YAML/공유코드 포맷을 쓰면 폰↔카메라 레시피 이동이 그대로 된다.

### 레시피 UX
- **보면서 만든다**: 편집기 상단에 라이브뷰, 값을 바꾸면 350 ms 디바운스로 카메라에 바로 반영(실시간 미리보기, 토글 가능). 편집 진입 시 카메라의 원래 룩을 스냅샷 → "카메라에 적용" 없이 나가면 원복(`useLivePreview`).
- 탭 한 번 조작: 채도/대비/선명도 = 7칸 값 스트립(길게 누르면 0), 스타일·이펙트 = 스와치 타일, WB 미세조정 = 카메라와 같은 2D 패드(B↔A, M↔G), 색온도 = 스테퍼 + 프리셋(백열등/형광등/주광/흐림/그늘), EV = 스테퍼 + ±1 빠른 칩, DRO = 세그먼트.
- 하단 고정 바: [저장] [카메라에 적용]. 기본 레시피도 편집 가능 — 저장하면 사본("내 레시피로 저장"). 저장 안 한 변경이 있으면 나갈 때 저장/버리기/계속 확인(`usePreventRemove`).
- **카메라 지금 설정으로 만들기**: 카메라 메뉴에서 맞춘 룩을 그대로 레시피로(`recipeFromCamera`, 적용→역변환 왕복 테스트). 이름 붙일 수 없는 WB 프리셋(그늘 등)은 `keep`, 못 읽은 값은 알려줌.
- 목록: 룩 스와치(색온도·틴트·채도·대비·흑백 근사), 태그, 필터(전체/즐겨찾기/내 레시피/기본), 상단 "카메라에 적용된 레시피" 카드, 적용 버튼/적용됨 표시, 길게 눌러 메뉴(적용·편집·복제·공유·즐겨찾기·삭제). 즐겨찾기는 기본 레시피에도.
- 공유: QR(Recipe Lab 카메라 앱의 Read Recipe QR Code로 읽힘) + 링크 + YAML. 가져오기: 클립보드/파일(여러 개)/붙여넣기(텍스트 속 링크 여러 개), 항목별 제외, 이름 충돌 시 새 이름 미리 표시, 딥링크 `a5kremote://recipe/import?code=<코드>`.
- 촬영 화면: 라이브뷰 위 **레시피 pill** → 즐겨찾기 우선 시트에서 바로 전환. **위치 pill**(±m, 보낸 장수, 남은 동기화) → 설정.

### 공통 UX
- 토스트 + 햅틱으로 결과 피드백(적용/저장/실패). 인라인 메시지 제거.
- 설정: 연결 카드(상태·주소·AP/mock 빠른 선택) → 연동·위치 → 동기화 → 위치 기록 → 저장 공간. 긴 설명은 "자세히"로 접고, 파괴적 동작은 카드 안 행 + 확인.
- 보관함: 날짜별 섹션, 당겨서 지금 동기화, 빈 상태 안내.

### 4. 원격 조작
viewer의 촬영 화면 그대로: 라이브뷰, 셔터/반셔터/AF, 줌, MF·확대, 동영상, 설정 칩(드래그로 바로 변경/탭하면 휠 피커). 프로토콜만 교체되면 된다.

---

## 백그라운드 아키텍처 (핵심)

```
 camera ──ws events──▶ EventLink ──▶ session ──▶ gps.response (카메라가 EXIF 기록)
                                        │  ├──▶ photo sync (photo.added → 썸네일)
 CoreLocation ──task──▶ track + latest ─┘  └──▶ Live Activity (잠금화면 / Dynamic Island)
```

### 왜 이렇게 하나 (iOS 제약에서 출발)
- iOS는 백그라운드 앱을 수 초 내 정지시킨다. **계속 실행이 허용되는 정당한 방법 = 위치 백그라운드 모드**(`UIBackgroundModes: location` + "항상" 권한 + 표준 위치 업데이트). 이 앱의 1번 기능이 정확히 위치 서비스이므로 정책상 정당하다.
- 위치 업데이트가 살아있는 동안 JS 런타임도 살아있다 → 같은 시간에 **WebSocket·GPS 응답·동기화·Live Activity 업데이트**가 돈다. 다른 백그라운드 API(BGTask 등)는 언제 실행될지 보장 없음 → 따라잡기용으로만.
- **폰이 카메라에 접속**(WebSocket 클라이언트)한다. 폰이 서버가 되는 구조는 iOS 백그라운드에서 불가능.
- Android: expo-location 포그라운드 서비스(알림 표시)가 같은 역할.

### 세션 상태 기계 (`src/features/session/session.ts`)
| 상태 | 조건 | 위치 모드 | Live Activity |
|---|---|---|---|
| off | 앱 백그라운드 & 백그라운드 연동 꺼짐 | 꺼짐 | 종료 |
| connecting | 소켓 연결 중 / hello 대기 | 이전 모드 유지 | – |
| linked | hello 수신 | **linked**: High, 5 m, 5 s | 시작(포그라운드일 때) / 업데이트 |
| lost | 링크 끊김 | 3분간 linked 유지 → **standby**: Balanced, 50 m, 60 s, deferred 5분 | "재연결 중" |
| (standby 4h) | 계속 못 찾음 | standby | 종료(잔류 표시) |

- 재연결: 1·2·5·10·20·30초 백오프(최대 30초). 45초 동안 아무 프레임(ping 포함)이 없으면 소켓이 열려 보여도 죽은 것으로 간주(Wi-Fi 끊김 감지). 포그라운드 복귀 시 즉시 재시도.
- 폰은 `phone.state {background}`를 보내 카메라가 백그라운드 폰에 무거운 것(라이브뷰 등)을 안 보내게 한다.
- 번들 로드 시 `initSession()`(root layout import) → iOS가 백그라운드에서 앱을 재시작해도 화면 마운트 없이 같은 동작.
- 포그라운드 전용 모드(백그라운드 연동 꺼짐): 연속 위치 업데이트 없이, 요청이 오면 그때 fix 1회.

### Live Activity (ActivityKit)
- 잠금화면/배너 + Dynamic Island(compact: 카메라 아이콘 + 위치 기록 장수, expanded: 연결 상태·배터리·GPS 정확도·남은 동기화·마지막 촬영 경과).
- 구성: `modules/a5k-live-activity/`(로컬 Expo 모듈, Swift: start/update/end) + `targets/live-activity/`(위젯 확장, `@bacons/apple-targets`가 prebuild 시 Xcode 타깃 생성). `A5kSessionAttributes`는 두 곳에 같은 정의(ActivityKit은 타입 이름+Codable로 매칭) — **동기화 유지 필수**.
- 제약 반영: ① 시작은 **포그라운드에서만** 가능 → 백그라운드에서 연결되면 `needs-foreground`로 표시하고 다음 앱 진입 때 시작. ② 업데이트는 로컬(푸시 없음), 3초로 합치고 중요한 변화(촬영·연결 변화)는 즉시. ③ staleDate(연결 3분/대기 15분)로 앱이 죽었을 때 시스템이 흐리게 표시. ④ 8시간 시스템 한계로 종료되면 감지해 다음 포그라운드에서 재시작.
- Android에는 해당 없음(포그라운드 서비스 알림이 대신).

### 배터리 / 자원 원칙
- 정밀 위치는 카메라 연결 중에만. 끊기면 3분 뒤 저전력.
- 소켓은 작은 JSON만. ping 15초.
- 동기화는 이벤트 구동 + 예산. 백그라운드에서 원본 대량 다운로드 안 함(원본 레벨이라도 pass당 8장).
- 라이브뷰는 보이는 동안에만.

---

## 테스트 / 검증

### 시뮬레이터 실측 (iPhone 18 Pro, 2026-11-21)
| 시나리오 | 결과 |
|---|---|
| 포그라운드 촬영 | `EXIF ← shot 13: 37.566500, 126.978000 ±5m (live)` |
| 앱 백그라운드(홈) 후 촬영 | `shot 14: 37.570000, 126.982000 (live)` — 백그라운드에서 이벤트 수신·응답 |
| 카메라 재시작 → 백그라운드 앱 자동 재연결 | `phone hello background=true` |
| 백그라운드 + 범위 이탈(`/mock/offline`) 중 위치 A에서 촬영 → B로 이동 → 재연결 | `shot 13: 37.591867, 127.001867 (track)` — 큐 재요청에 **촬영 시점** 위치(A→B 보간)로 답함, 이동 후 위치 아님 |
| Live Activity | 잠금화면에 카메라·연결·배터리·위치 장수·정확도·마지막 사진 표시 |

- `npm test`: 순수 로직 33개 — GPS 응답 선택, 트랙 보간, 시계 오프셋, 이벤트 파싱, 백오프, 레시피 YAML·공유코드(Recipe Lab 공개 벡터)·카메라 매핑.
- `npm run mock`: mock 카메라. HTTP v0 + v0+ 이벤트(촬영→`photo.added`+`gps.request`, 폰 없을 때 큐→재연결 시 재요청, `EXIF ←` 로그). `GET /api/photos/<id>`의 `gps`로 "카메라가 기록한 위치" 확인.
- iOS 시뮬레이터: `xcrun simctl location booted set <lat>,<lon>`으로 위치, 앱 백그라운드 후 `curl -XPOST localhost:8080/api/camera/shutter`로 촬영 → mock 로그에 `EXIF ←`가 찍히면 백그라운드 GPS 응답 동작. 범위 이탈은 `curl -XPOST 'localhost:8080/mock/offline?seconds=40'`.

## 남은 것 / 열린 질문
- 실제 CFW 프로토콜(PROTOCOL.md "CFW 요구사항").
- 카메라 Wi-Fi 연결 방식(AP vs Station)과 iOS 자동 재접속(NEHotspotConfiguration) — 지금은 사용자가 Wi-Fi 연결.
- Live Activity 푸시 업데이트(앱이 완전히 죽은 경우) — 서버가 필요해 보류.
- 영상(MP4) 동기화: 현재 사진만. 영상은 크기 때문에 썸네일만 받는 방향 검토.
