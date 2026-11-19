# NATIVE_DEPLOY.md — 트랙 B 네이티브 UI 프로세스 배포·기동 메커니즘

> 역할: RE + 설계 문서(코드/커밋/펌웨어 수정 없음). 실기에 아무것도 쓰지 않음.
> 배경: CLAUDE.md §1(벽돌 위험), §2(트랙 B), §6(기기 워크플로); NOTES §8~13; TODO_TRACKB.md.
> 검증: 에뮬레이터(firmware 1.10, pristine NAND 사본, QEMU `sgc1m`)에서 관찰. `[?]` = 미확정.

---

## 0. 한 줄 결론
설치된 APK(`/android` chroot, API10, uid=app)가 **OpenMemories 백도어**(= Tweak의 `libtweak.so` `Shell.nativeExec`)와 동일한 경로로 **네이티브 루트 셸 명령 1개**를 쏴서, APK 안에 담아 배포한 네이티브 바이너리를 **chroot 밖 tmpfs `/android/a5k/`로 복사→exec** 한다. 이 바이너리는 **네이티브(비-Android) 쪽에서 루트로** 돌면서 `/dev/input/event0`·`/usr/lib/libsdcom.so`·osal_uipc에 직접 접근한다. 배포는 `pmca-console install -f <apk>` USB 앱 설치 그대로. **CLAUDE.md §1 기준 벽돌 위험 없음**(플래시 비-기록, 재부팅 완전 복구). 단 Rigel 화면 출력 라이브러리는 chroot 안(bionic) 전용이라 네이티브(glibc) 프로세스에서 링크 불가 → 그 1건은 별도 미해결 과제로 남는다(§7).

---

## 1. 핵심 메커니즘: APK → 네이티브 루트 실행 (OpenMemories 백도어)

### 1.1 증거 — `Shell.nativeExec`가 chroot 밖에서 실행된다
`tools/OpenMemories-Tweak/app/src/main/java/.../Shell.java`:
- `nativeExec(cmd)` → JNI `libtweak.so` → 명령 문자열을 **그대로 네이티브 루트 셸에 전달**.
- `execAndroid(cmd)`는 그 위에 `/bin/busybox chroot /android <cmd>`를 **덧씌워야** Android 안에서 실행됨.
- 즉 **nativeExec의 기본 실행 위치 = chroot 밖 네이티브 리눅스**. 실제로 Tweak은 telnetd를 `Shell.exec("busybox telnetd -l sh")`(네이티브)로, adbd는 `execAndroid(...)`(chroot 안)로 띄운다 → telnet 루트가 네이티브 `/setting`을 보는 이유(CLAUDE.md §6).

### 1.2 증거 — libtweak.so 정적 분석 (에뮬레이터에서 pull, md5 `bb7dbb34…`)
`Java_...Shell_nativeExec` → `shell_exec_async` → 네이티브 심볼 `osal_valloc_msg_wait` / `osal_snd_msg` / `osal_snd_sync_msg` / `osal_free_msg` (NEEDED: `libosal_uipc.so`). 즉 명령 문자열을 **osal_uipc 커널 메시지 큐**에 실어 **네이티브 쪽 Sony 디버그/로그 커맨드 서비스**로 보내고, 그 서비스가 루트로 셸 실행한다. (NOTES §10.4가 서술한 osal_uipc 경로와 동일 계열. 정확한 수신 큐 ID는 서비스측이라 본 분석 범위 밖 `[?]` — 메커니즘 성립에는 불필요.)
- 그 밖 import: `dlopen/dlsym`, `open/read/close`, `mmap`, `/dev/mem` 문자열(백업 prop용, 본 배포 경로와 무관).

### 1.3 이 백도어가 트랙 B에 주는 정확한 프리미티브
- 백도어 서비스는 **네이티브 루트 + 네이티브 마운트 네임스페이스**에서 돈다.
- chroot 안 앱이 `/data/data/<pkg>/lib/...`에 쓴 파일은 네이티브에서 `/android/data/data/<pkg>/lib/...`로 보인다(같은 nflasha17 vfat, chroot가 `/android/data`에 마운트).
- 따라서 **단 한 줄**로: 앱 번들 바이너리를 tmpfs로 복사 → chmod → chroot 밖에서 exec.

---

## 2. 에뮬레이터 실측 (이 세션, 2026-11-15)

QEMU `sgc1m`, pristine NAND 사본, `A5K_SHELL`(UART2 ttyAM1 루트 셸 = **백도어와 동일한 네이티브 루트·네임스페이스**로 관찰). HMP/USB 포트 46480–46482.

| 관찰 | 명령 | 결과 |
|---|---|---|
| 네이티브 측 프로세스/uid | `ps -eo pid,user,args` | im.elf(root), zygote/system_server/scalaralauncher 등은 chroot 안. **백도어 영역 = 네이티브 루트** |
| event0 존재(네이티브) | `ls -l /dev/input/` | `event0/1/2` 전부 존재(13,64·65·66) |
| **네이티브 바이너리 exec(outside chroot)** | `/android/a5k/deploytest keys 2` (cwd `/`) | `device: D keyboard / reading 2 s` → `exit=0`. **chroot 밖에서 ARM 바이너리 정상 실행 + event0 열림** |
| 실행 위치가 chroot 밖임 | 같은 셸에서 `/usr/lib/libsdcom.so`, `/usr/bin/bk.elf` 보임, `/proc/self/root`=네이티브 루트 | 확인(Android `/system`이 아니라 네이티브 `/usr`) |
| glibc 런타임(네이티브) | `ls -l /lib/ld-linux.so.3` | `→ ld-2.11.2.so`, `/usr/lib/libosal_uipc.so` 존재 → **glibc 동적 바이너리 네이티브 실행 가능** |
| APK 번들 .so가 네이티브에서 보임 | `ls -l /android/data/data/com.github.ma1co.../lib/` | `libtweak.so/libadbd.so/libprotectiontweak.so`(uid 1000). 앱 설치 시 lib이 여기로 추출됨 |
| `/android/data`(nflasha17 vfat) noexec 아님 | `cp … /android/data/dt; /dt keys 1` | `VFAT-DATA-EXEC-OK` (단, 그래도 §3처럼 tmpfs 권장) |
| **Rigel 클라 라이브러리는 chroot 안(bionic)** | `ls /system/lib/.../gralloc.diadem.so`(네이티브) | **없음**; `/android/system/lib/`에만(hw/) 존재 → 네이티브 glibc 프로세스는 링크 불가(§6, §7) |

`deploytest` = 프로젝트 기존 `app/native/probe/PROBE`(static ARM, event0 읽기). a5kshell `push`로 `/android/a5k/`에 올려 실행. 이 셸은 관찰용(배포 경로 아님)이지만, **실행 주체의 권한·네임스페이스가 백도어와 동일**하므로 §1 프리미티브를 그대로 입증한다.

USB 앱 설치 자체는 `emulator/docs/RE_USB_APP_INSTALL.md`에서 이미 end-to-end 성공(= `install -f <apk>` → Application List 등재 → 기동). 본 문서는 그 위에 "설치된 APK가 네이티브 프로세스를 띄운다"를 추가 검증.

---

## 3. 전달·기동·종료 설계 (권장안)

### 3.1 전달: 바이너리를 APK에 번들
- 네이티브 UI 바이너리를 **APK의 `lib/armeabi/lib<name>.so`** 로 넣는다(파일명을 `lib*.so`로 해야 설치 시 네이티브 lib 디렉터리로 자동 추출·exec 비트 유지됨 — adbd가 이 방식: `applicationInfo.nativeLibraryDir + "/libadbd.so"`).
- 설치 후 네이티브 경로: `/android/data/data/<pkg>/lib/lib<name>.so` (또는 `.../app-lib/...`, 기기별). 실측에서 `/android/data/data/<pkg>/lib/`에 추출됨.
- APK 하나에 **런처 Activity(Java) + 네이티브 바이너리** 동봉 → `pmca-console install -f` 한 번으로 둘 다 배포.

### 3.2 기동(start) — 런처 Activity가 백도어로 한 줄
```
# nativeExec 로 네이티브 루트에서 실행되는 "한 줄":
mkdir -p /android/a5k && \
cp /android/data/data/<pkg>/lib/lib<name>.so /android/a5k/a5kui && \
chmod 755 /android/a5k/a5kui && \
/android/a5k/a5kui > /android/a5k/a5kui.log 2>&1 & \
echo $! > /android/a5k/a5kui.pid
```
- **왜 tmpfs `/android/a5k`로 복사**: CLAUDE.md §1 규칙 1(플래시 비-기록). `/android`는 `s_start_android.sh`가 `mount -t tmpfs`로 만든 RAM. `/data`(nflasha17)는 exec 가능하지만 NAND이므로 거기서 직접 돌리지 않는다(SD=noexec, `/tmp`=NAND 쓰기금지와 동일 원칙).
- APK 자체(uid=app)는 `/android/a5k`에 쓸 권한이 없을 수 있으나, **복사·chmod·exec 전부 백도어(루트)가 수행**하므로 문제 없음.
- 경로는 **네이티브 관점**으로 쓴다(`/android/...`). nativeExec는 chroot 밖에서 돌기 때문.

### 3.3 종료(stop) — 앱 종료/크래시 모두에서 정리
런처 Activity의 `onDestroy`/MENU-exit/`onPause`에서 백도어로:
```
P=$(cat /android/a5k/a5kui.pid 2>/dev/null); \
[ -n "$P" ] && kill -TERM $P; \     # SIGTERM → 바이너리가 SDCom disconnect 후 정상 종료
rm -f /android/a5k/a5kui.pid
```
- `kill -TERM`을 쓰는 이유: 네이티브 바이너리가 **SIGTERM 핸들러에서 SDCom `disconnect()` 후 종료**(CLAUDE.md §1 규칙 3). `kill -9` 금지(osal 큐/SDCom 미정리 → NOTES §10.9형 카메라 다운 위험).
- 멱등성: pid 파일 없으면 `ps|grep a5kui`로 대체 탐색(Tweak의 `Procfs.findProcess` 패턴과 동일).

### 3.4 크래시 안전성(바이너리 측 필수 — CLAUDE.md §1 규칙 3)
`app/native/sdcom/sdprobe.cpp`가 이미 모범. 그대로 적용:
- `SIGSEGV/SIGABRT/SIGBUS/SIGTERM/SIGINT/SIGHUP` 핸들러 → `g_ipc->disconnect()` 후 `SIG_DFL` 재발생.
- libsdcom 객체 크기 정확히(IntParam 0x10 / IPC 0x44 / Msg 0x48) — `sizeof` 정적 검증(§1 규칙 2, NOTES §10.9).
- `SD_DRYRUN=1` 미연결 단독 실행 먼저(§1 규칙 4).
- 종료 시 `/android/a5k/a5kui.pid` 정리는 §3.3의 런처가 담당(바이너리는 자기 리소스만 정리).

---

## 4. 입력(키)·전면 상태: 런처 Activity가 ABT 통지 유지
- `/dev/input/event0`의 **전 키 수신**은 전면 Android 앱이 `AppInfoReceive`로 ABT에 통지해야 열린다(NOTES §10.4/10.5; 트랙 A `BaseActivity.notifyAppInfo` = `com.android.server.DAConnectionManagerService.AppInfoReceive` 브로드캐스트). ABT 미통지면 휠 계열만 온다.
- 따라서 **권장 구성**: 런처 Activity를 **전면 유지**하여 (1) `AppInfoReceive`로 키 라우팅을 열고, (2) `DAConnectionManagerService.apo`(`APO/NO`)로 자동전원off를 막는다. 네이티브 프로세스는 event0을 **병렬로 읽는다**(NOTES §10.3: event0은 grab 아님 → 동시 읽기 가능).
- MENU(514)를 "앱 종료"로 쓰면 Activity `onDestroy`가 §3.3 stop을 호출 → 네이티브 프로세스도 함께 종료.
- `[?]` 네이티브 프로세스가 ABT_set_state를 **스스로** 호출(libarbiter_proxy.so, glibc에서 호출 가능)해 런처 전면 의존을 없앨 수 있는지 — ABT payload의 pkg/cls가 Android 앱을 식별하므로 런처 전면 유지 쪽이 검증된 안전안. 추후 실기 확인 대상.
- `[?]` 전면 유지에 따른 메모리(zygote/앱 상주) 비용 — 최종 standalone 단계에서 Java 제거 시 해소(NOTES §11.2). 현 단계는 런처 상주가 가장 안전.

---

## 5. 툴체인 결정 — **glibc(`app/native/toolchain`, Makefile.cross)**

| 항목 | glibc (toolchain) | bionic (toolchain-ndk) |
|---|---|---|
| 실행 위치 | **chroot 밖 네이티브** (`/lib/ld-linux.so.3`=glibc 2.11.2, 실측 존재) | chroot 안 필수(`/system/bin/linker` + `LD_LIBRARY_PATH=/system/lib`) |
| libsdcom / osal_uipc | **네이티브 `/usr/lib`에 존재(직접 링크)**. sdprobe 실기 성공(NOTES §10.7) | 네이티브 쪽엔 없음 |
| event0 | 네이티브에서 직접(실측) | 가능하나 chroot 내부 |
| Rigel 클라(`gralloc.diadem.so`/`rgl*`) | **불가**(이 .so가 chroot 안 bionic 전용) | 가능(단 chroot 안) |

**결론**: 과제 요구("chroot 밖 네이티브 측에서 libsdcom/event0 접근")는 **glibc 전용으로만 성립**. bionic 바이너리는 네이티브 리눅스에서 안 뜬다(sftest가 `chroot /android`를 요구한 이유, NOTES §11.4). 제어(SDCom)·입력(event0)·수명(osal) 모두 glibc 네이티브에서 실증됨 → **네이티브 UI 바이너리 = glibc, Makefile.cross.**

**단 §6의 Rigel 화면 출력은 이 결정과 충돌**한다(아래).

---

## 6. 해소 안 된 긴장: Rigel 화면 출력 vs 네이티브 glibc
- 화면 합성 최상위 = im.elf 안 **Rigel Window Server**. 재사용 가능한 클라 API(`rglCreateWindow/rglSwapBuffers` 등)는 **`gralloc.diadem.so` / `libdihal.so`(= chroot 안 `/android/system/lib`, bionic)** 에 들어있다(NOTES §11.5~11.7). 실측: 이 .so들은 네이티브 `/system/lib`에 **없음**.
- 즉 **같은 프로세스에서 glibc(libsdcom/event0) + bionic(rgl*) 동시 링크 불가.** 선택지:
  1. **네이티브 glibc 프로세스가 Rigel 프로토콜을 osal_uipc로 직접 구현**(큐 `UX_DEBUG_CLIENT 0x014b04b4`, 메시지 0xC0B 레이아웃 — NOTES §11.6/11.7). 라이브러리 링크 없이 큐로만. ← 트랙 B 지향과 일치하지만 **미구현/미검증**(기존 5대 미확정 중 ①Rigel 창 합성).
  2. UI를 chroot **안**(bionic)에 두고 SDCom은 chroot 안에서 접근(sdprobe가 네이티브였지만 osal 큐는 커널 전역이라 chroot 안에서도 접근 가능 여부 `[?]`). 이 경우 "네이티브 측 실행" 요구와 어긋남.
- 본 문서 범위(배포·기동·입력·제어·수명)에서는 **glibc 네이티브 + 선택지 1**을 권장하고, Rigel 창 합성은 **기존 미확정 ①과 묶어 별도 RE**로 남긴다. `[?]`
- 참고: 이 긴장은 "배포 메커니즘"의 결함이 아니다. 배포·기동·event0·SDCom·수명은 전부 glibc 네이티브로 검증됨. 미해결은 **화면 출력 링크 경로** 하나.

---

## 7. 벽돌 위험 평가 (CLAUDE.md §1 대조)

| 요소 | 지속성 | 위험 | 근거 |
|---|---|---|---|
| `pmca-console install -f` (APK → `/data/app`, dalvik-cache, packages.xml) | nflasha17(Android data) | **없음** | 일반 앱 설치(설치/제거 가능). RE_USB_APP_INSTALL 검증. CLAUDE §1 표의 "Android 앱" 행 |
| 백도어 1-라인(cp/chmod/exec/kill) | RAM tmpfs `/android/a5k` | **없음** | 플래시 비-기록, 재부팅 시 tmpfs 소멸(§1 규칙 1) |
| 네이티브 바이너리 실행(event0 읽기·SDCom) | 휘발 | **없음~낮음** | event0=읽기. SDCom=휘발 엔진상태, 재부팅 복구(CLAUDE §1 SDCom 행, NOTES §10.7/10.10) |
| 크래시 시 정리 | — | **낮음(규칙 준수 시)** | SIG 핸들러 disconnect·객체 크기 정확·kill -9 금지(§3.3/3.4). 미준수 시 NOTES §10.9형 다운(벽돌 아님, 물리 재부팅) |
| 플래시/`/usr`/`/system`/Backup.bin | — | — | **이 경로는 전혀 건드리지 않음** |

**종합: 벽돌 위험 0.** 모든 쓰기는 (a) 일반 앱 설치 영역 또는 (b) RAM tmpfs. `/usr`·`/system`·NAND·Backup.bin raw·Bkup_Write 미접촉. 최악의 사고(SDCom 미정리 다운)도 물리 재부팅으로 복구(원격 복구 불가라는 운영 불편일 뿐, 벽돌 아님).

---

## 8. 에뮬레이터 수용 테스트 계획 (회귀용)

전제: `A5K_NAND`=pristine 사본, `A5K_QEMU`=`qemu-deploy`, `-M usb-socket=...`, 포트 46480–46489. PMCA 패치본 `<scratchpad>/usbdev/pmca/b`, venv `…/usbdev/venv`, `A5K_USB=127.0.0.1:<port>`. 관찰은 `A5K_SHELL` + `tools/a5kshell.py`(배포 경로 아님, 검증 보조).

1. **부팅** → Android up(`ps`에 system_server) 확인.
2. **USB 설치**: `pmca-console install -d socket -f <launcher.apk>` → `Task completed successfully`, `/android/data/app/<pkg>-*.apk` 생성, lib 추출(`/android/data/data/<pkg>/lib/lib<name>.so`).
3. **런처 기동**: MENU → 애플리케이션 → 애플리케이션 목록 → 런처 선택(또는 `am start`). 런처가 백도어로 §3.2 start 1-라인 전송.
4. **네이티브 프로세스 chroot 밖 실행 확인**(a5kshell 관찰): `ps -eo pid,user,args`에 `/android/a5k/a5kui`(root), `/android/a5k/a5kui.pid` 존재, `/proc/<pid>/root`가 네이티브 루트(= chroot 밖).
5. **event0 열림**: 바이너리 로그(`/android/a5k/a5kui.log`)에 "D keyboard" 열림 기록; HMP `sendkey m 300` 주입 시 이벤트 수신 로그.
6. **SDCom 연결**: 로그에 `connect=0`, GetBodyFirmwareVersion/GetSystemState 응답(NOTES §10.7 동일). (Scalar 모드 진입 조건 `[?]` — 앱 목록 진입으로 트리거되는지 실측.)
7. **앱 종료 → 정리**: 런처 종료(MENU) → §3.3 stop → `ps`에서 `a5kui` 사라짐, pid 파일 제거, SDCom disconnect 로그.
8. **크래시 안전**(옵션): 바이너리에 의도적 SIGSEGV 주입판으로 핸들러 disconnect 후 종료·카메라 생존 확인.
9. **재부팅 복구**: 재부팅 후 `/android/a5k` 비어있음(tmpfs 소멸), APK는 `/data`에 잔존(재기동 가능) — 벽돌 없음 확인.

합격 기준: 1–7 전부 통과 = "USB로 APK 설치 → 앱 목록에서 기동 → chroot 밖 네이티브 프로세스 실행 → SDCom·event0 열림 → 앱 종료가 프로세스 정지".

---

## 9. 미확정 항목 `[?]`
- 백도어 수신 큐의 정확한 osal ID/서비스명(메커니즘 성립엔 불필요, nativeExec 재사용으로 우회).
- 네이티브 프로세스가 ABT를 스스로 통지해 런처 전면 의존을 없앨 수 있는지(§4).
- Scalar 모드(SDCom 접속 허용 상태 5/9) 진입 트리거 — 앱 목록 진입 여부(NOTES §8.2).
- **Rigel 화면 출력**: glibc 네이티브에서 `UX_DEBUG_CLIENT` 큐로 직접 합성(§6, 기존 미확정 ①) — 본 배포 설계의 유일한 큰 공백.
- 실기 lib 추출 경로가 `/data/data/<pkg>/lib` vs `/data/app-lib/<pkg>-*` 중 어느 쪽인지(기기별) — 런처가 둘 다 탐색하도록.
