#!/bin/sh
# wifi_survive.sh — AF/라이브뷰 중 Wi-Fi(telnet/adb) 기아 방지 패치 (RAM-only, 벽돌0, 재부팅 원복)
#
# 근거: docs/RE/network/0001 §9 — Wi-Fi SDIO IRQ(mmc1/221)와 데이터경로(ksdioirqd FIFO1,
# ath6kl/htc_rx NORMAL)가 im.elf의 SCHED_RR 71개(prio 6~123)와 같은 CPU(특히 CPU0)에서
# 경쟁 → AF/이미징 부하 시 Wi-Fi 스레드가 CPU를 못 받아 링크 정체(끊김).
#
# 전략: CPU3를 Wi-Fi 전용으로 예약. Wi-Fi IRQ/스레드를 CPU3로, im.elf 전 스레드를 CPU0-2로.
# 그러면 ksdioirqd/ath6kl이 CPU3에서 경쟁 없이 돌아 기아 해소. (chrt 불필요: 전용 코어라 우선순위 무관.)
#
# 전부 /proc·taskset 쓰기 = 휘발(재부팅 원복). 플래시 미기록. busybox 최소환경 가정.
# 사용: telnet root 셸에서  sh wifi_survive.sh   (teardown: sh wifi_revert.sh)
set -e 2>/dev/null
BB=busybox
IM=$($BB pidof im.elf 2>/dev/null)
WIFI_CPU=8     # CPU3 비트마스크 (1<<3)
IM_CPU=7       # CPU0-2 (0b0111)

echo "wifi_survive: im.elf=$IM, Wi-Fi->CPU3(mask $WIFI_CPU), im.elf->CPU0-2(mask $IM_CPU)"

# 1) SDIO 하드 IRQ -> CPU3 (221=mmc1=Wi-Fi, 218=mmc0 동반 이동)
for i in 221 218; do $BB echo $WIFI_CPU > /proc/irq/$i/smp_affinity 2>/dev/null && \
  echo "  irq$i -> $($BB cat /proc/irq/$i/smp_affinity 2>/dev/null)"; done

# 2) Wi-Fi 데이터경로 스레드 전부 -> CPU3
for d in /proc/[0-9]*; do
  c=$($BB cat $d/comm 2>/dev/null) || continue
  p=${d#/proc/}
  case "$c" in
    *sdio*|irq/221*|*ath6kl*|*htc*|*cfg80211*|wpa_supplicant|dhcpcd|ksdioirqd*)
      $BB taskset -p $WIFI_CPU $p >/dev/null 2>&1 && echo "  wifi thread $p ($c) -> CPU3" ;;
  esac
done

# 3) im.elf 전 스레드 -> CPU0-2 (CPU3 비움). 신규 스레드는 이동한 부모서 mask 상속.
n=0
for t in /proc/$IM/task/[0-9]*; do
  tid=${t#/proc/$IM/task/}
  $BB taskset -p $IM_CPU $tid >/dev/null 2>&1 && n=$((n+1))
done
echo "  im.elf threads moved to CPU0-2: $n"
echo "wifi_survive: DONE (revert: sh wifi_revert.sh)"
