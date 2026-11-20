# boot — 펌웨어 패키징, 파티션, WBI, 자동 실행, 복구

| 번호 | 보고서 | 상태 |
|---|---|---|
| 0001 | [펌웨어 패키징·파티션·부팅 체인](0001.%20firmware-partitions-and-boot-chain.md) | derived + observed |
| 0002 | [WBI 스냅샷과 /usr 수정의 함정](0002.%20wbi-warm-snapshot.md) | 공장 스냅샷 재개 observed, 생성/무효화 derived |
| 0003 | [dmode + env.txt 자동 실행](0003.%20autostart-via-dmode-and-env.md) | observed (에뮬 확정) |
| 0004 | [복구 경로와 복구 불변식](0004.%20recovery-paths-and-invariant.md) | derived, 실기 리허설 미검증 |
| 0005 | [Warm 부팅(WBI 스냅샷 재개) 복원 시퀀스·에뮬 구현](0005.%20warm-boot-restore.md) | 시퀀스 derived+observed, SS 커널·디바이스 재개 observed, im.elf UI 미도달 |

## 열린 항목
- (해소: [0005] §6) ~~warm 재개 하에서 dmode/env.txt 자동 실행이 적용되는가~~ → **warm(HOT·SS)에서는 적용 안 됨**(init 미실행); 콜드에서만 트리거, warm엔 route-b 주입
- SS 경로 im.elf 벽: `initialize_setting`의 `Backup_nread(0x01700000,4)`가 재개 시 ≠4 반환 → assert/abort; 최소 스텁 = SS 경로 Backup 서빙 정합(backup.ko 재개/NAND 읽기 레벨 또는 HLE) — [0005] §5 블로커2
- SS NAND/MUMIN 하드오프: 현재 device-private 런타임 파일(`nand_lowmem.bin`) 자동 로드로 해결; 장차 보드가 FTL 하드오프 직접 재현 가능 — [0005] §2·블로커1
- `touch /setting/updater/mode` 리허설(사용자 승인 필요)
- 부트로더의 비-플래그 진입(키 홀드) 존재 여부
- ud_wbi_drop 라이브 효과(위험, 미시도)
