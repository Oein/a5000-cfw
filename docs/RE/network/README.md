# network — Wi-Fi/네트워크 스택 동작, 끊김 원인, 원격 제어 채널

| 번호 | 보고서 | 상태 |
|---|---|---|
| 0001 | [AF/라이브뷰 중 Wi-Fi 끊김 — 의도 vs 부작용](0001.%20af-wifi-drop.md) | derived + emulator-observed. **판정: 부작용(PREEMPT_RT 스케줄링 기아)**, Sony의 의도적 정지 경로 없음. RAM-only 완화책 4종, 실기 1회 계측으로 봉인 대기 |

## 현재 요약
- Wi-Fi 데이터 경로(ksdioirqd FIFO 1, htc_rx/ath6kl 워크큐·telnetd·adbd = CFS)는 av-cam µITRON 태스크(SCHED_FIFO 1–154 커널 스레드 111개)와 im.elf RR 스레드(68개) 아래에 있다. mmc1 IRQ는 CPU0 고정.
- NetService 상태기계의 정지 트리거는 API(Android/Sony UI), 시스템 서스펜드(IMDB `*_sus`), HW 에러, 핫슈 분리뿐 — 이미징 트리거 없음.
- 보조 요인: `vm.min_free_kbytes=256` + ath6kl AMSDU RX 버퍼 order-1 원자 할당.
- 스택/프로토콜 자체는 `packages/emulator/docs/RE_WIFI.md`.

## 열린 항목
- AF 중 실제 RT 부하 증분과 어느 층이 Wi-Fi를 굶기는지(실기 계측, 0001 §7).
- 링크(결합) 유지 여부, page-alloc 실패의 시간 상관.
- `services.dex`(odex) WifiService의 소니 수정 여부 — 즉시 끊김과는 무관하나 미해석.
- USB 제어 채널(순정에 네트워크/시리얼 가젯 없음): PTP 벤더 오퍼레이션 또는 자작 가젯 모듈 RE.
