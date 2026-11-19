# 복구 가능성 지도 (RECOVERY MAP) — 메인 펌웨어를 어디까지 깨도 USB로 되살아나는가

> 범위: **복구 경로의 "도달 가능성(reachability)"만** 다룬다. 어떻게 플래시에 쓰는지(펌웨어 기록 절차)는 이 문서의 범위가 아니며, 적지 않는다.
> 목적: CLAUDE.md §1(벽돌 위험 0) / §2(교체 전환은 복구경로 완전 확보 후에만)의 **선행 점검**. "언젠가 메인 펌웨어를 교체할 때, 무엇을 건드리면 USB 복구(=updater core 도달)가 끊기는가"를 에뮬레이터로 실측.
>
> 환경: QEMU `sony-a5000` 머신, **미수정** Sony a5000 fw 1.10, 실험마다 **pristine NAND 클론**(`cp -c pristine.nand`), `backup/`는 절대 불건드림. 실기 아님. 바이너리 `qemu-recov`(= shared/qemu-system-arm-sgc1m), loader = `emulator/provision/loader.cpio`(현재 트리). 포트 46570–46572.
> 관련: `emulator/docs/RE_UPDATER_BOOT.md`(updater core 부팅 계약), `RE_REBOOT.md`(SWITCH_MODE 후 sub-micro 전원 사이클), `RE_USB_HOST_PATH.md`(USB 통로), `RE_GUEST_SHELL.md`(게스트 루트 셸 = 에뮬 전용 보조수단).
> `[?]` = 미증명.

---

## 0. 한 줄 결론

**복구 생명줄은 NAND의 두 파티션 — `nflasha1`(updater core, 독립 커널+initrd, 6 MiB plain FAT)과 `nflasha2`(`/setting`, updater 플래그 + Backup.bin)다.**
메인 펌웨어(`/system`=nflasha3, `/usr`=nflasha15, Android=nflasha16)를 **통째로 밀어도** updater core는 **그 둘에만 의존해서** 독립적으로 부팅하고 USB updater 셸까지 간다. 단, **updater로 가려면 `/setting/updater/mode` 플래그가 NAND에 있어야** 하고, **그 플래그를 세우는 유일한 소프트웨어 경로는 "메인 펌웨어가 USB 가젯 + `up.sh`를 돌릴 만큼 살아있을 때"(USB `SWITCH_MODE`)뿐이다.** 메인이 그 정도도 못 돌 만큼 깨지면 — 즉 화면이 검고 USB 열거가 안 되면 — **플래그를 세울 소프트 경로가 사라진다.** 그때는 실제 부트로더의 자체 updater 진입(키 조합 / 부팅 실패 카운터)에 의존해야 하는데, 그건 에뮬레이터로 표현 불가 → `[?]`.

---

## 1. 실험 결과표

각 행은 **독립된 fresh NAND 클론**에서 수행. "UI 부팅?"=촬영화면 도달, "USB 열거?"=`pmca-console info -d socket`가 기기를 찾는가, "updater 도달?"=플래그 세운 상태에서 `pmca-console updatershell -m ILCE-5000`가 **054C:03E2**로 updater 셸 프롬프트에 닿아 `help`에 응답하는가.

| # | 파손 수준 | UI 부팅? | USB 열거? | updater 도달? | 근거(런 / 로그) |
|---|---|---|---|---|---|
| 1 | **Baseline(stock)** | ✅ 촬영화면 | ✅ MTP **054C:08D3**, `info`→ILCE-5000/fw 1.10 | ✅ `updatershell` SWITCH_MODE → sub-micro 전원 사이클 → **054C:03E2** updater 셸, `help` 응답 | e1 |
| 2 | **플래그 사전 존재**(pristine 클론에 `touch /setting/updater/mode; sync` 후 전원 사이클) | — (바로 updater core) | updater MSC **054C:03E2** | ✅ 로더가 메인 상태와 무관하게 updater core 부팅 (`a5k-loader: /setting/updater/mode set -> updater core (nflasha1)`) | e2 |
| 3a | **`/usr`(nflasha15) 선두 1 MiB 0으로** = im.elf·전 커널모듈 소실 | ❌ 전 `/usr/kmod/*.ko` open 실패, im.elf 없음, 화면 검정 | ❌ 가젯 안 뜸(`No devices found`) | ⚠️ USB 경로로는 **불가**(위 ❌). 플래그 사전존재 시 ✅(`Sony DSC ... updater mode`, `help`, Done) | e3a |
| 3b | **Android(nflasha16) 선두 1 MiB 0으로** = `/android/system` 마운트 불가 | ❌ im.elf는 뜨지만 `zygote` disabled(`cannot find /system/bin/app_process`), Android 죽음, 화면 검정 | ❌ 가젯 안 뜸 | ⚠️ USB 경로 불가. 플래그 사전존재 시 ✅ | e3b |
| 3d | **`/system`(nflasha3, 메인 커널+initrd+av-cam) 선두 1 MiB 0으로** | ❌ **정상 부팅은 stage-2 진입조차 실패**: stage-1 로더가 `a5k-loader: FAILED: system mount` → 자체 fail `/bin/sh`로 떨어짐 | ❌ | ⚠️ USB 경로 불가. 플래그 사전존재 시 ✅ (nflasha1은 nflasha3와 **완전 독립**: updater는 자기 커널/initrd를 nflasha1에서 로드) | e3d |
| 3c | **im.elf 런타임 중단**(libupdaterufp.so bind-mask) | — | — | **판정 불가 `[?]`**: §3 참조 | e3c(flake) |
| 4 | **비-플래그 updater 진입**(전원투입 시 키 홀드 / 부팅 실패 카운터) | — | — | **`[?]` 에뮬레이터로 표현 불가** — §4 | — |

보조 사실:
- Baseline(e1)의 SWITCH_MODE 흐름은 `RE_REBOOT.md`대로 재현됨: sub-micro off 데이터 `factor 13(OFF_BY_UPDATE), next 0x20(restart), cold` → `power-off line p0.10 low` → `sub-micro power cycle` → DDR/IRAM clear → cold boot → nflasha1 updater(977 820 B 커널 @0x81528000, 3 809 280 B initrd @0x80628000).
- UFP 모드는 시작 시 `comp`가 있으면 `mode*`를 **스스로 삭제**(`execute_ufp.sh`)하므로, 정상 카메라는 updater 왕복 후 **다음 부팅이 다시 normal**이 된다(RE_UPDATER_BOOT §5). 즉 플래그는 "복구 왕복용 1회성 토글"이지 영구 상태가 아니다.
- nflasha1 md5는 모든 파손 실험에서 불변(`c06cc96d...`): 메인을 밀어도 updater 파티션은 건드려지지 않음(확인).

---

## 2. 왜 이렇게 되는가 (복구 통로의 구조)

```
전원 투입
  └─ (실제) 부트로더  ─┐
       stage-1 loader │  /setting/updater/mode 있나?
                      ├─ 있음 → nflasha1 에서 updater 커널+initrd 로드 → updater core
                      │          └─ sauu → USB 가젯 054C:03E2(MSC) → PMCA updater 셸
                      └─ 없음 → nflasha3(/system) 메인 커널+initrd → stage-2
                                 └─ /usr(nflasha15)·Android(nflasha16) 마운트 → im.elf·zygote → UI·USB(MTP)
```

- **updater core는 nflasha1 + nflasha2(플래그)만으로 완결된다.** 자기 커널·initrd·`sauu`를 전부 nflasha1에서 가져오고, 메인의 `/system`·`/usr`·Android 중 **무엇도 참조하지 않는다**. → 표의 3a/3b/3d가 전부 "플래그만 있으면 updater ✅"인 이유.
- 반대로 **메인 펌웨어가 USB를 여는 건 전적으로 메인 쪽 소프트에 달렸다.** USB MTP 가젯은 im.elf/libObj의 app framework(`ModelUsbMtp…`)가 올려야 뜬다. e3b에서 im.elf가 떠 있어도 Android가 죽으면 가젯이 안 떴다 → **USB 열거에는 im.elf 생존만으로 부족하고 앱/모델 트리까지 필요**하다(관찰값).
- 플래그 `/setting/updater/mode`를 **쓰는** 정상 경로는 둘뿐:
  1. **USB `SWITCH_MODE`(0x30)** → 메인 fw의 `libupdaterufp.so` → `system("up.sh user")`가 플래그 생성 + sub-micro restart. **메인이 USB+up.sh를 돌 만큼 살아있어야 함.**
  2. **루트 셸에서 `touch`**(OpenMemories telnet 또는 에뮬 전용 `A5K_SHELL`). 이것도 메인 fw(또는 최소한 셸)가 떠 있어야 함.

→ **핵심 안전 명제:** 복구(updater 도달)는 "플래그가 이미 있거나" 혹은 "플래그를 세울 만큼 메인이 살아있거나" 둘 중 하나를 요구한다. **메인을 '화면 검정 + USB 무열거'까지 깨 버리면, 소프트웨어만으로 플래그를 세울 방법이 없어진다.**

---

## 3. e3c(im.elf 런타임 중단) 판정 불가 사유 `[?]`

- 시도한 방법(루트 셸에서 `libupdaterufp.so`를 빈 파일로 bind-mount)은 **tmpfs 런타임 변경이라 재부팅 시 사라짐** → "다음 부팅의 복구 가능성"을 보여주지 못함. 방법상 부적절.
- 또한 해당 런 다수가 av-cam **task-113 NULL memcpy oops**(`PC 0x4394aa4c / LR 0x437dee4d`)에 걸렸는데, 이는 `emulator/docs/RE_ENGINE_OOPS.md`에 기록된 **에뮬레이터 CPU 기아(starvation) 아티팩트**다(실기와 무관, 호스트에서 QEMU 다수 병렬로 load avg 30+일 때 악화). 복구 가능성 신호가 아님.
- "im.elf가 안 뜨는" 상황의 깨끗한 대역은 **3a(/usr 소거)**가 이미 제공한다: im.elf 바이너리·커널모듈이 전부 `/usr`에 있으므로 /usr을 밀면 im.elf는 못 뜨고 → UI·USB 모두 죽는다. 따라서 "im.elf 불능 → USB 복구 경로 상실"은 3a로 확정.

---

## 4. 비-플래그 updater 진입 (키 홀드 / 부팅 실패 카운터) `[?]`

- 에뮬레이터의 stage-1 로더(`provision/loader.sh`)는 **오직 `/setting/updater/mode` 파일 존재 여부**로만 updater를 결정한다. 부팅 실패 시 fallback updater 진입은 **모델에 없다**: 3d에서 보듯 메인이 못 뜨면 로더는 그냥 fail `/bin/sh`로 떨어질 뿐, updater로 자동 전환하지 않는다.
- 실제 Sony 부트로더 바이너리는 입수/모델링되지 않았다(RE_UPDATER_BOOT §3, §7의 미해결 1·2). 따라서 **실기의 키 조합·부팅 실패 카운터 진입 여부는 에뮬레이터로 확인 불가** → `[?]`. 실기 콜드 부팅 1회(플래그만, `ud_send_lsi` 없이)로만 가려질 문제(RE_UPDATER_BOOT 미해결 1).
- HMP `sendkey`를 전원투입 중 홀드하는 것도 **부트로더가 없어 의미 없음**(입력은 커널 uinput `event0`로만 들어오고, 이는 부팅 이후). 표현 불가로 결론.

---

## 5. 복구를 유지하려면 절대 건드리면 안 되는 것 (결론)

교체 전환(CLAUDE.md §2 최종 목표) 설계 시, **다음을 보존해야 USB 복구가 살아있다**:

1. **`nflasha1`(updater core) — 절대 불가침.** 메인을 아무리 바꿔도 이게 멀쩡하면 "플래그만 있으면" 되살아난다. 교체 작업이 nflasha1을 덮거나 그 유효성을 깨면 **진짜 벽돌**. (실험상 nflasha1은 전 과정 md5 불변으로 안전지대 역할 확인.)
2. **`nflasha2`(`/setting`) — 플래그와 Backup.bin의 집.** FAT가 깨지면 플래그를 세울 자리도 없고 Backup 복구도 흔들린다.
3. **"플래그를 세울 능력"의 보존.** 실무상 가장 위험한 지점. 교체한 메인 UI/앱이 **USB SWITCH_MODE 경로(libupdaterufp 로드 → `up.sh user` 실행 → sub-micro restart) 또는 루트 셸을 끝까지 살려두지 못하면**, 그 다음 단계에서 뭔가 잘못됐을 때 **플래그를 세울 소프트 경로가 없어 복구가 끊긴다.** → 교체 펌웨어는 "자기 자신이 깨지기 전에, updater로 넘어가는 USB 명령/셸을 항상 응답할 수 있어야" 안전마진이 생긴다.
4. 따라서 **교체 전환 착수 전 필수 선결:** (a) nflasha1/nflasha2 무결성 보장 + 외부(NAND 덤프) 복구 경로 확보, (b) 교체본이 어떤 상태로 죽든 **플래그 진입(USB 또는 셸)이 남아있음**을 보장하거나, 그게 불가능하면 **실기에서 비-플래그 부트로더 진입(§4 `[?]`)의 존재를 먼저 확인**. 이 중 하나라도 없으면 교체는 CLAUDE.md §1 기준 금지.

## 6. 미해결 / 한계 `[?]`

- §4: 실제 부트로더의 키 조합·부팅 실패 카운터 updater 진입 — 에뮬 표현 불가, 실기 1회 콜드부팅 필요.
- 파손은 각 파티션 **선두 1 MiB zero**로만 시뮬했다(호스트에서 raw 이미지 기록 도구는 쓰지 않고, 게스트 셸 `dd`로 클론 NAND에만 적용). 커널/initrd 자체를 "부팅 불가"로 만드는 더 깊은 손상(예: nflasha3 전체 malformed)은 3d(선두 소거 → `FAILED: system mount`)로 대표했고, 로더가 그 아래 단계로 못 가는 것까지만 확인. 실제 부트로더가 그 상황에서 뭘 하는지는 `[?]`.
- `/usr`·`/system`은 읽기전용 squashfs/ext2/LZPT라 **런타임 호스트 수정 없이** 게스트 `dd`로 **블록 디바이스에 직접** 기록해 클론을 깼다(쓰기 성공·md5 변화 확인). 전원 사이클 후 반영됨(3a/3b/3d 모두 재부팅에서 파손 효과 관찰).
- e3b: im.elf 생존에도 USB 미열거 — "USB 가젯 기동에 앱/모델 트리의 어느 부분까지 필요한지"의 정밀 경계는 미측정 `[?]`(이 문서 결론에는 영향 없음: 어느 쪽이든 심한 파손=USB 상실).
