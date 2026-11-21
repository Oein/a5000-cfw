# mobile-remote

α5000 CFW와 짝을 이루는 스마트폰 앱 (iOS 우선 + Android, Expo/React Native).
카메라 통신 규격은 아직 미정 → placeholder 프로토콜 + **mock 카메라**로 실기 없이 동작.

- 설계: [docs/DESIGN.md](docs/DESIGN.md) — 기능 4종, 백그라운드 아키텍처, Live Activity
- 프로토콜: [docs/PROTOCOL.md](docs/PROTOCOL.md) — v0 HTTP + v0+ 이벤트, CFW 요구사항

## 기능
1. **백그라운드 GPS 응답** — 카메라가 촬영 후 위치를 물으면 폰이 답하고, 카메라가 EXIF에 쓴다. 화면 꺼져도 동작.
2. **백그라운드 동기화** — 카메라의 `photo.added`에만 반응해 썸네일/미리보기/원본을 조금씩. 보관함 탭에서 오프라인 열람.
3. **사진 레시피** — Recipe Lab 호환 YAML·공유 링크 가져오기/내보내기, 편집, 카메라 적용.
4. **원격 조작** — 라이브뷰, 셔터/AF/줌/MF, 모드·셔터속도·조리개·ISO·WB 등 (sony-remote-reboot UI).
- iOS **Live Activity**: 잠금화면/Dynamic Island에 연결 상태·위치 기록 장수·배터리·GPS 정확도.

## 실행
```sh
npm install
npm run samples          # mock 샘플 이미지 생성 (macOS, swift)
npm run mock             # mock 카메라 :8080 (HTTP + ws 이벤트)
EXPO_PUBLIC_CAMERA_HOST=127.0.0.1 npx expo run:ios   # dev build (Expo Go 불가: 네이티브 모듈)
```
mock에서 촬영 시뮬레이션: `curl -XPOST localhost:8080/api/camera/shutter` → 로그에 `EXIF ← shot N: lat, lon` 이 찍히면 GPS 응답 동작.

검사: `npm test` (순수 로직), `npm run typecheck`, `npx expo lint`.

## 구조
```
src/app/                 화면 (expo-router): 촬영 · 갤러리 · 레시피 · 보관함 · 설정
src/lib/camera.ts        카메라 HTTP 클라이언트 (프로토콜 v0 경계)
src/features/link/       이벤트 채널 (WebSocket, 재연결/워치독) + 프로토콜 타입
src/features/session/    백그라운드 세션 오케스트레이터 (위치 모드·GPS 응답·동기화·Live Activity)
src/features/geotag/     위치 태스크, 트랙 저장, 응답 선택
src/features/sync/       사진 동기화 엔진, OS 주기 작업
src/features/recipes/    레시피 모델·YAML·공유코드·카메라 매핑·저장소
modules/a5k-live-activity/  로컬 Expo 모듈 (Swift ActivityKit)
targets/live-activity/      위젯 확장 (SwiftUI Live Activity UI, @bacons/apple-targets)
mock/                    mock 카메라 (server.mjs + events.mjs)
tests/                   node --test
```

## 출처 / 라이선스
- UI·카메라 클라이언트는 [Oein/sony-remote-reboot](https://github.com/Oein/sony-remote-reboot) `viewer/`에서 가져옴(같은 저자).
- 레시피 포맷은 [Recipe Lab](https://github.com/voxivoid/recipe-lab-sony-pmca)(GPL-3.0)의 공개 스펙을 따른 독립 구현 — 그 코드·레시피 표는 포함하지 않음.
