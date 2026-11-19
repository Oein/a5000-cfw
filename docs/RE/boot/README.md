# boot — 펌웨어 패키징, 파티션, WBI, 자동 실행, 복구

| 번호 | 보고서 | 상태 |
|---|---|---|
| 0001 | [펌웨어 패키징·파티션·부팅 체인](0001.%20firmware-partitions-and-boot-chain.md) | derived + observed |
| 0002 | [WBI 스냅샷과 /usr 수정의 함정](0002.%20wbi-warm-snapshot.md) | 공장 스냅샷 재개 observed, 생성/무효화 derived |
| 0003 | [dmode + env.txt 자동 실행](0003.%20autostart-via-dmode-and-env.md) | observed (에뮬 확정) |
| 0004 | [복구 경로와 복구 불변식](0004.%20recovery-paths-and-invariant.md) | derived, 실기 리허설 미검증 |

## 열린 항목
- warm 재개 하에서 dmode/env.txt 자동 실행이 실기에서 적용되는 시점 확인
- `touch /setting/updater/mode` 리허설(사용자 승인 필요)
- 부트로더의 비-플래그 진입(키 홀드) 존재 여부
- ud_wbi_drop 라이브 효과(위험, 미시도)
