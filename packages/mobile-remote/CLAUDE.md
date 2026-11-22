@AGENTS.md

# mobile-remote 규칙 (a5000-cfw 패키지)
- 응답·문서는 한국어. 루트 `CLAUDE.md` 전역 규칙(커밋 단위, 공개 가능한 것만) 적용.
- 설계/프로토콜 변경 시 `docs/DESIGN.md`·`docs/PROTOCOL.md` 갱신.
- **EXIF는 카메라가 쓴다** — 폰은 위치를 답할 뿐 사진 파일을 수정하지 않는다.
- 카메라 경계는 `src/lib/camera.ts`(HTTP)와 `src/features/link/`(이벤트) 둘뿐. 화면/기능은 이 경계만 쓴다.
- 백그라운드에서 도는 코드(session/geotag/sync/link)는 화면 마운트에 의존하지 않는다(번들 로드 시 `initSession()`; React hook은 UI용 구독만).
- `A5kSessionAttributes.swift`는 `modules/a5k-live-activity/ios/`와 `targets/live-activity/` 두 벌 — 항상 같이 수정.
- 순수 로직(`trackMath`, `answer`, `link/protocol`, `recipes/{model,yaml,shareCode,apply}`)은 RN import 금지, 상대 import는 `.ts` 확장자 포함(`node --test`로 실행).
- Recipe Lab(GPL-3.0) 코드·레시피 표 복사 금지. 공개 스펙/테스트 벡터 기반 호환만. 레시피 표는 개인 사용 빌드에 내장하되 커밋 금지: `npm run recipes:bundle`(postinstall)이 생성하는 `src/features/recipes/bundled/`(gitignore). 테스트 fixture는 직접 만든 가짜 값.
- 의존성 설치: `npx expo install <pkg>` (`.npmrc` legacy-peer-deps).
