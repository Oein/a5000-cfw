import AsyncStorage from '@react-native-async-storage/async-storage';
import { useEffect, useState } from 'react';

/**
 * App preferences, kept in AsyncStorage as one JSON object. Background code may run before any
 * screen (iOS can relaunch the app for location), so every reader awaits loadPrefs() first.
 */

export type SyncLevel = 'off' | 'thumbs' | 'previews' | 'originals';

export type Prefs = {
  /**
   * The background session: keep the camera link up with the screen off, answer the camera's
   * GPS requests, sync new photos. Needs "always" location permission.
   */
  backgroundSession: boolean;
  /** Show the session on the lock screen / Dynamic Island (iOS Live Activity). */
  liveActivity: boolean;
  /** What sync brings over. */
  syncLevel: SyncLevel;
  /** Save new photos (originals) into the Photos library automatically. */
  autoSaveToPhotos: boolean;
  /** Days of location track kept (answers for shots taken while disconnected). */
  trackDays: number;
  /** Latest camera clock offset (see trackMath.clockOffset), null until measured. */
  clockOffsetMs: number | null;
};

const KEY = 'prefs.v2';

export const DEFAULT_PREFS: Prefs = {
  backgroundSession: false,
  liveActivity: true,
  syncLevel: 'thumbs',
  autoSaveToPhotos: false,
  trackDays: 14,
  clockOffsetMs: null,
};

let current: Prefs | null = null;
let loading: Promise<Prefs> | null = null;
const listeners = new Set<(p: Prefs) => void>();

export function loadPrefs(): Promise<Prefs> {
  if (current) return Promise.resolve(current);
  loading ??= AsyncStorage.getItem(KEY).then(
    (raw) => {
      let saved: Partial<Prefs> = {};
      try {
        saved = raw ? (JSON.parse(raw) as Partial<Prefs>) : {};
      } catch {
        // A broken value means defaults
      }
      current = { ...DEFAULT_PREFS, ...saved };
      return current;
    },
    () => (current = { ...DEFAULT_PREFS }),
  );
  return loading;
}

export async function setPrefs(changes: Partial<Prefs>) {
  const next = { ...(await loadPrefs()), ...changes };
  current = next;
  listeners.forEach((l) => l(next));
  await AsyncStorage.setItem(KEY, JSON.stringify(next));
  return next;
}

export function onPrefsChange(l: (p: Prefs) => void) {
  listeners.add(l);
  return () => listeners.delete(l);
}

export function usePrefs(): Prefs {
  const [prefs, setState] = useState<Prefs>(current ?? DEFAULT_PREFS);
  useEffect(() => {
    let alive = true;
    loadPrefs().then((p) => alive && setState(p));
    listeners.add(setState);
    return () => {
      alive = false;
      listeners.delete(setState);
    };
  }, []);
  return prefs;
}
