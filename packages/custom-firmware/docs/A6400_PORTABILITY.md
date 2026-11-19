# a6400 (ILCE-6400, fw 2.00) 이식성 조사 — 2026-11-15

근거: 로컬 정적 조사(`firmware/Update_ILCE6400V200.exe`를 scratchpad에서 언팩), `tools/fwtool.py/devices.yml`, `tools/Sony-PMCA-RE/README.md`, 공개 저장소 ivica00/sony-a6400-research 첫 페이지. 기기 작업은 하지 않음. 확인되지 않은 항목은 [?]로 표시.

## 언팩 결과 요약
- `cfw/scripts/extract_dat.py`: 그대로 동작함(DATV/PROV/UDID/FDAT/DEND). `fwtool unpack`: 동작함(crypterName **CXD90045**, pycryptodomex 필요). 업데이터 USB PID는 0x3E2로 a5000과 같은 계열.
- 커널: `3.0.27_nl-rt106+ SMP PREEMPT RT`, gcc 4.5.1(Sony CE Linux 8.2.5.7), 빌드일 2019-05-27로 **a5000과 같은 커널 계열**. version.txt는 `38111_03.2019052701`.
- 파티션 구조(mount.conf)는 a5000과 거의 같음: /setting, /system(nflasha3=vmlinux+initrd), /wbi(nflasha5, `WBI1` 매직), /rootfs, /usr(nflasha15). mount.conf에 `/android/system`(nflasha16) 항목은 남아 있지만 **업데이트 패키지에 nflasha16 이미지가 없음**(a5000에는 있음).
- `/usr/lib`(126개)에서 **libsdcom.so·libScalarDaemon.so·sdmon.elf가 사라졌고**, `UX_DEBUG_CLIENT`·`ScalarDaemon`·dalvik 문자열이 nflasha3/5/7/15 어디에도 없음. 그대로 남은 것: `im.elf`(21KB 로더, 구조 동일), `libObj.so`, `viewUnified2~8.so`, `bk.elf`, `libbackup.so`(Backup_read/write), `grm_gles.ko`·`/dev/dmpgles2`, 커널 uinput.
- `devices.yml`에서 ILCE-6400 항목에는 `apps:` 필드가 없음(a5000/a6000에는 있음). 즉 **PMCA 앱 플랫폼이 없음**. PMCA-RE README: "CXD90045/CXD90057 기기는 **펌웨어가 암호 서명되어** 호환되지 않음."

## 구성요소별 이식성
| a5000 구성요소 | a6400 적용 | 근거 |
|---|---|---|
| fwtool 언팩 / extract_dat | **그대로 됨** | 이번 조사에서 실제로 언팩함 |
| fwtool **pack**(재포장, CFW) | **안 됨** | CXD90045는 서명 펌웨어(PMCA-RE README). 트랙 D는 사실상 막혀 있음 |
| Sony-PMCA-RE 앱 설치 / updatershell | **안 됨** [?] | CXD90045 미지원으로 명시됨. 대안 진입로는 ivica00이 찾은 Mass Storage "USB Debugger v2"(RAM `/tmp_bt`에 push+exec) |
| OpenMemories telnet root | **안 됨** | OM-Tweak은 PMCA 앱이라 a6400에 설치할 수 없음 |
| 트랙 A Android 앱(CameraEx/ScalarInput) | **안 됨** | Android 파티션 이미지, dalvik, PMCA가 모두 없음. 메모리상 "트랙 C=a6400 앱"이라는 전제는 성립하지 않음 [?: 실기 rootfs로 최종 확인] |
| 트랙 A의 UI 설계·레이아웃·EXP 설정 로직 | **변경 필요** | 디자인과 매핑 자산은 재사용 가능. 실행 기반은 네이티브로 다시 써야 함 |
| 트랙 B SDCom 제어 통로 | **안 됨 / 대체 필요** | libsdcom·ScalarDaemon 없음. 카메라 제어는 im.elf 내부 osal_uipc 메시지나 viewUnified/libObj 후킹으로 해야 함(ivica00 방식) |
| 트랙 B Rigel UX_DEBUG_CLIENT 창 | **안 됨** [?] | 문자열 없음. 그래픽은 grm_gles/dmpgles2 경로만 공통 |
| event0 / uinput 입력 | **변경 필요** [?] | 커널에 uinput은 있음. "D keyboard"를 a5000처럼 im.elf가 만드는지는 미확인 |
| Backup.bin / bk.elf / 화이트리스트 | **변경 필요** | bk.elf·libbackup 존재. propId 값은 기종별로 다시 확정해야 함(벽돌 규칙 동일) |
| 메뉴 데이터(viewUnified2 노드 트리) RE 기법 | **변경 필요** | 같은 so 이름과 구조. 오프셋과 노드는 새로 분석. ivica00 터치메뉴 mod가 viewUnified2/libObj/libMWF를 RAM 후킹으로 성공 |
| QEMU 에뮬레이터(CXD90014 모델) | **변경 필요** | ivica00 보고: a6400 = CXD90045, Cortex-A5×4, SMP. 같은 커널이라 머신 골격과 NAND HLE 방식은 재사용 가능성이 큼. 주변장치 주소와 블록은 재확인 필요 [?]. ivica00에도 out-of-tree QEMU `sony-a6400`(쉘까지 부팅)이 있음 |
| sub-micro / CamCore·SA DSP / F_USB20HDC 모델 | **변경 필요** [?] | 새 블록(usb_darwin, xhci-hcd.ko 존재 → USB3 컨트롤러로 바뀌었을 수 있음) |
| 벽돌 안전 원칙(RAM 전용, flash 금지) | **그대로 적용** | ivica00도 같은 규칙(flash 금지, /dev/mem 스윕 금지) |

## 다음에 할 빠른 조사
1. ivica00 저장소의 docs(USB Debugger v2 진입 절차, QEMU 머신 소스, mod loader)를 읽고 우리 트랙 B 설계와 대응시키기.
2. a6400 `libObj.so`·`viewUnified2.so`를 IDA로 a5000 것과 diff해 SDCom을 대신하는 제어 API(osal_uipc 메시지 ID)를 찾기.
3. nflasha7 rootfs의 init 스크립트에서 Android 기동 흔적과 im.elf가 띄우는 입력 장치를 확인하기.
4. a6400 실기 SoC 주소맵을 ivica00 QEMU 소스와 우리 `emulator/docs/HARDWARE.md`로 대조하기.

## 결론
**"조금 조사해서 그대로 옮기는" 수준은 아니다.** 재사용할 수 있는 것은 도구(언팩), 같은 커널과 파티션 지식, Sony UI 라이브러리(libObj/viewUnified) RE 기법, 에뮬레이터 골격, 안전 원칙, UI 디자인이다. 트랙 A(Android/PMCA), SDCom/Rigel 기반 트랙 B, OM telnet, fwtool 재포장(트랙 D)은 a6400에 **없거나 서명으로 막혀 있다**. 현실적인 a6400 경로는 ivica00 방식, 즉 USB Debugger로 RAM에 올리는 mod loader와 im.elf 라이브러리 후킹 위에 우리 UI를 네이티브로 다시 구현하는 것이다.

> 2026-11-15 사용자 확인: ivica00/sony-a6400-research의 USB Debugger + RAM mod loader 방식은 실제로 동작함(사용자가 직접 확인).
