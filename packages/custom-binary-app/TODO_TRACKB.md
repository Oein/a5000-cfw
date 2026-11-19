# TODO_TRACKB.md — 트랙 B(네이티브) 이관 작업 원장

> 트랙 A(Android 앱)로는 불가능하거나, 네이티브 프로세스/엔진 직결이 필요해 **트랙 B에서 해야 하는** 작업만 모음.
> 배경·아키텍처는 CLAUDE.md §2(트랙 B), NOTES.md §8~13. 벽돌 안전 규칙은 CLAUDE.md §1.

## 1. 이미지 DB 복원 — 복구 실행 자체가 Track A 불가 + 진행바 (2026-11-13 이관)
- **⚠️ 복구 실행 자체가 Track A에서 불가**: `com.sony.scalar.sysutil.didep.Media` 는 **이 a5000(ILCE-5000) 런타임에 클래스가 없음** → 호출 시 `NoClassDefFoundError`(2026-11-13 실기 확인). OpenMemories 스텁엔 컴파일용으로만 존재. 즉 앱에서 `prepareRecovery/execRecovery` 불가. (앱은 "네이티브 전용 — 미지원" 안내만 표시하도록 처리됨.) 순정 복구는 im.elf 내부 `M_BRO_REPAIR`(MWF 명령)로 수행 → **트랙 B에서 SDCom/IPC로 im.elf에 복구 명령을 보내는 경로**를 찾아야 실행 가능. (`re/scalar/message_ids.tsv`엔 recovery 메시지 없음 → 추가 RE 필요.)
- **진행바/남은시간(복구가 가능해진 뒤)**: 진행률/예상시간/남은시간이 **카메라 엔진 프로세스(im.elf) 내부 전용**. SDCom 메시지ID·Android 프레임워크·시스템 프로퍼티 어디에도 노출 안 됨(전수 grep 0건, RE 확인). `didep.Media.RecoveryListener`는 단계 콜백 3개(Required→Prepared→Finished)뿐, % 없음.
- **네이티브 소스(RE로 확인된 위치)**:
  - 진행률: `PARAMID_REPAIR_PROGRESS_RATE` → libObj `M_BRO_REPAIR` 모델 멤버 `m_progressRate` → 뷰 `PAS_BarProgress::setProgress(uint)`.
  - 예상시간 3종: `PARAMID_ESTIMATED_TIME` / `DEL_ESTIMATED_TIME` / `REG_ESTIMATED_TIME` → `m_chkTime/m_delTime/m_regTime`(libObj offset 0xA4/0xA8/0xAC) → 뷰가 `${v1:02}:${v2:02}:${v3:02}` 포맷.
  - 상태 레지스터: `SET_STATUS_ID_DB_FOR_BROWSE_CHECKING / REPAIRING / REPAIRED` (im.elf 내부 status 레지스트리).
  - UI 참조: `viewUnified4.so` `ViewManualRepairForPlay`, Layout `RECOVERY_CONFIRMATION/EXECUTION/COMPLETE`, `CHECKING_DATABASE`.
  - 근거: `re/ida/libObj.so.c` 739340–739420/740595–740630, `re/ida/libObj.so.strings.tsv` ~22294–22470, `re/ida/viewUnified4.so.names.tsv` 3853–4829.
- **할 일**: 트랙 B 네이티브 프로세스에서 엔진 이벤트/status 레지스트리를 구독해 `m_progressRate`·예상시간을 읽어 자체 진행바 렌더. (트랙 A는 무한 "복원 중…" 오버레이로 대체 구현됨.)

## 2. CameraEx 미노출 기능 — SDCom 보조 통로 (트랙 A에서도 일부 가능하나 네이티브 확정 필요)
- 피킹(설정ID 54/55), 제브라(194, 라이브는 VDF 0x59000E), AF보조광(16), 프리미엄 자동(Superior Auto) 등 CameraEx가 안 여는 항목.
- 설정명↔SDCom param ID 매핑 미확정(숫자 ID) → 순정 메뉴 토글 전후 덤프/Backup.bin diff로 관찰 확정 필요. 맵: `re/scalar/{message_ids,request_params,camera_params}.tsv`.

## 3. 트랙 B 핵심 미확정 5건 (CLAUDE.md §2, NOTES §8~13)
- [ ] Rigel UX_DEBUG_CLIENT 창 합성(큐 0x014b04b4) — 우리 UI 프로세스 화면 출력.
- [ ] 프리뷰 프레임 취득(VDF 영상 평면, 라이브뷰).
- [ ] evdev 이벤트 ID 실측(/dev/input/event0).
- [ ] ABT 전키 수신(AppInfoReceive→ABT_set_state).
- [ ] WBI 콜드부트.

## 4. 최종 목표 (충분히 검증 후에만, 유일한 진짜 벽돌 위험)
- 순정 카메라 앱을 `/system`에서 교체 + WBI 재생성 + 부팅 트리밍. NAND 백업·복구경로·대면·전면검증 전까지 **착수 금지**.

## 5. 동영상 재생 (트랙 A 불가 확정 2026-11-13 → 트랙 B 필수)
- **현상**: 갤러리 단일뷰에서 MP4(AVC) 재생 시 기기 **하드 리셋**(원격 복구 불가, 물리 재부팅 필요). 실기 4경로 전부 리셋:
  1. `android.media.MediaPlayer`(stagefright/OMX) — 리셋
  2. `com.sony.scalar.media.MediaPlayer` + `setDisplay(holder)` + `setZOrderOnTop(true)` — 리셋
  3. 위에서 `setZOrderOnTop` 제거(hole-punch push-buffer) — 리셋
  4. `setDisplay` 도 제거(surface 미전달, Sony 레퍼런스 `MediaPlayerManager` 와 동일 시퀀스 reset→setDataSource→prepare→start) — **리셋**
- **원인(확정)**: HW 비디오 디코더(VDF 평면)는 im.elf/MprIf 엔진이 배타 소유. scalar MediaPlayer 는 `libmediaex.so` IMediaPlayerExService→MprIf 로 라우팅되지만, **우리 Android 앱 프로세스 윈도우 안에서** prepare/start 하면 VDF 평면이 Android 합성과 충돌 → 커널/엔진 다운. 레퍼런스 앱(srctrl)이 되는 건 순정 네이티브 렌더링 컨텍스트라서. 즉 Android 뷰 레이어에서는 구조적으로 불가(사진 HW 디코드가 OptimizedImage 오버레이 1개로 되는 것과 달리, 동영상은 전용 VDF 평면 소유권이 필요).
- **트랙 A 현재**: `Gallery.MOVIE_PLAYBACK_ENABLED=false` 가드 — OK 누르면 "동영상 재생은 준비 중입니다" 메시지만(리셋 방지). 썸네일(.THM)·삭제·네비게이션은 정상. scalar 재생 코드(startPlayback)는 트랙 B 참고용으로 남겨둠.
- **트랙 B 할 일**: 네이티브 프로세스(Rigel/MprIf)에서 `MprIf::PlayMovie`(libmpr.so) 직접 호출로 VDF 평면에 재생. §3의 "프리뷰 프레임 취득(VDF 영상 평면)"과 같은 평면 소유권 문제 → 그 해결과 묶임.
- **근거**: `rootfs_usr/lib/libmpr.so`(MprIf::PlayMovie/ResumePlayMovie/CancelPlayMovie), `firmware/.../lib/libmediaex.so`(IMediaPlayerExService), `/tmp/stubx` `com.sony.scalar.media.MediaPlayer`(javap: setDisplay/setSurface @Deprecated, prepare 동기), 레퍼런스 github `up209d/open-memories-app-ai` MediaPlayerManager.
