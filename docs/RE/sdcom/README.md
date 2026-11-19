# sdcom — ScalarDaemon IPC (SDCom) 프로토콜

카메라 엔진 제어 통로. 큐 0x1540432, 휘발성 엔진 상태라 재부팅 복구(벽돌 안전).

| 번호 | 보고서 | 상태 |
|---|---|---|
| 0001 | [전송·연결·데몬 수명주기](0001.%20transport-connect-and-daemon-lifecycle.md) | connect observed / D2A 상태머신 derived |
| 0002 | [메시지 카탈로그](0002.%20message-catalog.md) | derived, 사실상 완전(146 id / 147 처리기) |
| 0003 | [카메라 파라미터와 Shift 명령](0003.%20camera-parameters-and-shift-commands.md) | observed (Get/ShiftShutter) |
| 0004 | [이벤트 구독과 수신 분기](0004.%20event-subscription-and-dispatch.md) | derived, id 왕복 미확인 |
| 0005 | [클라이언트 안전 규칙과 사고](0005.%20client-safety-rules-and-incident.md) | observed |

## 열린 항목
- standalone 프로세스에서 D2A/state 5 획득 (트랙 B 최우선 실기 프로브)
- 이벤트 id 왕복 실측, 프리뷰 프레임 pull 가능 여부
- Superior Auto exposureMode 수치

원본 TSV(메시지 id, 요청 파라미터, 카메라 파라미터, 이벤트 표)와 sdprobe 실행 로그는 `packages/data/re/`(비공개).
