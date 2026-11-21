import { Directory, File, Paths } from 'expo-file-system';
import { useEffect, useState } from 'react';

import type { CameraClient, PhotoItem } from '@/lib/camera';
import { loadPrefs, type SyncLevel } from '@/lib/prefs';
import { saveOriginal } from '@/lib/save';

/**
 * Background photo sync (feature 2): the phone keeps its own copy of what the camera shoots —
 * small thumbnails always, previews or originals if chosen — so the library can be browsed with
 * the camera off or out of range, and new shots show up while the phone is in a pocket.
 *
 * Cheap by design, on both sides:
 *  - one listing request per tick (newest first, stop at the first known photo), no live view;
 *  - small files first (the camera's ~160 px thumbnails, a few KB), previews only if asked;
 *  - a per-tick item and time budget, so a background wake-up (iOS gives seconds) ends cleanly
 *    and the rest comes next tick;
 *  - one tick at a time, never overlapping.
 *
 * Wake-ups: the session runs a pass on each photo.added event from the camera (foreground or
 * background — the app is alive there through the location session); without the event channel
 * (an older camera app) the foreground polls every FOREGROUND_INTERVAL_MS; and the OS's periodic
 * background task (backgroundTask.ts, best effort, ≥15 min) catches up the rest.
 */

const DIR = new Directory(Paths.document, 'library');
const INDEX = new File(DIR, 'index.json');

export type SyncedItem = {
  /** id + capture time: ids are reused after the card is formatted. */
  key: string;
  id: number;
  date: number;
  thumb: string | null;
  preview: string | null;
  savedToPhotos: boolean;
  syncedAt: number;
};

export type SyncStatus = {
  running: boolean;
  lastRun: number | null;
  lastResult: string | null;
  lastError: string | null;
  /** Photos on the camera not yet copied, as of the last listing. */
  pending: number;
};

type Index = { items: SyncedItem[] };

let index: Index | null = null;
let status: SyncStatus = { running: false, lastRun: null, lastResult: null, lastError: null, pending: 0 };
const listeners = new Set<() => void>();

const notify = () => listeners.forEach((l) => l());

function load(): Index {
  if (index) return index;
  try {
    if (!DIR.exists) DIR.create({ intermediates: true });
    index = INDEX.exists ? (JSON.parse(INDEX.textSync()) as Index) : { items: [] };
  } catch {
    index = { items: [] };
  }
  return index;
}

function persist() {
  try {
    INDEX.write(JSON.stringify(load()));
  } catch {
    // Retried on the next change
  }
  notify();
}

export const keyOf = (item: Pick<PhotoItem, 'id' | 'date'>) => `${item.id}-${item.date}`;

export function syncedItems(): SyncedItem[] {
  return [...load().items].sort((a, b) => b.date - a.date);
}

async function fetchTo(url: string, name: string): Promise<string | null> {
  const file = new File(DIR, name);
  try {
    if (file.exists) file.delete();
    const done = await File.downloadFileAsync(url, file);
    return done.uri;
  } catch {
    return null;
  }
}

/** Photos on the camera that the library doesn't have, oldest first. Reads at most maxPages pages. */
async function newPhotos(client: CameraClient, maxPages = 4, pageSize = 30) {
  const known = new Set(load().items.map((i) => i.key));
  const found: PhotoItem[] = [];
  let offset = 0;
  for (let page = 0; page < maxPages; page++) {
    const list = await client.photos(offset, pageSize);
    let hitKnown = false;
    for (const item of list.items) {
      if (known.has(keyOf(item))) hitKnown = true;
      else found.push(item);
    }
    offset += list.items.length;
    if (hitKnown || offset >= list.total || list.items.length === 0) break;
  }
  return found.reverse();
}

export type SyncOptions = { maxItems: number; maxMs: number };
export const FOREGROUND_BUDGET: SyncOptions = { maxItems: 40, maxMs: 60_000 };
export const BACKGROUND_BUDGET: SyncOptions = { maxItems: 8, maxMs: 20_000 };

/** One sync pass. Resolves with a short summary; never runs twice at once. */
export async function syncOnce(client: CameraClient, budget: SyncOptions = FOREGROUND_BUDGET): Promise<string> {
  if (status.running) return '이미 동기화 중';
  const prefs = await loadPrefs();
  if (prefs.syncLevel === 'off') return '동기화 꺼짐';
  status = { ...status, running: true, lastError: null };
  notify();
  const started = Date.now();
  let copied = 0;
  try {
    await client.info(); // quick reachability check (3 s timeout)
    const fresh = await newPhotos(client);
    for (const item of fresh) {
      if (copied >= budget.maxItems || Date.now() - started > budget.maxMs) break;
      await copyItem(client, item, prefs.syncLevel, prefs.autoSaveToPhotos);
      copied++;
    }
    const pending = fresh.length - copied;
    status = { ...status, pending, lastResult: pending > 0 ? `${copied}장 받음, ${pending}장 남음` : `${copied}장 받음` };
  } catch (e) {
    status = { ...status, lastError: String(e instanceof Error ? e.message : e) };
  } finally {
    status = { ...status, running: false, lastRun: Date.now() };
    notify();
  }
  return status.lastError ?? status.lastResult ?? '';
}

async function copyItem(client: CameraClient, item: PhotoItem, level: SyncLevel, autoSave: boolean) {
  const key = keyOf(item);
  const thumb = await fetchTo(client.smallUrl(item.id, item.date), `${key}.thumb.jpg`);
  const preview = level === 'previews' || level === 'originals' ? await fetchTo(client.previewUrl(item.id, item.date), `${key}.preview.jpg`) : null;
  let savedToPhotos = false;
  if (level === 'originals' && autoSave && item.jpeg) {
    try {
      await saveOriginal(client, item);
      savedToPhotos = true;
    } catch {
      // Left for a manual save from the gallery
    }
  }
  load().items.push({
    key,
    id: item.id,
    date: item.date,
    thumb,
    preview,
    savedToPhotos,
    syncedAt: Date.now(),
  });
  persist();
}

export function syncStatus() {
  return status;
}

/** Removes the phone's copies (not the camera's photos, not what was saved to Photos). */
export function clearLibrary() {
  try {
    if (DIR.exists) DIR.delete();
  } catch {
    // Nothing more to do
  }
  index = { items: [] };
  persist();
}

export function libraryBytes() {
  if (!DIR.exists) return 0;
  return DIR.list().reduce((n, f) => n + (f instanceof File ? (f.size ?? 0) : 0), 0);
}

export function useSyncState() {
  const [, setTick] = useState(0);
  useEffect(() => {
    const l = () => setTick((t) => t + 1);
    listeners.add(l);
    return () => {
      listeners.delete(l);
    };
  }, []);
  return { status, items: syncedItems() };
}

export const FOREGROUND_INTERVAL_MS = 15_000;

/**
 * Fallback polling while the app is open and the camera answers HTTP but has no event channel
 * ({@code eventsLinked} false). With events, the session syncs on photo.added instead.
 */
export function useForegroundSync(client: CameraClient, connected: boolean, eventsLinked: boolean) {
  useEffect(() => {
    if (!connected || eventsLinked) return;
    let alive = true;
    const run = () => alive && syncOnce(client).catch(() => {});
    run();
    const timer = setInterval(run, FOREGROUND_INTERVAL_MS);
    return () => {
      alive = false;
      clearInterval(timer);
    };
  }, [client, connected, eventsLinked]);
}
