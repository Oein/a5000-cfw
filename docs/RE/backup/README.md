# backup — Backup.bin / BKID 영속 설정

`/setting/Backup.bin` 구조, 안전 쓰기 집합(Tier A/B/C), 설정↔propId 실측 매핑. (CVPDM 그룹 쓰기 경로와 설정 옵션 트리는 settings 카테고리 소관.)

| 번호 | 보고서 | 상태 |
|---|---|---|
| 0001 | [Backup.bin 구조와 쓰기 안전 등급](0001.%20backup-bin-structure-and-write-safety.md) | derived + observed |
| 0002 | [설정 ↔ propId 실측 매핑](0002.%20propid-mapping-measured.md) | observed (6개 확정) |
| 0003 | [제브라 라이브 적용 경로](0003.%20zebra-live-apply-path.md) | derived (SDCom 불가) |

## 열린 항목
- Tier B(PAL/언어/지역) propId, Superior Auto propId
- 유효 값 범위가 필요한 새 값 쓰기
- 제브라 PARAMID 수치, BKID 심볼 정합

원본 Backup.bin 파싱 결과, 173개 목록, BKID 심볼 목록(197)은 `packages/data/re/`(비공개).
