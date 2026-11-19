# a5000 UI (camera-ui)

커스텀 카메라 UI 앱. **목표: 순정 카메라 앱 "교체"** (지금은 실물에서 UI 확인용 프로토타입).
new-ilce-5000(ILCE Remote) camera 앱을 베이스로 포크 — 카메라 플러밍(CameraEx·ScalarInput·framework-res 아이콘·라이브뷰)은 검증된 것을 재사용, UI/조작만 우리 설계로.

## 베이스 대비 변경
- 폰 뷰어 web 번들 제거(온-카메라 UI엔 불필요). ApiServer는 `/api`·`/api/debug`만.
- appId `dev.oein.a5kui`, 라벨 "a5000 UI" (ILCE Remote와 공존 설치 가능).
- 조작: UP=셔터↔조리개, RIGHT=ISO↔노출 순환 (DOWN=퀵메뉴는 구현 예정).

## 빌드
```
cd camera-ui
./gradlew :app:assembleDebug     # app/build/outputs/apk/debug/a5k-ui-debug-a5k-0.1.apk
```
요구: JDK, Android SDK(local.properties의 sdk.dir), Gradle 8.11(wrapper). web/node 불필요.

## 설치 (카메라 온라인 필요)
- **USB(권장, WiFi 불필요)**: 카메라 USB=MTP 연결 → `sudo tools/pmca-console install -f <apk>`
- **WiFi+adb**: OpenMemories Tweak로 adb 켜고 → `adb connect <ip>:5555 && adb install -r <apk>`
- 설치 후 카메라에서 직접 실행(Menu > Application > a5000 UI). 셸 `am start`는 순정 런처가 막음.

## 로드맵
1. (현재) 실물에서 UI 확인 — 촬영 OSD·모드/드라이브 피커·조작.
2. 퀵 메뉴(WB·초점영역·초점, 5s 자동닫힘), 메뉴 α7식 재디자인.
3. 프리미엄 자동(virtualModeDial 네이티브 경로).
4. **교체 전환**: 순정 카메라 앱 식별 → /system 교체 → WBI 재생성 + 부팅 트리밍(순정≥ 속도).
