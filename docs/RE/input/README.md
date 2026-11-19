# input — 키·다이얼·ABT 입력 RE

a5000의 입력은 `im.elf`가 만드는 가상 evdev(`/dev/input/event0`)가 중심이며, 전 키를 받으려면 ABT(arbiter) 통지가 필요하다. 다이얼 회전은 별도의 InfraDial 경로다.

| 번호 | 보고서 | 상태 |
|---|---|---|
| 0001 | [evdev 키맵과 ABT 키 소유권](0001.%20evdev-keymap-and-abt-key-ownership.md) | 키맵 observed / ABT derived / 휴지통·Fn·DISP 미확정 |
| 0002 | [InfraDial 다이얼 회전 파이프라인](0002.%20infradial-dial-pipeline.md) | derived + 방향 판정 실기 정정(observed) |
| 0003 | [다이얼 노출 매핑과 Fn 메뉴](0003.%20exposure-dial-and-fn-menu.md) | derived / Fn 키코드 미확정 |

## 열린 항목
- 네이티브 프로세스에서 ABT APP_INFO 송신으로 전키 수신 검증
- 휴지통·Fn·DISP·C·SK1/SK2 evdev 코드 재캡처
- 상류 다이얼 이벤트 생산자(큐 0x11B02AA 송신자) 식별
