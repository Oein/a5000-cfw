#!/bin/sh
# wifi_revert.sh — wifi_survive.sh 원복 (모든 affinity를 전코어 0-3으로). 재부팅해도 원복되나 즉시 복구용.
BB=busybox
IM=$($BB pidof im.elf 2>/dev/null)
ALL=f   # CPU0-3 (0b1111)
echo "wifi_revert: im.elf=$IM, 전부 -> CPU0-3"
for i in 221 218; do $BB echo 1 > /proc/irq/$i/smp_affinity 2>/dev/null; done   # 순정 기본=CPU0
for d in /proc/[0-9]*; do
  c=$($BB cat $d/comm 2>/dev/null) || continue
  p=${d#/proc/}
  case "$c" in
    *sdio*|irq/221*|*ath6kl*|*htc*|*cfg80211*|wpa_supplicant|dhcpcd|ksdioirqd*)
      $BB taskset -p $ALL $p >/dev/null 2>&1 ;;
  esac
done
for t in /proc/$IM/task/[0-9]*; do $BB taskset -p $ALL ${t#/proc/$IM/task/} >/dev/null 2>&1; done
echo "wifi_revert: DONE"
