# sensor — 모션/방향 센서 (가속도계·디지털 레벨)

바디 자세/방향을 읽는 경로. accel.ko(MMA845x) → libdig_level(각도·사분면) → SDCom 이벤트 / Android HAL. 전부 읽기·구독 = 벽돌 안전.

| 번호 | 보고서 | 상태 |
|---|---|---|
| 0001 | [바디 회전/방향 읽기](0001.%20orientation-read.md) | derived (생산자 체인·4사분면 분류·SDCom/Android 경로), msgId·축대응은 실기 확정 대기 |

## 열린 항목
- `StartDigitalLevel`/`StopDigitalLevel` 수치 msgId, `SDEVENT_DIGITALLEVEL_ANGLE_UPDATE` SD_EVENT_ID·ParamList 레이아웃 (이벤트 왕복 계측)
- a5000 실기가 실제로 각도 스트림을 내보내는지(`PROP_DEVICE_DIGITAL_LEVEL`), Android `OrientationWatcher` 로그로 즉시 검증
- libdig_level 0/1/2/3 ↔ Android 0/90/180/270 ↔ EXIF orientation 축/부호 1:1 대응 봉인

원시 덤프(IDA, device 캡처, 앱 소스 인용)는 `packages/data/re/`(비공개).
