# menu — 순정 메뉴 구조·인벤토리·액션 경로

| 번호 | 보고서 | 상태 |
|---|---|---|
| 0001 | [순정 메뉴 노드 트리 구조](0001.%20stock-menu-node-tree-structure.md) | derived. 7번째 탭/항목 주입 RAM-only 불가 결론 |
| 0002 | [순정 메뉴 실측 인벤토리](0002.%20stock-menu-inventory-device-verified.md) | observed (실기 20장) |
| 0003 | [메뉴의 앱 실행과 액션 항목](0003.%20app-launch-and-action-items.md) | derived. openView 오프셋·비행기 토글 확정, ABT cmd 숫자 미확정 |
| 0004 | [메뉴 항목 구현 경로 맵](0004.%20menu-item-implementation-map.md) | 트랙 A 현황 + 네이티브 전용 분류 |

## 열린 항목
- 탭4 진입 시 실제 ABT_set_state 인자 시퀀스(실기 관찰)
- 각 액션 view*.so 내부 `requestModelExecute`의 숫자 cmd(직접 엔진 호출이 필요할 때만; "순정 view를 openView"하면 불필요)
- Fn 그리드 항목 집합 추출(입력 카테고리 [0003](../input/0003.%20exposure-dial-and-fn-menu.md) 참조)

메뉴 노드 트리 전체 덤프, 생성자 트레이스, 패치 레이아웃 JSON 등 원자료는 `packages/data/re/menu/`(비공개)에 있다.
