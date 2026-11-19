# 트랙 B 런타임 인터페이스 — 에뮬레이터 동적 검증 (Rigel 창 · 입력/ABT · SDCom · 프리뷰)

> 작성 2026-11-15, RE 전용(코드 커밋·펌웨어 수정·실기 접촉 없음). 전부 **에뮬레이터**(`emulator/`, QEMU `sony-a5000`,
> 순정 펌웨어 1.10 무수정, 사용자 NAND 클론)에서 관찰. 실기 재확인 전까지 "에뮬 확정"으로 취급.
> 배경: CLAUDE.md §2/§5, `re/NOTES.md` §8·§10.3–10.8·§11.5–11.7, `re/static/trackb_re_plan.md`(정적 RE 통합본),
> `app/docs/NATIVE_DEPLOY.md`, `app/docs/NATIVE_PORT_PLAN.md`.
> 표기: **[V]** = 에뮬에서 동작 관찰, **[S]** = 정적(디컴파일/디스어셈블) 근거, **[?]** = 미확정.
> IDA 주소는 `re/ida/*.c` 기준(libObj.so 로드 베이스 0), 런타임 = `/proc/147/maps`의 libObj 베이스 + 주소.

---

## 0. 한눈 요약

| # | 질문 | 결론 |
|---|---|---|
| 1 | 네이티브 프로세스가 Rigel 창을 띄울 수 있나 | **예 [V]**. bionic 바이너리를 root로 `busybox chroot /android` 안에서 실행 → `gralloc.diadem.so`/`libdihal.so` dlopen → `ws_hal_alloc`×2 → `registerBuffers` → `rglCreateWindow("UX_DEBUG_CLIENT",640,480,0,0,2)` → `rglShowWindow` → **`rglSetWindowParam`으로 z-order 0(최상단)** → `rglSwapBuffers`. 패널에 사각형+텍스트 표시 성공(§1.6 스크린덤프). 필수 조건 2개: **(a) 버퍼 usage에 `0x100`(HW_TEXTURE) 비트**, **(b) Rigel 메인 서피스가 붙은 상태 = 카메라가 "Scalar(앱) 모드"**. Diadem(순정 촬영) 모드에선 합성기가 아예 돌지 않음. |
| 2 | 입력 | event0 EV_KEY 실측 [V]: Diadem 모드 = **0건**, 앱 모드(런처 전면, APP_INFO 있음) = 휠·휴지통·줌·S1·**MENU 514** 수신. **휴지통 = 595** 확정. ABT 메시지를 glibc `libosal_uipc`로 직접 송신 가능 [V]. 네이티브 APP_INFO 단독 효과는 미분리 [?]. |
| 3 | SDCom이 ILCE Remote 실행 중에만 되던 이유 | ScalarDaemon은 **D2A 이후(state 5)** 에만 접속 허용 [V]: Diadem `connect=-5`, App List 진입 후 `connect=0`. **네이티브 프로세스가 arbiter에 `ABT cmd=2 (SET_A)`를 직접 보내면 Android 앱 없이도 SDCom이 열림 [V]** (단 순정 상태머신이 어긋남 — §3.3 주의). |
| 4 | 프리뷰 | 라이브뷰 = VPH **YUV 레이어(AIF ch51)**, OSD(ch53) 아래 [V]. SDCom `OpenCamera 0x1000`(무파라미터) → `CameraStartPreview 0x1004` 결과코드 0 [V], `GetVideoLayout 0x0604` = (0,0,640,480) [V]. 픽셀은 클라이언트로 오지 않음(하드웨어 평면). |

**정정(기존 문서 대비)**
- `trackb_re_plan.md` §1 "Z순서: tail append → 우리 창이 위" **오류**. 합성 루프 `sub_67A7BC`는 리스트를 **tail→head** 순으로 그린다(`sub_66FF7C`=list+4=tail, `sub_66FC7C`=node+4=prev) → **나중에 추가된 창이 맨 아래**. 위로 올리려면 `RglWindowParam+16`(order)=0 으로 `rglSetWindowParam` [V].
- `NATIVE_DEPLOY.md` §6 "Rigel 라이브러리는 bionic 전용이라 네이티브 프로세스 불가" → **root bionic 프로세스(chroot /android)로 해결 [V]**. glibc 쪽은 SDCom/event0/osal용 헬퍼로 분리하면 됨(둘 다 root 백도어로 실행 가능).
- "Rigel 클라는 `WindowClientWrapper::Connect`(0x014b0455) 선행 필요?" → **불필요 [V]** (rhello는 호출 안 함).

---

## 1. Rigel UX_DEBUG_CLIENT 창 (화면 출력)

### 1.1 빌드/실행 환경 (재현용)
- 도커 없이 macOS에서 빌드 가능(스크래치 `re-rigel/src/`):
  - **bionic(Android 2.3)**: `arm-none-eabi-gcc -march=armv7-a -mthumb -mfloat-abi=softfp -mfpu=vfpv3-d16 -fno-short-enums -ffreestanding -fno-pic -c` + 자체 `crt`(`_start: mov r0,sp; mov r1,#0; ldr r2,=main; adr r3,structors; b __libc_init`, structors 4개 = `{-1,0}`) + `arm-none-eabi-ld --dynamic-linker=/system/bin/linker --hash-style=sysv` + `app/native/sysroot-android/lib/{libc,libdl}.so`. 헤더 대신 선언 직접(bionic `signal`=`bsd_signal`, `RTLD_NOW`=0).
  - **glibc(카메라 /usr/lib)**: `zig cc|c++ -target arm-linux-gnueabi.2.17 -mcpu=cortex_a5` (심볼 버전 GLIBC_2.4만 생성되도록 `clock_gettime` 대신 `gettimeofday`), C++은 `-nostdlib++ -fno-exceptions -fno-rtti -fno-sized-deallocation` + sysroot `libstdc++.so.6`/`libgcc_s.so.1`, `-Wl,--allow-shlib-undefined`. **libsdcom은 NEEDED에 libosal_uipc가 없음 → `-Wl,--no-as-needed -losal_uipc` 필수**(안 하면 `undefined symbol: osal_dynamic_id`).
- 실행: 네이티브 root 셸에서 `busybox chroot /android /system/bin/sh -c "LD_LIBRARY_PATH=/system/lib /a5k/rhello ..."` (바이너리는 tmpfs `/android/a5k/`).

### 1.2 최소 동작 시퀀스 (rhello, [V])
```c
typedef struct { int w, h, fmt, rsv, slot; } WSImageBuffer;      // 20 B, slot = BufferManager 슬롯(0..31)
hd = dlopen("/system/lib/libdihal.so", RTLD_NOW);                 // bionic dlsym은 핸들 라이브러리만 검색 →
hg = dlopen("/system/lib/hw/gralloc.diadem.so", RTLD_NOW);        //   두 lib을 각각 dlopen
WSImageBuffer b[2]; int stride;
for i: ws_hal_alloc(640, 480, /*fmt*/1 /*RGBA8888*/, /*usage*/0x133, &b[i], &stride);   // → slot 6,7, stride 640(px)
for i: ws_hal_lock(&b[i], 0x133, 0,0,640,480, &va); 그리기; ws_hal_unlock(&b[i]);
_Z15registerBuffersP13WSImageBuffer(b);                            // 전역 dword_F530 ← CreateNativeWindow가 msg+0x68로 복사
win = rglCreateWindow(NULL, "UX_DEBUG_CLIENT", 640, 480, 0, 0, 2); // type 0(create) 동기 송신
rglShowWindow(win, 1);                                              // type 1, status=1
rglGetWindowParam(win, p); p[16] = 0; rglSetWindowParam(win, p);    // order=0 → 리스트 head = 최상단
loop { 백버퍼 lock/그리기/unlock; rglSwapBuffers(win, front, 0,0,640,480); }   // type 6(post)
rglDestroyWindow(win); ws_hal_free(&b[1]); ws_hal_free(&b[0]);     // 종료/시그널 시 반드시
```
- 반환값 관찰: alloc/free 0, create = 포인터, show/set/swap = 1, getParam 바이트 `01000000 0000 0000 8002 e001 8002 e001 01.. 0000803f 0000803f`(status, x,y, w,h, sw,sh, order, scale 1.0f×2).
- `rglDI_flushVDF`(type 8)는 불필요(Rigel 출력엔 영향 없음, 서버측 `dmpNativeSendWedgeCommands`). `rglWaitUpdate`(type 33)는 렌더 후 응답 대기 — 쓰지 않아도 됨.

### 1.3 필수 조건 (실패 원인 추적으로 확정)
1. **usage 비트 `0x100`(GRALLOC_USAGE_HW_TEXTURE)** [V][S]. 서버는 창 버퍼를 텍스처로 등록할 때(`sub_6789E8`) `ws_hal_lock(buf, 0x100, …)`을 부르고, libdihal `ws_hal_lock`은 `(slot.usage & 0x1F00)!=0 || (lock_usage & 0x1F00)==0` 일 때만 성공. usage 0x33으로 할당하면 서버 lock 실패 → 텍스처 id 0 → **창은 리스트에 있지만 아무것도 안 그려짐**(pmem으로 `tex=0,0` 관찰). 0x133으로 바꾸자 `tex=4,5` 등록·표시.
2. **Rigel 메인 서피스 부착 = 앱(Scalar) 모드** [V]. 렌더 함수 `sub_67A7BC`는 `ctx(=WS+88)+8`(EGL surface)이 0이면 통째로 건너뜀(`sub_679A24`). 관찰:
   - 순정 촬영 화면(Diadem): `ctx+8 = 0`, 패널 OSD = 순정 GRM 버퍼(PPF 경유 `0x8F758000/0x8F7EE000`, RGBA4444). 클라이언트 창은 리스트에 들어가도(응답 정상) 절대 보이지 않음.
   - 메뉴 → 애플리케이션 → 애플리케이션 목록 진입 후: `ctx+8 = 0x00057c10` → 합성 동작, 우리 창 표시.
   - 메인 서피스 attach/detach(type 64/65, `sub_67A324/67A2C8`)는 `getpid()==msg+0`(= 서버 자신) 검사 → **외부 클라이언트가 직접 붙일 수 없음** [S].
3. 큐/채널: 클라이언트 Communicator = 자기 수신 "`UX_DEBUG_CLIENT_0`"(prefix → 0x014b04b4) + 서버 "`UX_WINDOW_SYSTEM`"(0x014b04b3). 런타임에 다른 점유자 없었음 [V](리스트에 SF 창 `DI_ANDROID_APP` 하나뿐).

### 1.4 서버 쪽 사실 (pmem 실측 + 디컴파일)
- WindowServer 전역 = `dword_EF5A34`(IDA) → 런타임 `libObj베이스+0xEF5A34`. 필드: `+38/+40` 640×480, `+36` 더블버퍼 토글, `+68/+72` 창 리스트 head/tail, `+76` 재정렬 플래그, `+78` 렌더 요청 플래그, `+88` 렌더 ctx(vtbl `0xE5C450`, `+8` surface).
- 리스트 노드(0x24 B): `+0 next, +4 prev, +12 → 서버측 창 상태(WindowMessage 192 B 복사본), +16/+20 텍스처 id(버퍼 0/1), +24 front 선택`.
- 메시지 디스패처 `sub_67B8FC`: type 0 add(`sub_67B0F8`, tail append, order=count−1), 1 show/param(`sub_67B33C`: `+88` 재정렬 시 `sub_67B2E6`로 order 위치에 재삽입 → `sub_67B3E4`가 head부터 0,1,2… 재번호), 2 getParam, 3 resize, 4/5 Show/HideScreen, 6 post(dirty rect + front idx), 32 destroy, 33 waitUpdate, 64/65 attach/detach(서버 pid 전용), 128/130 무시, 133/134 서버 콜백, 그 외 → vtbl+48(`sub_67A0DC`: 7 setId, 8 flushVDF, 135 OSD gain).
- **렌더 트리거**(`+78`): type 1(status≠0), type 4/5, type 0/3/6이면서 그 창 status≠0(`sub_67B8B4`), type 33. 즉 **post(type 6) 한 번이 곧 렌더 요청** — 별도 flush 불필요.
- 렌더 `sub_67A7BC`: tail→head 순회, 창마다 `status(+8)!=0`이면 버퍼 텍스처 등록(최초 1회), 색 = `(f28·f32, f28·f32, f28·f32, f28)`(f28 = `msg+28` 불투명도, f32 = `msg+32` 밝기, 기본 1.0), `u16 msg+36` = 블렌드 모드 인자(`sub_6793D6`) [?], 목적 rect = 창 (x,y,sw,sh).
- WindowMessage(192 B) 보강: `+0` (서버 전용 pid), `+4 type`, `+8 status`, `+12/14 x,y`, `+16/18 w,h`, `+20/22 scaled w,h`, `+24 order(u8)`, `+28 f32 opacity`, `+32 f32 brightness`, `+36 u16 blend`, `+44 name[32]`, `+80..86 dirty`, `+88 reorder(u8, 서버 계산)`, `+89 front`, `+92/+96 a5/a6`, `+100 nbuf`, `+104 WSImageBuffer×2`, `+144 client type(0=전체 복사,1=type/status만)`, `+148`, `+164 응답 결과(0=OK)`.
- **RglWindowParam(0x50 B) = WindowMessage+8..+0x58**: `+0 status, +4 x,y, +8 w,h, +12 sw,sh, +16 order, +20 opacity f32, +24 brightness f32, +28 blend u16, …`. `rglSetWindowParam`은 status를 덮어쓰지 않음(현재값 유지) → type 1 송신.

### 1.5 픽셀 형식 / 알파 [V]
- 메모리 바이트 순서 **R,G,B,A**(GL_RGBA/UNSIGNED_BYTE; 서버 `glTexImage2D(…,0x1908,0x1401)`). u32 LE 리터럴로는 `0xAABBGGRR`. stride = 640 px.
- alpha 0 픽셀 → 아래 창이 비침(픽셀 단위 블렌딩 동작). alpha 0x80 빨강(`0x800000FF`)이 검정 배경 위에서 꽉 찬 빨강으로 보임 → **premultiplied(ONE, 1−SRC_ALPHA) 블렌드 추정 [?]** → 반투명은 RGB도 미리 곱해서 써야 안전.
- 매 post마다 내용 갱신 확인(FRAME 카운터 증가) [V] → 서버가 텍스처를 버퍼에 직접 바인딩(복사본 1회가 아님).

### 1.6 hello Rigel 증거
- `app/docs/img/trackb_hello_rigel.png` — App List(Android 런처) 위에 네이티브 창 최상단 합성(투명 영역으로 런처가 비침).
- `app/docs/img/trackb_hello_rigel_scalar_noapp.png` — Android 앱 없이 Scalar 모드(§3.3 상태)에서 네이티브 창만 표시.
- 원본 스크린덤프: 스크래치 `re-rigel/r3/top1.png`, `re-rigel/r3/blackmode.png`. 서버 상태 덤프 예(pmem):
```
list head=000c71f8 tail=0002a948
[0] tex=6,7  status=1 order=0 name='UX_DEBUG_CLIENT' buf0={640,480,1,0,6} buf1={640,480,1,0,7}
[1] tex=2,3  status=1 order=1 name='DI_ANDROID_APP'  buf0={640,480,1,…,0} buf1={…,1}
```

### 1.7 남은 것 [?]
- 블렌드 모드 `+36`/불투명도 `+28` 값별 효과, premultiplied 여부(테스트 패턴 1회로 확정 가능).
- 슬롯 재사용: 서버는 슬롯별 vaddr 캐시(`sub_6D26D0`)를 둠 → 클라이언트가 free 후 같은 슬롯을 다른 물리주소로 재할당하면 stale 매핑 위험 [?]. 프로세스 수명 동안 버퍼 2개를 고정 보유 권장.
- 다른 크기/위치(부분 창, 853×480 논리 해상도 압축)는 미시험.
- 패널 EVF/HDMI 출력 경로(line OSD ch54)는 미확인.

---

## 2. 입력: `/dev/input/event0` + ABT

### 2.1 실측 (keyprobe `cap`, HMP `sendkey <k> 300`) [V]
| 상태 | 수신 |
|---|---|
| Diadem(순정 촬영 화면) | **아무 키도 안 옴** (휠/OK/휴지통/MENU/재생/줌/S1 전부 순정 UI가 처리) |
| App List(Android 런처 전면, 런처가 AppInfoReceive 통지함) | ←105 →106 ↑103 ↓108, **휴지통 595**, 줌 611/610(반복 press+release, 정지 645), S1 = 516+517 동시, 놓을 때 516/517 해제 + `530 value=0` |
| §3.3 상태(외부 SET_A로 들어간 Scalar 모드, Android 앱 없음) | 0건 (ABT cmd 3 송신 후에도 0건) |
- 휴지통 키 = **595 (0x253)** 확정 (NOTES의 "530 추정" 정정; 530 value 0은 S1 해제 때 따라오는 꼬리 이벤트).
- 에뮬 `t`(망원)→611, `w`(광각)→610으로 나옴. NOTES §10.5(T=610/W=611)와 반대 → 에뮬 키 매핑 문제인지 실기 기록 문제인지 [?].
- S1(반셔터)은 앱 모드에서 **앱 종료(A2D)를 유발** — 런처에서 `s` 누르자 `arbiter cmd=4 → setState 5 → diadem`(logcat). 키를 앱이 소유하지 않으면 엔진이 S1/재생/MOVIE로 Diadem 복귀.

### 2.2 arbiter 프로토콜 (logcat `DAConnectionManagerService-JNI` + 정적) [V][S]
- 메시지: 큐 `0x01490411`, 436 B, 헤더워드(msg−4)=a7, 레이아웃은 `trackb_re_plan.md` §4 그대로(재확인: libarbiter_proxy `ABT_set_state` @0xb74 디스어셈블). glibc `libosal_uipc`의 `osal_valloc_msg_wait(0x01490411,&m,0x1B4,1)` + `osal_snd_msg`로 **직접 송신 가능** [V](keyprobe `abt`, `snd=0`).
- 정상 수명주기(관찰): 부팅 시 Android `setState(6 BOOT_CMP)` → App List 진입 시 **im.elf가 cmd 2(SET_A)** 송신(`SensorArbiterObserver: update state --> android`) → 런처 onResume `AppInfoReceive` → JNI `ABT_set_state(3,…)`(`resume:pkg:cls:`) → arbiter가 cmd 3을 관찰자들에게 재전달(`arbiter callback: cmd=3 / receive appinfo, do nothing`) → 종료 시 im.elf cmd 4(EXIT) → Android 액티비티 정리 후 `setState(5 EXIT_CMP)` → `update state --> diadem`.
- 순정 송신자 a7: im.elf/libObj는 `0x0088044D`(`sub_38D42C`/`sub_7098E0` 리터럴), viewUnified8 `sub_13560`은 cmd 6, Android는 `0x98`. libObj `ScalarView`(`sub_38D934` @0x38db5a, `ScalarView::EventHandler` @0x38e1cc)는 **저장해 둔 436 B ABT 레코드(pkg/cls/key/keyinfo 포함)를 그대로 재송신**(SCALARBASE "ABT_CMD_SET_A", `byte_EEDB84`가 0이면 "skip ABT_CMD_SET_A").

### 2.3 ABT cmd 3(APP_INFO)와 전키 [V 일부]
- 정상 앱 모드(App List, 런처가 이미 자기 AppInfo를 보낸 상태, r6)에서 keyprobe로 `cmd=3, flag=0, keyinfo={~0×4,0x7FFFDFFF}, a7=0x98` 추가 송신 → arbiter가 관찰자에 재전달(`arbiter callback: cmd=3`) → 이어서 누른 **MENU가 event0에 `514` 1/0(+530 0)으로 도착** [V]. 같은 키가 Android에도 `scancode=514 keyCode=82`로 전달되어 런처가 스스로 종료(`setState 4→5`, diadem 복귀) — 런처 앱의 동작이지 엔진 동작이 아님. 재생/MOVIE는 런처 종료 후라 미확인.
- 런처 자체 AppInfo와 우리 AppInfo가 둘 다 존재했으므로 "**네이티브 ABT 단독으로** 전키가 열리는가"는 분리되지 않음 [?]. 확실한 것: 앱 모드 + (누군가의) APP_INFO 상태면 MENU까지 event0으로 온다. 상태머신이 Diadem이거나 어긋난 상태(§3.3)에서는 cmd 3을 보내도 0건.
- Diadem 상태와 §3.3 상태에서 cmd 3 → event0 0건, 순정 UI가 계속 키 처리 [V].
- 실용 결론: 전키 경로 = **앱 모드 진입 + APP_INFO**. 런처 APK가 투명 Activity로 전면을 유지하고(키를 삼키고 종료하지 않게) 네이티브 UI가 event0을 읽는 NATIVE_DEPLOY §4 구성이 그대로 유효. 네이티브가 직접 cmd 3을 보내는 것은 추가로 해롭지 않음(관찰상 부작용 없음).

---

## 3. SDCom 접속 조건

### 3.1 관찰 [V]
| 상태 | `sdprobe 2 0x000d 0x0009` |
|---|---|
| Diadem 촬영 화면 | `connect = -5` |
| App List 진입 후(런처) | `connect = 0`, `0x000d` → id97 = `0x1000a`(1.10), `0x0009` → id69 = 2 |
| 외부 SET_A 후(§3.3) | `connect = 0`, id69 = 3 |
- 즉 실기에서 "ILCE Remote 실행 중에만 됐다" = **D2A(ScalarDaemon state 5)가 앱 모드 진입 때만 일어나기 때문**. 컴포넌트 ID 2 그대로 사용, 앱이 자기 연결을 갖고 있어도 병행 접속 OK(`trackb_re_plan` §8 일치).

### 3.2 D2A 트리거
- 순정: im.elf가 App List 진입 시 arbiter에 cmd 2(SET_A) → ScalarDaemonIf::onD2ARequested(관찰자) → SDCom open.
- **네이티브 직접 [V]**: keyprobe가 `ABT cmd=2, flag=0, a7=0x98`을 보내자 5초 뒤 `connect=0`. Android는 cmd 2에 반응해 즉시 `setState 4 → 5`(실행할 앱 없음 → 종료) — 그래도 ScalarDaemon은 열린 채 유지, 순정 UI(Diadem 화면·메뉴)는 계속 동작.

### 3.3 ⚠️ 외부 SET_A의 부작용 (실기 금지 사항)
- 외부 SET_A 후 메뉴 → App List로 다시 들어가면 im.elf는 ScalarBase로 넘어가 메인 서피스를 붙이지만 **cmd 2를 다시 보내지 않고**(이미 android 상태로 인식) Android 런처가 뜨지 않음 → **화면 검정, 키 0건, 순정 복귀 수단 없음**(에뮬). 이 상태에서 네이티브 Rigel 창은 단독으로 정상 표시됐음(§1.6 두 번째 그림) — 즉 "Android 없는 Scalar 모드"가 실재하지만, 현재는 **상태머신을 깨뜨려서 얻은 것**.
- 실기에서 이 경로를 쓰면 전원 재시작 외 복구가 없을 가능성 → **CLAUDE.md §1 기준 금지(재부팅 복구는 되지만 원격 복구 불가)**. 정식 경로는 런처 APK로 정상 App 모드 진입.
- 올바른 standalone 진입(Android 없이 im.elf가 ScalarBase+서피스 attach하도록 하는 메시지)은 [?] — 단서: libObj `ScalarView`/`SCALARBASE`(`sub_38D934`, `byte_EEDB84`, `dword_EEDB7C=2`), viewUnified8 `sub_1358A`(cmd 6/BOOT), arbiter.so `ABT_S` 상태 enum.

### 3.4 클라이언트 레시피 (변경 없음, 실측 재확인)
`SDComIPC(listener)` → `connect(0x1540432, 2)` → `createNewTransactionId` → `createNewMessage(0x1540432, 5, tx)` → `setMessageId` → (`addParam`) → `sendSync(msg,&rep,5)` → 응답 param id1 = 결과코드. 객체 크기 제약·시그널 disconnect 규칙(NOTES §10.9) 유지.

---

## 4. 프리뷰 / 라이브뷰

### 4.1 관찰 [V]
- 패널 = VPH 두 레이어: `0xF3440144 = 0x01000233`(bit0 YUV on, bit1 OSD on). YUV = AIF ch51 `0xF3441220` base `0x8CE00000`, line 0x400, 8-bit luma(+C) — 에뮬 `lv-source=bars` 막대 루마(`0xbf…`)가 들어 있음. OSD = ch53 `0x8F758000`(16bpp), 위에 알파 합성.
- Scalar 모드(§3.3)에서 SDCom: `OpenCamera 0x1000`(**파라미터 없음**, Android HAL `SDComMsg_Basic(a1,4096,0)`과 동일) → 0, `CameraStartPreview 0x1004` → 0 (OpenCamera 전엔 결과 2 = 카메라 미오픈), `CameraGetParameters 0x1002` 미오픈 시 2. `GetVideoLayout 0x0604` → id1542=0, 1543=480, 1544=0, 1545=640 (= y,h,x,w 순서로 보임 [?]).
- 결론: 라이브뷰 픽셀은 클라이언트로 오지 않고 YUV 하드웨어 평면에 직접. 우리 UI는 **Rigel 창의 투명(alpha 0) 영역**으로 보여 주는 구조.

### 4.2 남은 것 [?]
- Rigel 합성 결과 아래에 YUV가 실제로 비치는지 스크린덤프 증명 미완: 앱 모드에선 SF 창(`DI_ANDROID_APP`, 런처 화면 = 불투명)이 우리 창 아래 깔려 YUV를 가림. 실험(서버 메모리에서 SF 창 status=0)은 엔진 oops로 중단. 실제 런처 APK 방식에서는 **런처 Activity를 투명 테마로** 만들어야 함(ILCE Remote가 라이브뷰를 보여 주는 것과 같은 구조).
- `CamseqGetPreviewFrame 0x0127`/`StartYUVAreaGet 0x0603` 미시험.

---

## 5. 실험 환경 메모 / 재현 도구
- 스크래치 `/private/tmp/claude-501/…/scratchpad/re-rigel/`: `src/rhello.c`(Rigel), `src/keyprobe.c`(event0 캡처 + ABT 송신), `src/pmem.c`(`/proc/147/mem`으로 WindowServer/창 리스트 덤프), `src/sdprobe.cpp`(app/native 원본 복사, 헤더만 C 헤더로), `src/mkb.sh`(bionic), `src/mkg.sh`(glibc), `bcrt.S`, `font57.h`.
- 에뮬 실행: `A5K_SHELL=46491 … ./run.sh -display none -monitor tcp:127.0.0.1:46490,server,nowait -global cxd90014-aif.lv-source=bars -trace dmp_smaph_job`. `-trace dmp_smaph_job`의 `p3d-draw <target>`으로 합성 여부를 셀 수 있음(OSD 타깃 `0x8d7b0500`, SF GL 타깃 `0x8d858080`).
- 주의: (1) UART 셸에 미완성 따옴표 줄이 남으면 이후 모든 a5kshell 호출이 "no shell" — 소켓에 `\x04\x03\n` 보내서 복구. (2) 호스트 부하(load ~40)에서 App List 진입/키 연타 시 엔진 oops `PC 0x4394aa4c, liro-kliro_113`(= `emulator/docs/RE_ENGINE_OOPS.md`, 에뮬 vCPU 기아 문제, 펌웨어/우리 코드 무관)가 3회 발생 → Sony Exception monitor 프롬프트로 떨어짐. (3) 메뉴 타일 화면은 로딩 중 키를 버림 → 6–8초 대기 후 조작.

## 6. 다음 단계 (우선순위)
1. 런처 AppInfo 없이(투명 런처 대신 네이티브 cmd 3만) 재생 207/MOVIE 515/줌 방향이 event0으로 오는지 분리 실험(§2.3).
2. 투명 Activity 런처 + rhello → 라이브뷰 위 OSD 합성 스크린덤프(§4.2).
3. 블렌드/불투명도 필드 확정(§1.7).
4. standalone Scalar 진입 메시지 RE(§3.3 단서) — 성공 시 Android 런처 불필요.
