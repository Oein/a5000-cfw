# BOOT_TIME — a5000 부팅 시간 프로파일과 최적화 계획 (커스텀 시스템용)

> 작성 2026-11-15. RE 에이전트(측정·에뮬레이터 실험·설계만). 실기 기록/커밋 없음.
> 원천: `emulator/`(QEMU `sony-a5000`) 위에서 **미수정 순정 fw 1.10**을 콜드부트시켜
> snsc 부팅타임 링(PA 0x834C0000) + klog 링(0x834E0000) + 화면덤프 + 게스트 셸 프로세스 추적으로 측정.
> 관련: `emulator/docs/RE_WARM_RESUME.md`(WBI/HOT 스냅샷 부팅), `emulator/docs/BOOT.md`(2단계 부팅),
> `emulator/docs/RE_USERLAND_SUBCPU.md`(userland/av-cam 시퀀스), `app/docs/NATIVE_PORT_PLAN.md`(트랙 B),
> CLAUDE.md §1(벽돌 0)·§2(두 트랙).

## 0. 설계 제약 (사용자 확정)
- **소니의 처리 결과는 100% 동일 유지**: 카메라 엔진·이미지 파이프라인·인코딩·AF/AE/AWB·렌즈/센서 제어·
  미디어 DB·화질. 부팅 단축은 **UI 레이어·Android·비필수 블로트 + 순서/병렬화/스냅샷 부팅**을 겨냥한다.
- 처리 컴포넌트의 **init 자체는 최적화 가능**하되 **출력이 완전히 동일할 때만**(엔진 상태·메시지 동일,
  캡처 동작 시 캡처물 바이트 동일로 A/B 증명). 플래시를 바꾸는 항목은 **[설계 전용]** 표시.
- 에뮬레이터에서는 처리 비활성 실험도 **측정용으로는** 허용하되, 권고안은 처리를 보존한다.

## 1. 측정 방법 · 시간축 주의 ⚠️
- **게스트 시간만 보고한다.** 호스트(macOS, 다수 에이전트 동시 실행 load avg 16~47)가 느려서
  **벽시계(wall)는 게스트 실제보다 ~2배 부풀려진다**(b1 벽시계 CamReady 16.9s vs icount 8.6s).
  따라서 모든 수치는 (a) 커널 printk/snsc 링 타임스탬프(sched_clock), (b) `jiffies_64`(PA 0x80360240, HZ=100),
  (c) `-icount shift=0,sleep=off`(게스트 명령수 기준 결정론적 시간)로 취한 **게스트 초**다.
- `ic5`(순정 cmdline, 셸 없음) T_shoot 8.69s ≈ `ic0`(ttyAM1 셸 계측) 8.71s → **계측 오버헤드 무시 가능**,
  계측값이 순정 부팅을 대표함을 확인.
- 계측 훅: stage-1 로더가 stage-2 initrd 대신 **훅된 initrd**(`/sbin/init`=워처+원래 init)를 올려,
  usr 마운트 후 신규 PID를 200ms 간격으로 커널로그에 기록(`A5KBT proc …`)하고 IMDB/Android를 bind-mount로
  교체. 순정 바이너리·NAND·커널은 **미수정**(에뮬레이터만, 벽돌 0).
- 스크래치: `scratchpad/re-boottime/`(`bt2.py` 하네스, `pbt.py` 링 파서, `ana.py` 마일스톤, `ic0`~`ic5` 런).

## 2. 콜드부트 타임라인과 임계경로 (게스트 초)

2단계 부팅이다(`emulator/docs/BOOT.md`): **stage-1 = NAND 로더 커널**(소니 NAND 스택으로 MUMIN/Backup/
av-cam.bin을 RAM에 올리고 리셋) → **stage-2 = 실제 커널+UI**. snsc 링은 stage-2에서 리셋되어 다시 0부터다.

| 단계 | 구간(stage-2 게스트초) | 소요 | 내용 | 임계경로? |
|---|---|---|---|---|
| **S1** stage-1 로더 | 별도 커널, 0→1.6 | **~1.6s** | 전체 Linux 부팅→NAND 모듈 insmod→av-cam.bin/Backup.bin 복사→`reboot -f` | ✅ |
| **A** stage-2 커널 | 0→0.30 | 0.30s | initramfs(8MB) 해제, 4×A5 SMP 기동, 기본 드라이버 | ✅ |
| **B** init | 0.30→0.64 | 0.34s | rootfs/usr/system/setting 마운트, **33개 .ko insmod**, bootin→im.elf fork | ✅ |
| **C** im.elf u+i 스테이지 | 0.64→1.10 | 0.46s | infra 라이브러리 ~60개 dlopen+init, **liro→av-cam entry_pre/main**(0.68) | ✅ |
| **D** 엔진 내부 init + Android 대기 | 1.10→3.56 | **2.46s** | av-cam 내부 시퀀스 + **설정DB 복원 대기**(zygote 2.79s 후 arbiter BOOT_CMP 3.56) | ✅ |
| **E** UI 첫 합성 | 3.56→3.93 | 0.37s | viewUnified/libObj 오브젝트 생성, 패널 ON, **OSD 화면 표시**(T_osd≈3.6) | ✅ |
| **F** 라이브뷰 YC 핀 연결 대기 | 3.93→8.60 | **4.67s [?]** | `EV PIN_OPEN`→`CamReady`. 센서 YC 출력 대기 — **에뮬 과장 의심**(ISP 미모델) | ✅ |

- **stage-2 → 촬영화면 ≈ 8.7s, stage-1 포함 콜드부트 총 ≈ 10.3s (게스트).**
- **OSD 최초 표시(T_osd) ≈ 3.6s(stage-2)/≈5.2s(총).** 전체 OSD(T_shoot) ≈ 8.7s/10.3s.
- 임계경로 한 줄: `stage-1 로더 → stage-2 커널 → init(모듈/마운트) → im.elf infra → av-cam 엔진(센서/SA/YC) → UI 합성`.
  OSD는 CamReady를 기다리지 않고 3.6s에 이미 뜬다(E가 F에 안 막힘 — 좋은 설계, 유지).

### 2.1 임계경로에서 소비 주체
- 지배 구간은 **D(2.46s) + F(4.67s) = 7.1s로 전체의 ~82%**, 둘 다 **av-cam 카메라 엔진**(소니 처리, 보존 대상).
  단 D에는 **Android 설정DB 복원 대기**가, F에는 **에뮬 과장 의심 대기**가 섞여 있어 *처리 자체를 안 건드리고*
  순서/병렬화로 줄일 여지가 있다(§4).
- Android(zygote 2.79s, system_server 2.84s, servicemanager/vold/netd/installd/mediaserver 0.87s~)은
  `taskset 0x8`로 **CPU3 전용**, 엔진(CPU0-2)과 병렬 → **첫 프레임 임계경로 밖**. 단 **예외**: UI의 BOOT_CMP가
  `libSyncAndroid`의 설정 복원(→ `/android/data` 마운트, Android 기동과 동시)에 걸려 **Android 데이터 경로는
  임계경로에 있다**(§3 ic1 참고).
- 모듈 33개 insmod는 0.33→0.41(~80ms)로 저렴. 무거운 건 liro(av-cam entry_pre, ~0.68에 완료)뿐.

## 3. 실험 결과 (각 fresh NAND 클론, icount, 게스트초)

| 런 | 변경 | T_osd | T_shoot | i_end | BOOT_CMP(DAC) | CamReady | 판정 |
|---|---|---|---|---|---|---|---|
| **ic0** | 순정(셸 계측) 기준 | 3.68 | 8.71 | 1.10 | 3.56 | 8.60 | 기준 |
| ic5 | 순정 cmdline, 셸無 | 3.80 | 8.69 | 1.14 | 3.64 | 8.60 | 계측오버헤드≈0 확인 |
| **ic3** | 주변부 IMDB 제거(hdmi/cec/nfc/wlan/gps/compass/geo/usbhost 8개) | 3.58 | **8.71** | 1.09 | 3.38 | 8.60 | **경로 단축 0** — 주변부는 이미 병렬(CPU3) |
| ic1 | Android 전체 제거(s_start_android→tmpfs 스텁) | — | **없음(정지)** | 0.90 | — | — | **UI 미도달**(55s까지 엔진 BOOT_CMP/CamReady 없음) |
| ic2 | Android 제거 + SyncAndroid(row100) IMDB 제거 | — | 없음(정지) | 0.91 | — | — | 여전히 정지 |
| ic4 | Android 제거 + 주변부/USB/SyncAndroid 24개 제거 | — | 없음(정지) | 0.84 | — | — | 여전히 정지 |

**핵심 발견**
1. **주변부 라이브러리 제거(ic3)는 촬영화면 도달을 못 줄인다** — 이미 i2-스레드(CPU3, tgt=4)에서 병렬 기동이라
   임계경로 밖. 단 **RAM/CPU 경합은 줄어든다**(호스트 부하 민감도: b1 벽시계 16.9s vs ic0 8.6s가 방증).
   → 이들은 *제거*가 아니라 *첫 프레임 이후로 지연(defer)* 하면 경합만 덜고 기능(parity)은 보존.
2. **Android를 통째로 들어내면 UI가 멈춘다(ic1/ic2/ic4)** — s_start_android.sh가 세우는 `/android/data`(vfat)
   마운트 + cgroup + Android init이 **카메라 엔진 UI 준비(BOOT_CMP)와 얽혀 있다**. SyncAndroid만 빼도 안 풀림 →
   단순 제거는 불가. **깨끗한 제거는 설정DB 복원 경로를 대체해야 한다**(트랙 B 네이티브 UI가 SD에서 자체 복원 — §4-2).
3. zygote 2.79s → BOOT_CMP 3.56s의 근접은 D구간의 Android 대기 성격을 뒷받침(klog: zygote 2.79, bootanim 재시도 2.85/3.55).

## 4. 커스텀 시스템 최적화 계획 (순위) — 소니 처리 보존

### ⭐ 1. 스냅샷/WBI 부팅 — 가장 큰 레버 [설계 전용]
- 순정이 이미 쓰는 고속 경로를 우리 시스템용으로: **부팅 완료된 상태를 NAND 스냅샷(WBI)으로 떠두고 전원ON시
  그 이미지를 DDR로 복원**(boot mode 11=SS) 하거나, 전원 유지 중이면 **HOT 재개**(boot mode 2, DDR 자기리프레시 유지).
- 효과: **S1+A+B+C+D 전체(≈6~7s)를 건너뛴다.** `RE_WARM_RESUME.md`: HOT 재개는 디바이스 resume 2.4ms + 태스크
  thaw로 "수초 내 촬영화면 복귀, uptime 연속, stage-1 없음, Linux 배너 없음". **스냅샷 자체가 초기화된 엔진 상태
  그대로**라 소니 처리 동일성은 정의상 보장(스냅샷=증명).
- 추정: 콜드 ~10s → **스냅샷 부팅 ~1~2s**. (순정 카메라가 체감상 빠른 이유가 바로 SS/HOT.)
- 에뮬 타당성: HOT 재개는 `warm-resume=on`으로 **이미 동작 확인**(RE_WARM_RESUME §7). WBI 생성 머신러리도
  fw에 존재(cmdline `cmpr.path=/system/sabin/ssboot.bin`, nflasha5 `WBI1`). 커스텀 WBI는 우리 부팅 후 생성.
- 필요: WBI 파티션 쓰기 + 부트로더 SS 분기 → **플래시 변경이라 설계 전용**(실기 착수는 CLAUDE.md §2 교체단계 조건 충족 후).

### 2. UI 레이어 교체(트랙 B 네이티브) → Android 미기동
- 네이티브 standalone UI면 zygote/system_server/launcher/mediaserver/servicemanager/vold/netd/installd를
  **아예 안 띄운다** → CPU3 병렬 부하·RAM(cgroup 113M) 제거.
- **단, 설정DB 복원을 대체해야 함**(§3-2 정지 원인). NATIVE_PORT_PLAN §3: 네이티브 UI가 **SD `ILCEREMO/`에서
  마지막 설정을 자체 재적용**(A2D 복원 대응) → SyncAndroid 의존 끊음.
- 경로 절감: D의 Android 대기분(1.1→2.8, **~1.7s [?]**) — 우리 UI가 첫 프레임을 DB복원에 안 걸면 회수.
- 에뮬 타당성: 네이티브 UI 자체가 트랙 B M0~M2 과제. 측정은 UI를 SDCom-only로 띄워 BOOT_CMP 없이 OSD 그리는 경로 A/B.

### 3. 2단계 부팅 통합 (stage-1 로더 제거) [설계 전용/부트로더]
- stage-1은 **av-cam.bin/Backup.bin/MUMIN 복사만을 위해 전체 Linux를 한 번 더 부팅**(~1.6s)하고 리셋한다.
  이 복사는 부트로더/보드가 직접 하면 두 번째 커널 부팅이 불필요.
- 효과: **~1.6s 절감.** 에뮬: 보드(`sony_a5000.c`)가 이미 preload 가능 → 단일 커널 경로 설계 가능.
- 실기는 부트로더 소관(업데이트 패키지에 없음) → **설계 전용**. HOT 재개는 이미 stage-1을 건너뜀(참고 패턴).

### 4. 비필수 서비스 defer (첫 프레임 이후로) — 기능 보존
- Wi-Fi(ath6kl+supplicant)·NFC·HDMI·CEC·USB 가젯·orb-server/WebApi·DLNA·오디오 ALM 등은 **CamReady 이후 기동**.
  ic3가 보이듯 제거해도 경로는 안 줄지만(이미 병렬), **지연 기동은 콜드부트 구간의 CPU/RAM 경합을 줄인다**
  (공유 4×A5에서 호스트부하 민감도↓). 기능은 전부 유지(parity) — 시작 시점만 늦춤.
- 구현: IMDB "on demand"(msg 0x940021) 경로 활용 — 해당 row를 i-스테이지에서 빼고 CamReady 핸들러에서 호출.

### 5. init 스크립트/커널 cmdline 다듬기 (소폭)
- s_start_android.sh의 mke2fs/dd(ram8/9)·chk_bkfile는 **bk.bak 없을 때(초기부팅)만** 실행 → 일반부팅은 이미 skip.
- 커널: 순정이 이미 `console=ttynull`(시리얼 스팸 0), klog 링만. **production에서 `console=ttyAM1` 쓰지 말 것**
  (UART 비용 + resume 데드락, RE_WARM_RESUME §7 "Known issue"). `lpj=` 고정으로 delay 캘리브 이미 skip.
- 효과: 수십~수백 ms. 우선순위 낮음.

### 6. 소니 처리 init 최적화 (출력 동일 보장 + A/B 증명) — 처리 보존하며 속도만
- **SA 결함테이블/ZIT 콜드잡 캐시**: av-cam이 콜드부트마다 계산하는 defect 테이블(idt_cam/sa_dfdet 결과,
  `RE_RAW_DEVELOP`/`RE_COLD_BOOT_MODE`)을 **초기화된 SA 상태 스냅샷**으로 재사용(매번 재계산 금지).
  증명: 캡처 동작 시 정지영상/결함보정 출력 **바이트 동일** 비교.
- **센서 YC 기동(F)과 UI 합성(E) 중첩 보장**: 현재도 OSD(3.6s)는 CamReady(8.6s) 전에 뜬다(E가 F에 안 막힘).
  F의 4.67s는 **에뮬 과장 의심[?]**(ISP 미모델 → YC 출력 무한대기 후 타임아웃 추정). 실기에서 센서 기동은
  통상 ~0.5~1s이므로 **실기 별도 측정 필요**. UI가 라이브뷰 프레임을 기다리지 않고 OSD/조작을 먼저 활성화하는
  순정 동작을 유지·강화.
- **HOT 재개 시 stage-1 복사 재실행 금지**: 이미 스킵됨(RE_WARM_RESUME). 설계시 유지.
- **엔진 init 재정렬/병렬화의 동일성 증명 방법론(필수)**: 모든 재정렬·캐시는 에뮬 A/B로
  (a) snsc 링의 엔진 이벤트 **시퀀스 동일**(Os/Oc/Og 오브젝트, PIN_CONNECT, SET_PANEL_OUT, CamReady),
  (b) 부팅 후 **SDCom 전 파라미터 덤프 동일**(`native/sdcom` sdprobe getparams),
  (c) 캡처 가능해지면 **캡처 프레임/정지영상 바이트 동일**. 하나라도 어긋나면 그 최적화는 폐기.

## 5. 목표 추정 (게스트 시간, 소니 처리 전부 보존)

| 구성 | 콜드부트 → 촬영화면(게스트, 추정) | 비고 |
|---|---|---|
| 순정(측정 기준) | **~10.3s**(에뮬; F 과장 포함[?]) | stage-1 1.6 + stage-2 8.7 |
| +네이티브 UI(Android 미기동)+서비스 defer | OSD ~2~3s, 촬영 = F 실측 의존 | Android 대기분 ~1.7s + 경합 회수 |
| +단일 커널(stage-1 통합)[설계] | 위에서 추가 **−1.6s** | 부트로더 작업 필요 |
| **+스냅샷/WBI 부팅**[설계] | **~1~2s** | 전 초기화 단계 스킵, 처리상태 스냅샷 보존 → 실질 목표 |

**권고 요약**: (1) 최종 목표는 **커스텀 WBI/HOT 스냅샷 부팅(~1~2s)** — 처리 동일성은 스냅샷으로 보장.
(2) 그 전까지의 실용 단축은 **네이티브 UI로 Android 미기동 + 비필수 서비스 defer + (부트로더) 단일 커널**.
(3) 소니 엔진 처리는 그대로 두되 **결함테이블 캐시·init 재정렬**만 동일성 A/B 증명하에 적용.
(4) 주변부(Wi-Fi/NFC/HDMI/오디오 등)는 **제거가 아니라 지연** — 기능 보존하며 경합만 감소.

## 6. 미확정 · 후속 [?]
- **F(라이브뷰 YC 4.67s)가 에뮬 과장인지 실기 특성인지 미확정** — ISP 모델 없는 에뮬의 타임아웃 가능성.
  실기 콜드부트 klog/snsc 덤프로 센서 YC 기동 실측 필요(현재 기기 전원꺼짐).
- Android 제거 시 정지(ic1/2/4)의 **정확한 게이트** — SyncAndroid 외에 `/android/data` 마운트/cgroup/Android init
  중 무엇이 엔진 BOOT_CMP를 막는지 미분리. 네이티브 UI 설계 전 1건 추가 실험 권장(`/android/data`만 살린 스텁).
- 커스텀 WBI **생성** 경로(우리 부팅 후 ssboot 이미지 생성)의 에뮬 재현은 미착수 — RE_WARM_RESUME은 **재개**만 검증.
- 호스트 부하로 벽시계가 2배 부풀어 **절대 게스트초도 에뮬 상한**임 — 실기 절대시간은 별도(순정 체감 ~1.4s대).
