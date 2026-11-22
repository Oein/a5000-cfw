import { useEffect, useSyncExternalStore } from 'react';

/** Lets a screen hide the (native) tab bar while it shows its own bottom toolbar, e.g. in select mode. */
const hiders = new Set<string>();
const listeners = new Set<() => void>();
let hidden = false;

function set(key: string, on: boolean) {
  if (on) hiders.add(key);
  else hiders.delete(key);
  const next = hiders.size > 0;
  if (next !== hidden) {
    hidden = next;
    listeners.forEach((l) => l());
  }
}

export function useTabBarHidden() {
  return useSyncExternalStore(
    (l) => {
      listeners.add(l);
      return () => listeners.delete(l);
    },
    () => hidden,
  );
}

/** Hides the tab bar while `on` (and when the screen goes away, shows it again). */
export function useHideTabBar(key: string, on: boolean) {
  useEffect(() => {
    set(key, on);
    return () => set(key, false);
  }, [key, on]);
}
