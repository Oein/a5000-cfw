# a5000-cfw

Sony **α5000 (ILCE-5000)** 의 기본 카메라 UI를 더 단순한 커스텀 UI로 바꾸는 프로젝트. 사이드 앱이 아니라 **기본 카메라 경험 자체**를 교체하는 것을 목표로 하며, 순정의 **모든 기능·옵션은 100% 보존**하고 Sony의 영상/엔진 처리는 그대로 둡니다. 모든 실험은 **벽돌 위험 0**(RAM-only·가역) 원칙 아래 진행합니다.

> 프로젝트 전반·규칙은 [`CLAUDE.md`](CLAUDE.md), 방향/다음은 [`docs/AI_DOCS/0002.TODO.md`](docs/AI_DOCS/0002.TODO.md).

## 구조 (monorepo)
```
packages/
  emulator/            QEMU sony-a5000 머신 — 안전한 실험장
  android-app/         트랙 A: ILCE Remote 포크 (UI 설계 검증용 Android 앱)
  custom-binary-app/   트랙 B(주력): im.elf에 주입하는 네이티브 커스텀 UI (route-b)
  custom-firmware/     교체 전환 (미래 계획)
  tools/               서드파티 체크아웃 + 운영 스크립트
  data/                ⚠️ Sony 독점 자료·기기 백업·디컴파일 — 비공개(README만 추적)
docs/
  AI_DOCS/             개발 가이드·방향/TODO
  RE/                  역설계 보고서 (카테고리별 + 전역 README)
```

## 상태
- **트랙 B route-b**: 실기에서 렌더·메뉴·입력·카메라읽기(촬영 중) 동작. 현재 과제 = present 반응성(캐시드 매핑), 뷰모드별 오버레이 가시성. → [`packages/custom-binary-app/STATUS.md`](packages/custom-binary-app/STATUS.md)
- **역설계**: 디스플레이/카메라/설정/입력/메뉴/SDCom/부팅/이미징 등 카테고리별 정리. → [`docs/RE/README.md`](docs/RE/README.md)

## 빌드하려면
이 레포는 **코드와 공개 안전 문서만** 포함합니다. 펌웨어 원본·디컴파일·NAND 백업·크로스 sysroot 등 **Sony 파생/기기 비공개 데이터는 `packages/data/`에 로컬로 직접 생성**해야 합니다(절차: `docs/AI_DOCS/0001.LOCAL_DEV_GUIDE.md`, `packages/data/README.md`). 트랙 B 빌드: `cd packages/custom-binary-app && ./build.sh a5000-inproc`.

## 라이선스 / 주의
Sony 펌웨어 바이너리·디컴파일 산출물은 이 레포에 포함되지 않습니다(저작권). 역설계 문서는 동작 설명과 오프셋 인용만 담습니다. 본인 소유 기기에서, 본인 책임 하에만 사용하세요.
