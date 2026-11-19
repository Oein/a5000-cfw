# 실기 세션 플레이북 — route-b 전체 봉인 (device returns → one pass)

목표: 기기 복귀 시 **한 번의 세션**으로 배포·주입하고 device-sealed 6종을 모두 확정한다.
전제: 벽돌 0 (RAM-only, 가역, 플래시 미기록). 크래시 시 물리 전원 재부팅(사용자).

브랜치 `trackb-rigel-display-backend` 최신 빌드 사용. `export CAM_HOST=<기기IP>` (최근 192.168.10.181).

---
## 0. 빌드
```
cd app/native/platform/a5000-inproc
docker run --rm -v "$(cd ../../../..;pwd)":/work -w /work/app/native/platform/a5000-inproc a5k-cross make
# -> build/a5kinproc.so
```

## 1. 배포 (adb push → telnet extract; 검증됨)
```
# 에셋(한글 폰트 6.4MB) — USTAR tar로 (busybox tar가 BSD tar 거부)
python3 -I - /tmp/a5kassets.tar app/native/uicore/build/assets <<'PY'
import sys,tarfile,os
out,d=sys.argv[1],sys.argv[2]
with tarfile.open(out,'w',format=tarfile.USTAR_FORMAT) as t:
 for n in sorted(os.listdir(d)):
  p=os.path.join(d,n)
  if os.path.isfile(p):
   ti=t.gettarinfo(p,arcname=n); ti.uid=ti.gid=0; ti.uname=ti.gname=''
   t.addfile(ti,open(p,'rb'))
PY
adb connect $CAM_HOST:5555
adb -s $CAM_HOST:5555 push /tmp/a5kassets.tar /data/local/tmp/a5kassets.tar
adb -s $CAM_HOST:5555 push app/native/platform/a5000-inproc/build/a5kinproc.so /data/local/tmp/a5kinproc.so
# /android/data/local/tmp 는 telnet root 에서 보임
python3 -I re/scripts/cam.py 'mkdir -p /setting/a5k/assets && cd /setting/a5k/assets && busybox tar xf /android/data/local/tmp/a5kassets.tar && mkdir -p /android/a5k && cp /android/data/local/tmp/a5kinproc.so /android/a5k/a5kinproc.so'
```

## 2. 주입 (러닝 im.elf에 ptrace, 실기 배포법)
```
python3 -I re/scripts/cam.py 'PID=$(pidof im.elf); /android/data/local/tmp/a5kinject $PID /android/a5k/a5kinproc.so 0xc37cd'
# 확인
python3 -I re/scripts/cam.py 'pidof im.elf; cat /proc/uptime; busybox tail -20 /android/a5k/ui.log'
# 기대: display backend=vdf-inproc, assets fonts=1 icons=1, 무크래시
```
라이브뷰 활성화: 카메라를 촬영 모드로 두고 셔터 반누름(그래야 model/CAMERA 값이 채워짐 — inject 직후 비활성이면 -1).

---
## SEAL 1 — 다이얼 CW/CCW 두 a1[0] 코드
사용자: **다이얼을 CW 한 클릭, 잠시 후 CCW 한 클릭** 돌린다.
```
python3 -I re/scripts/cam.py 'busybox grep "dial a1\[0\] codes seen" /android/a5k/ui.log | busybox tail -1'
# -> "dial a1[0] codes seen = A, B (...)"
```
어느 쪽이 CW인지: CW만 여러 번 돌리고 `engine-dial CW` 직전 a1[0] 로그로 판별(필요시 임시 로그 추가). 확정되면:
```
# env.txt(autostart) 또는 재주입 시 환경에 A5K_DIAL_CW_CODE=<CW>, A5K_DIAL_CCW_CODE=<CCW> 설정
```
검증: 재주입 후 CW→CCW 반전이 정상인지 사용자 확인. 코드 확정값은 메모리/engine 기본값에 박아 영구화.

## SEAL 2 — SDCom 전용 설정 실적용 (WB/ISO 등)
우리 메뉴에서 WB(또는 ISO) 변경 → 엔진 반영 확인:
```
python3 -I re/scripts/cam.py 'busybox tail -30 /android/a5k/ui.log | busybox grep -iE "wb|iso|apply|cvpdm|sdcom"'
# 변경 전후 model/CAMERA read 가 바뀌면 OK. NO_ROUTE/ret -5 면 SDCom 송신 경로 수정 필요.
```
그룹라우트(focus/af/drive/meter/storage/expcomp/WB/creative/DRO/steadyshot/flash/aspect/scene)는 바로 될 것. 절대값(aperture/shutter)·순수 SDCom은 여기서 확정.

## SEAL 3 — ISO isoGain 실측
ISO를 100/200/400/AUTO로 바꾸며:
```
python3 -I re/scripts/cam.py 'busybox tail -5 /android/a5k/ui.log | busybox grep iso'
# iso raw/wrapper/isoGain(cam_param 269) 값을 숫자표와 대조 → isoWrapperToNumber 봉인
```

## SEAL 4 — Path A 풀레이트 present 레이트
```
# A5K_VDF_PATHA=1 로 (env 또는 /android/a5k/vdf_patha 마커) 재주입
python3 -I re/scripts/cam.py 'touch /android/a5k/vdf_patha'
# 재주입 후:
python3 -I re/scripts/cam.py 'busybox grep "vdf: present rate" /android/a5k/ui.log | busybox tail -3'
# detour-swap(기본) vs pathA-direct 의 fps 비교. 체감 지연 사용자 확인. 테어링/무크래시 확인.
# BMP fmt 상수(2=16bpp) 실제 RGBA 매핑도 패널 색으로 확인.
```
문제 없으면 Path A 를 기본 ON 으로 승격.

## SEAL 5 — 앱 실행 ABT 시퀀스 (OM Tweak)
순정 애플리케이션 탭을 **수동으로** 한 번 진입(또는 우리 openView) 하며 ABT 로그 관찰:
```
python3 -I re/scripts/cam.py 'busybox dmesg | busybox grep -iE "ABT|android|SyncAndroid" | busybox tail; busybox cat /android/data/resume_info.txt'
# ABT_set_state 인자/순서 확정 → 우리 앱실행 경로의 [device-seal] 채움.
# 직접실행: chroot /android /system/bin/am start -n com.github.ma1co.openmemories.tweak/.MainActivity
```

## SEAL 6 — shots-remaining
SD 카드 넣고 촬영 가능 상태에서:
```
python3 -I re/scripts/cam.py 'busybox tail -5 /android/a5k/ui.log | busybox grep -iE "shots|media"'
# model/MEDIA sel0 p0x103 가 실제 잔여매수인지 확인.
```

---
## 교체(무재부팅 .so 스왑) — 반복 배포 시
```
python3 -I re/scripts/cam.py 'touch /android/a5k/stop'   # 워커 teardown, 후크 스톡 복원
# 새 파일명으로 배포(dlopen path 캐시 우회) 후 재주입
```
리부트 금지(OM wifi/telnet 재설정 유발).

## 종료 후 상태
- 봉인된 상수(다이얼 코드·ISO표·Path A)는 소스 기본값에 반영 → 커밋.
- 미해결은 이 문서에 ✗ 로 남김.
