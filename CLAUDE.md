# CLAUDE.md — a5000-cfw (Sony a5000 커스텀 UI/펌웨어 프로젝트)

> 이 파일 = **프로젝트가 뭔지 + 구조 + 전역 규칙**. 각 package에 자체 `CLAUDE.md`/`README.md`가 있고, RE 상세는 `docs/RE/`. 응답 언어: **한국어**.

---

## 0. 한 줄 정의
Sony a5000(ILCE-5000)의 **복잡한 순정 카메라 UI(6탭/20페이지/106항목)를 더 단순한 커스텀 UI로 교체**하는 프로젝트. 사이드 앱이 아니라 **기본 카메라 경험 자체**를 바꾼다. 순정의 **모든 기능·옵션은 100% 보존**(단순화는 "접근·조작 방식"이지 기능 제거가 아님). Sony의 영상/엔진 SW 처리는 그대로 유지. 실험은 에뮬레이터에서.

---

## 1. ⭐ 1순위 제약 — 벽돌 위험 0
무엇을 하든 **기기를 벽돌로 만들 가능성이 있으면 하지 않는다.** 최상위 기준.

| 경로 | 지속성 | 벽돌 위험 | 정책 |
|---|---|---|---|
| Android 앱(CameraEx/Params) · SDCom 런타임 메시지 · RAM tmpfs 실행 | RAM/`/setting` | 없음~낮음(재부팅 복구) | 자유롭게 / 정답 경로 |
| `/setting/Backup.bin` 메뉴-쓰기 propId(subsystem 0x107, 173개) | 영구 flash | 낮음(메뉴 조작 동급) | 되돌리기/현상유지 즉시 안전 |
| Backup raw 쓰기 / 비-메뉴 BKID · `/usr`·`/system`·NAND flash | 영구 flash | **최고(진짜 벽돌)** | 교체 전환 단계 전까지 **금지** |

**기기 실험 규칙:** 플래시 미기록(RAM tmpfs `/android/a5k/`에서 실행), 상태 변경 전 사용자 확인, 카메라 크래시=물리 전원 재부팅 필요(원격 복구 불가).

---

## 2. 프로젝트 구조
```
root
├── README.md / CLAUDE.md / .gitignore
├── packages/
│   ├── emulator/           QEMU sony-a5000 머신 (실험장). 자체 CLAUDE/README
│   ├── android-app/        트랙 A: ILCE Remote 포크 (UI 설계 검증용 Android 앱)
│   ├── custom-binary-app/  트랙 B(현재 주력): route-b a5kinproc.so (im.elf 주입 네이티브 UI). STATUS.md
│   ├── custom-firmware/    교체 전환(미래 계획). 실기 적용 금지
│   ├── tools/              서드파티 체크아웃(gitignore) + scripts/(우리 운영 스크립트, cam.py 등)
│   └── data/               ⚠️ 비공개: Sony 펌웨어 원본/추출, 디컴파일, NAND 백업, 디바이스 덤프, rootfs_usr 심링크
└── docs/
    ├── AI_DOCS/            0001.LOCAL_DEV_GUIDE.md, 0002.TODO.md …
    └── RE/                 [카테고리]/[NNNN. name].md + 카테고리 README.md, 전역 README.md
```
- **`docs/`는 카테고리 구조**(RE·AI_DOCS는 카테고리의 예시; 필요하면 더 만든다).
- **`docs/RE/[category]/`**: `NNNN. <name>.md`(4자리 인덱스) + 카테고리 상태 `README.md`. 전역 상태 `docs/RE/README.md`.

---

## 3. ⭐ 전역 규칙 (반드시 지킬 것)
1. **매 활동마다 커밋.** 복구/원인 파악이 쉽도록 논리 단위로 자주 커밋. 커밋 메시지에 무엇을·왜.
2. **RE를 할 때마다 보고서 작성.** 조사한 내용은 `docs/RE/[category]/NNNN. name.md`로 남기고 카테고리 README·전역 README 갱신. (서브에이전트가 RE했으면 그 결과도 보고서화.)
3. **GitHub public repo에 올릴 수 있는 내용만 커밋.** Sony IP(펌웨어 원본·디컴파일·바이너리)·NAND 백업·개인정보·디바이스 덤프는 **전부 `packages/data/`(gitignore)**. 공개 전 전수 감사.
4. **응답은 한국어.** 벽돌 위험 0(§1). 실험은 에뮬레이터 우선.

---

## 4. 아키텍처 — 두 트랙 (병행)
- **트랙 A — `packages/android-app/`**: ILCE Remote 포크. 검증된 카메라 플러밍(CameraEx 라이브뷰·초점·파라미터, ScalarInput) 재사용, UI/조작만 우리 설계. 설치/제거 가능 → 벽돌 안전. **UI 모양·조작 확인용 프로토타입.**
- **트랙 B(주력) — `packages/custom-binary-app/`**: `a5kinproc.so`를 im.elf에 ptrace 주입(route-b). 별도 네이티브 프로세스로 화면(VDF detour-swap)·입력(createEvent/InfraDial 훅)·카메라(엔진 param read/set)를 직접. 에뮬+실기 검증 진행 중.
- **제어 통로 = SDCom**(두 트랙 공통): 휘발 엔진 상태라 재부팅 복구=벽돌 안전.
- **최종 목표 = 교체 전환**(`packages/custom-firmware/`): 순정 앱 교체. 유일한 진짜 벽돌 위험 단계 → NAND 백업·복구경로·대면·전면검증 전까지 착수 금지.

---

## 5. 개발 워크플로 (핵심 경로 — 재구성 반영)
- **기기 telnet root**: `python3 -I packages/tools/scripts/cam.py '<cmd>'` (192.168.10.181:23, 무인증 root). busybox 최소(head/tail 없음 → `busybox <applet>`).
- **트랙 B 빌드**: `cd packages/custom-binary-app && ./build.sh a5000-inproc` (docker `a5k-cross`). 산출 `platform/a5000-inproc/build/a5kinproc.so`.
- **route-b 실기 배포**: a5kinproc.so + `packages/emulator/tools/a5kinject`(ARM static)을 RAM tmpfs `/android/a5k/`에 push(adb staging→telnet cp) → `a5kinject <im.elf pid> <so> c37cc`. teardown=`/android/a5k/stop` 파일(리부트 불필요 .so swap).
- **에뮬레이터**: `packages/emulator/` (규칙 `AI_WORKFLOW.md`). a5kshell.py·a5kinject로 게스트 조작, HMP screendump/sendkey.
- **IDA/Ghidra**: `docs/AI_DOCS/0001.LOCAL_DEV_GUIDE.md`(ssh windev IDA 9.4 / ssh oeindev Ghidra).
- 빌드/주입 세부·route-b 상세는 `packages/custom-binary-app/CLAUDE.md`.

---

## 6. 상태 / 방향 / 다음 — 여기 두지 않는다
CLAUDE.md는 **안정적 사실·규칙만**(자주 안 바뀜). 상태·방향·다음 할 일은:
- **전역 방향/TODO**: `docs/AI_DOCS/0002.TODO.md`
- **트랙 B 현황**: `packages/custom-binary-app/STATUS.md`
- **RE 상세/보고서**: `docs/RE/` (카테고리별 + 전역 `README.md`)
