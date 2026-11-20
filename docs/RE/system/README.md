# system — 시스템 / 유지보수 (Setup 탭 잔여 항목) RE

> a5000(ILCE-5000) **Setup(공구) 탭**에서 다른 카테고리가 아직 다루지 않은 **시스템/유지보수** 항목을 매핑한다: 데모 모드, 센서 클리닝(존재 여부), 버전 표시, 인증 로고, 이미지 DB 복원, 미디어 정보 표시. 그리고 "이미 매핑된 시스템 항목"은 재작업하지 않고 해당 문서를 가리킨다.

공통 모델: 이 항목들은 대부분 **값 토글(Backup propId / 설정노드)** 아니면 **뷰 실행(open-view Event)** 이다 — 실시간 카메라 데이터면이 아니다. 실행형은 [menu/0003](<../menu/0003. app-launch-and-action-items.md>)의 ViewManager open-view Event 경로를 그대로 탄다.

상태: **정적 분석 확정**(`packages/data/re/ida/` Hex-Rays 캐시 + 디바이스 실측 메뉴 inventory `packages/data/re/static/inventory.md` + `docs/RE/menu/0002` 교차). 영구 쓰기 안전등급은 [backup](../backup/README.md).

## 문서
- [0001. maintenance-and-misc.md](<0001. maintenance-and-misc.md>) — 데모/클리닝/버전/인증로고/이미지DB복원/미디어정보 control + 기구 + route-b 판정, 그리고 "이미 커버됨" 매핑.

## 한 줄 결론

| 항목 | control | 기구 | route-b 판정 | 커버 위치 |
|---|---|---|---|---|
| **데모 모드** `SetupDemoMode` | 영구 토글 On=0/Off=1 (Backup/설정노드) + 실행 `viewDemoMoviePb` | 아이들 시 데모 동영상 재생 | ✅ 토글+뷰 실행(안전) | **신규(여기)** + 실행경로 menu/0003 |
| **센서 클리닝** `SetupCleaningMode` | — | 펌웨어엔 `ModelCleaning`/`MSGID_CLEANING_MODE_CMD`/`SeqCleaningMode` 존재하나 **a5000 메뉴엔 노출 안 됨** | ⛔ a5000 미노출(구현 불필요) | **신규(여기) — 핵심 발견** |
| **버전** `SetupVersion` | 읽기전용 뷰 `ViewVersionNumberToInstance`(viewUnified6) | 펌웨어/렌즈 버전 문자열 표시 | ✅ 뷰 실행(읽기) | **신규(여기)** + menu/0003 |
| **인증 로고** `CertificationLogo` | 읽기전용 뷰 `view/CERTIFICATIONLOGO`(viewUnified6) | 규제 인증 아이콘 표시 | ✅ 뷰 실행(읽기) | **신규(여기)** + menu/0003 |
| **이미지 DB 복원** `MemoryCardRecoverImageDB` | 실행 뷰 `view/MANUALREPAIR`/`AUTOREPAIR`(viewUnified8) | SD 이미지DB(.MDB 등) 재작성(원본 보존) | 🔶 뷰 실행(장시간, 자동실행 금지) | menu/0003(경로) + 여기(기구) |
| **미디어 정보 표시** `MemoryCardDisplayCardSpace` | 읽기전용 뷰 | SD 잔여 용량 표시 | ✅ 뷰 실행(읽기) | **신규(여기)** |
| PAL/NTSC `SettingSetupNtscPalSEL` | 영구+재부팅 | TV 포맷 전환 | ⚠️ 순정 뷰 경유 권장(Tier B) | menu/0004(native-only) |
| USB 연결/LUN, HDMI 해상도/제어 | 영구 토글/뷰 | — | ✅ 순정 뷰/토글 | menu/0003·0004, connectivity |

범례: ✅ 안전(토글/읽기뷰) · 🔶 실행형 주의 · ⚠️ 민감(영구/재부팅) · ⛔ a5000 미노출.

## 이미 다른 RE가 커버한 시스템 항목 (재작업 안 함)
- **포맷 / 설정 리셋(Initialize) / 파일 번호 / REC 폴더·새 폴더·폴더명 / USB 연결·LUN / HDMI 해상도·제어 / 언어 / 지역·날짜시간 / 클리닝·캘리브레이션 실행경로** → [menu/0003](<../menu/0003. app-launch-and-action-items.md>)(view-launch + 위험/게이트), [menu/0004](<../menu/0004. menu-item-implementation-map.md>)(네이티브 전용 ~13항목 목록).
- **모니터 밝기** → [settings/0001](<../settings/0001. monitor-brightness-apply.md>)(group 120).
- **절전 시작 시간 / 음량 / 오디오 신호 / 삭제 확인 / 타일 메뉴** 등 설정-노드 토글 enum → [menu/0002](<../menu/0002. stock-menu-inventory-device-verified.md>)(실측 inventory) + [backup](../backup/README.md)(영구 propId·쓰기 안전집합).
- **영구 저장 안전등급**(propId Tier A/B/C) → [backup/0001](<../backup/0001. backup-bin-structure-and-write-safety.md>)·[0002](<../backup/0002. propid-mapping-measured.md>).

## 핵심 결론
- Setup 탭 잔여 항목은 **실시간 엔진 데이터면이 아니다** — 값 토글(영구=backup propId, 휘발=설정노드) 또는 뷰 실행이다. route-b는 전부 **순정 뷰를 open-view Event로 띄우거나**(menu/0003 경로) **안전한 토글**만 하면 된다. 벽돌 위험 0.
- **센서 클리닝은 a5000에서 메뉴에 없다**(핵심 발견): 공유 펌웨어엔 `SetupCleaningMode`/`ModelCleaning`/`MSGID_CLEANING_MODE_CMD`/`SeqCleaningMode`가 컴파일돼 있으나, 디바이스 실측 Setup 탭(inventory, menu/0002)엔 항목이 없다 → a5000은 초음파 더스트 제거 하드웨어가 없어 노드가 게이트오프. 우리 UI에 넣을 것 없음.
- 버전/인증로고/미디어정보 = 읽기전용 뷰. 데모 모드 = 영구 On/Off + 데모재생 뷰. 이미지DB복원 = 실행형(장시간, 자동실행 금지).

## device-only / 미확정
1. `SetupDemoMode` / PAL·NTSC 등 영구 토글의 정확한 **Backup propId**(subsystem 0x107 인덱스) — backup/0002 측정표와 대조해 확정(실기 메뉴-쓰기 덤프).
2. `SetupVersion`이 읽는 펌웨어/렌즈 버전 문자열 **소스**(system model query 경로 상세) — 읽기전용이라 route-b 영향 없음, 필요 시 IDA.
3. 센서 클리닝 게이트 조건(InhFeatureCheck 모델 플래그) — a5000 미노출만 확인됨, 게이트 분기 상세는 미확정(불필요).

관련: [menu](../menu/README.md), [settings](../settings/README.md), [backup](../backup/README.md), [connectivity](../connectivity/README.md), [playback](../playback/README.md).
