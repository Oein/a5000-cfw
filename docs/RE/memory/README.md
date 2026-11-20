# memory — 물리 메모리 배치, RAM 사용처, 트림

| 번호 | 보고서 | 상태 |
|---|---|---|
| 0001 | [RAM 사용처와 안전 트림 목록](0001.%20ram-footprint-and-trim.md) | 에뮬 observed(순정 + route-b 주입), 실기 미확인 |

## 핵심 사실
- Linux MemTotal 175.8 MB / DDR 512 MiB. 나머지 ~336 MB는 `memrsv` carve-out(av-cam·영상·stream 풀)이다.
- Linux RAM 최대 소비자는 Android(PSS ~51 MB)이고, 다음이 im.elf(~31 MB, 대부분 공유 코드)다. 회수 가능한 cold 캐시가 ~48 MB 있다.
- 우리 UI는 +9.8 MB를 쓴다(tmpfs asset 6.6 MB가 최대 항목).
- im.elf는 `oom_adj=0`, Android는 `-16`이라 OOM이 나면 카메라가 먼저 죽는다.

## 열린 항목
- 실기 warm 재개 상태의 meminfo/cgroup/allocstall 시계열 수집("RAM 부족 다운"의 원인 확정)
- `stop zygote` 저메모리 모드에서 실기 telnet/adb가 살아남는지
- 우리 asset subset과 shadow 버퍼 제거(코드 작업, 별도 에이전트)
- oom_adj/min_free_kbytes 런타임 튜닝의 실기 검증
