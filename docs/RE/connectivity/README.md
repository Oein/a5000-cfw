# connectivity — 무선/USB/앱 "기능 플로우" (사용자 기능 단위)

> 순정 연결 기능(스마트폰 전송·스마트폰 제어/Smart Remote·TV 보기/DLNA·NFC 원터치·PlayMemories 앱·USB MTP/PC리모트)이
> **진입점 → 어느 데몬/서비스 → 전송 프로토콜**로 어떻게 동작하는지, 그리고 route-b(주입 UI)가 각각을 **구동할 수 있나/
> 순정·Android로 둬야 하나**를 정리. 스택 자체(AR6003/wpa_supplicant/USB 컨트롤러)·AF중 Wi-Fi 끊김은 아래 선행 문서 소관.

| 번호 | 보고서 | 상태 |
|---|---|---|
| 0001 | [Wi-Fi·NFC·USB·앱 기능 플로우 + route-b 판정](0001.%20wifi-nfc-usb-apps.md) | derived(정적) + emulator-observed. 6개 기능 플로우 전수 매핑. orb-server(WebAPI :10000) 신규 RE 포함 |

## 현재 요약 (한 줄씩)
- **스마트폰 제어 / Smart Remote** = Sony **Camera Remote API**(`orb-server` 독립 데몬, TCP **:10000**, 엔드포인트 `/sony/camera|guide|system`, 라이브뷰 MJPEG **:60152**). im.elf WebApiAddon이 실제 카메라 명령 처리, orb-server는 HTTP↔im.elf 브리지. route-b가 **직접 띄울 수 있음**(RAM 바이너리).
- **TV 보기** = UPnP/**DLNA DMS**(MediaServer:1, ContentDirectory+ConnectionManager) + Sony **XPushList** 서비스로 DMR(TV)에 푸시. 서버는 **im.elf 내부**(`libInfraDlnaControl`), 독립 데몬 아님.
- **스마트폰 전송 / 컴퓨터 전송** = Wi-Fi Direct GO(카메라=AP 10.0.0.1) 뒤 im.elf 내부 전송 서버가 PlayMemories Mobile/PlayMemories Home에 이미지 제공. 전송 와이어 세부는 device-only.
- **NFC 원터치** = FeliCa Plug 동적 태그(읽기전용 NDEF: Wi-Fi Direct SSID/PW + PlayMemories AAR). 상세는 `emulator/docs/RE_NFC.md`. route-b는 on/off·handover 구독만 가능, 태그 응답은 im.elf 내부 InfraNfc 고정.
- **PlayMemories 앱(PMCA)** = `/android` chroot 안 Android 2.3.7 액티비티. 실행=ABT 월드 스위치(`menu/0003`). 설치=USB 앱다운로드 가젯(`emulator/docs/RE_USB_APP_INSTALL.md`). OpenMemories 중단 → 우리 자작 설계로 대체.
- **USB MTP vs PC리모트** = 같은 F_USB20HDC 가젯, PID만 다름(MTP 0x047D계열 / PC-Remote 0x0641). 모드 선택 = `view/USBAUTO|USBMASSSTORAGE|USBMTP|USBREMOTEPC`. 컨트롤러는 `emulator/docs/RE_USB*`.

## 선행/인접 문서 (중복 작성 금지)
- `packages/emulator/docs/RE_WIFI.md` — AR6003 스택·wpa_supplicant·CT 서비스·에뮬 모델 스펙.
- `packages/emulator/docs/RE_NFC.md` — NFC 칩/드라이버/NDEF/handover 전수.
- `packages/emulator/docs/RE_USB.md` · `RE_USB_DEVICE_CONTROLLER.md` · `RE_STORAGE_USB.md` · `RE_USB_APP_INSTALL.md` — USB 가젯/컨트롤러/앱설치.
- `docs/RE/network/0001` — AF중 Wi-Fi 끊김(스케줄링 기아). **본 카테고리는 이걸 다시 다루지 않음.**
- `docs/RE/menu/0003` — 앱 실행(ABT 월드 스위치)·액션 항목·무선 메뉴 트리거.
- `packages/android-app/docs/NATIVE_PORT_PLAN.md §5` — Wi-Fi 자작 설계(W1~W6).

## 열린 항목 (device-only)
- Camera Remote API의 실제 getEvent/liveview 바이트, orb-server↔im.elf IPC 메시지 포맷(정적 심볼은 확보, 런타임 캡처 필요).
- "스마트폰 전송" 이미지 전송 와이어 프로토콜(DLNA pull인지 Sony 전용 push인지) — 실기/폰앱 캡처.
- ABT `ANDROID_*` cmd 숫자값과 프로덕션 호출 시퀀스(`menu/0003` 미확정).
- PC Remote(USBREMOTEPC) PTP 벤더 오퍼레이션 세트.
