# 카메라 ⇄ 폰 프로토콜

> **상태: placeholder.** 실제 CFW 프로토콜은 아직 설계하지 않았다(나중에). 지금은 앱을 실기 없이 완성하기 위한 최소 규격이고, 앱은 `CameraClient`(HTTP)와 `EventLink`(이벤트) 두 경계만 통해 카메라와 말하므로 교체 범위가 작다.

## v0 — HTTP (요청/응답)
[sony-remote-reboot](https://github.com/Oein/sony-remote-reboot)의 ILCE Remote API 그대로, 포트 8080. 정의: `src/lib/camera.ts`.

| 경로 | 용도 |
|---|---|
| `GET /api/info` | 모델·펌웨어 (+v0+ `clock`) |
| `GET/POST /api/camera` | 상태 조회 / 설정 변경(`CameraChanges`) |
| `POST /api/camera/{shutter,focus,focusdrive,zoom,magnify,movie}` | 촬영 조작 |
| `GET /api/liveview` | MJPEG |
| `GET /api/photos`, `/api/photos/<id>[/small\|preview\|full\|raw]`, `POST /api/photos/delete` | 사진 |
| `GET /api/movies`, `/api/movies/<id>/{thumb,file}` | 동영상(Range 지원) |

**v0+ 추가 (mock만 구현)**
- `GET /api/info` → `clock`: 카메라 벽시계를 UTC처럼 읽은 epoch ms (사진 `date`와 같은 규칙).
- `POST /api/camera` 필드: `creativeStyle, saturation, contrast, sharpness, pictureEffect, pictureEffectOption, dro` (레시피). 상태에 `look`, `creativeStyleValues`, `pictureEffectValues`.
- `GET /api/photos/<id>` → `gps`: 카메라가 EXIF에 기록한 위치(검증용).

## v0+ — 이벤트 채널 (WebSocket)
`ws://<camera>:8080/api/events`, JSON 텍스트 프레임. **폰이 접속**한다(iOS 백그라운드에서 폰이 서버가 될 수 없음). 정의: `src/features/link/protocol.ts`.

### 카메라 → 폰
| type | 필드 | 의미 |
|---|---|---|
| `hello` | `v, model, clock, battery?` | 접속 직후 1회. `clock`으로 시계 오프셋 측정 |
| `gps.request` | `reqId, shotId?, capturedAt` | 촬영 후 "그 시각 위치?". 응답 없으면(끊김) 큐에 두고 재접속 시 같은 `capturedAt`으로 재요청 |
| `photo.added` | `item: PhotoItem` | 새 사진 (동기화 트리거) |
| `status` | `battery?, shotsLeft?, mode?` | 상태 변화 |
| `ping` | `t` | 15초마다. 폰은 45초 무응답이면 링크 사망 판정 |

### 폰 → 카메라
| type | 필드 | 의미 |
|---|---|---|
| `hello` | `v, app, background` | 접속 직후 |
| `gps.response` | `reqId, source: live\|track\|none, fix: {lat,lon,alt,acc,t}\|null, reason?` | 위치 답변. 카메라가 EXIF에 기록(`none`이면 위치 없이 둠) |
| `phone.state` | `background` | 폰이 백그라운드면 카메라는 무거운 푸시를 자제 |
| `pong` | `t` | ping 응답 |

## CFW 요구사항 (실제 프로토콜 설계 시 반드시 만족)
1. **폰이 클라이언트**, 카메라가 서버(또는 카메라가 AP). 폰 백그라운드 제약 때문.
2. 라이브뷰 중에도 이벤트 채널 유지. 순정은 라이브뷰 진입 시 Wi-Fi가 끊기는 현상이 실측됨 → CFW에서 해결 또는 우회 필요.
3. `gps.request` 큐는 카메라 쪽에 영속(전원 껐다 켜도 남게) — 폰이 나중에 답할 수 있도록. EXIF 사후 기록은 카메라 파일 쓰기이므로 §1 벽돌 위험과 무관(SD 카드 파일)하지만 쓰기 실패·카드 교체 처리 필요.
4. 이벤트 메시지는 작게(<1 KB), 폰이 백그라운드면 라이브뷰/대용량 푸시 금지.
5. 시계: 카메라 벽시계 그대로 보내고 폰이 오프셋 보정(카메라에 타임존 개념 없음).
6. 인증/페어링: 미정(현재 없음). 최소한 첫 페어링 시 토큰 교환 필요.
7. 레시피 포맷은 Recipe Lab YAML/공유코드와 호환 유지(폰·카메라·Recipe Lab 간 이동).
