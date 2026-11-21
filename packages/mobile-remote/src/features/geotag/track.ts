import { Directory, File, Paths } from 'expo-file-system';

import { dayKey, daysAround, DEFAULT_MATCH, positionAt, type MatchOptions, type TrackPoint } from './trackMath';

/**
 * The phone's location track: one JSON-lines file per UTC day under Documents/track/. Appending a
 * line is cheap, which matters in the background task; reading happens only when a photo is
 * matched. Kept on the phone only, never sent anywhere except (optionally) the camera.
 */

const DIR = new Directory(Paths.document, 'track');

function fileFor(day: string) {
  return new File(DIR, `${day}.jsonl`);
}

function ensureDir() {
  if (!DIR.exists) DIR.create({ intermediates: true });
}

export function appendPoints(points: TrackPoint[]) {
  if (points.length === 0) return;
  ensureDir();
  const byDay = new Map<string, TrackPoint[]>();
  for (const p of points) {
    const key = dayKey(p.t);
    byDay.set(key, [...(byDay.get(key) ?? []), p]);
  }
  for (const [day, list] of byDay) {
    const file = fileFor(day);
    const text = list.map((p) => JSON.stringify(p)).join('\n') + '\n';
    if (file.exists) file.write(text, { append: true });
    else file.write(text);
  }
}

const cache = new Map<string, TrackPoint[]>();

function readDay(day: string): TrackPoint[] {
  const file = fileFor(day);
  if (!file.exists) return [];
  const points: TrackPoint[] = [];
  for (const line of file.textSync().split('\n')) {
    if (!line) continue;
    try {
      points.push(JSON.parse(line) as TrackPoint);
    } catch {
      // A line cut short by the app being killed mid-write
    }
  }
  return points.sort((a, b) => a.t - b.t);
}

/** Position at a UTC time from the stored track, or null. */
export function findPosition(utcMs: number, options: MatchOptions = DEFAULT_MATCH): TrackPoint | null {
  const days = daysAround(utcMs, options.maxGapMs);
  const today = dayKey(Date.now());
  const points = days.flatMap((day) => {
    // Today's file still grows: never cache it
    if (day !== today && cache.has(day)) return cache.get(day)!;
    const list = readDay(day);
    if (day !== today) cache.set(day, list);
    return list;
  });
  return positionAt(points.sort((a, b) => a.t - b.t), utcMs, options);
}

export type TrackSummary = { days: number; points: number; bytes: number; latest: TrackPoint | null };

export function trackSummary(): TrackSummary {
  if (!DIR.exists) return { days: 0, points: 0, bytes: 0, latest: null };
  const files = DIR.list().filter((f): f is File => f instanceof File && f.name.endsWith('.jsonl'));
  let bytes = 0;
  let points = 0;
  for (const f of files) {
    bytes += f.size ?? 0;
    points += f.textSync().split('\n').filter(Boolean).length;
  }
  const newest = files.map((f) => f.name.replace('.jsonl', '')).sort().pop();
  const latestList = newest ? readDay(newest) : [];
  return { days: files.length, points, bytes, latest: latestList[latestList.length - 1] ?? null };
}

/** Deletes day files older than keepDays. */
export function pruneTrack(keepDays: number) {
  if (!DIR.exists) return;
  const oldest = dayKey(Date.now() - keepDays * 86_400_000);
  for (const f of DIR.list()) {
    if (f instanceof File && f.name.endsWith('.jsonl') && f.name.replace('.jsonl', '') < oldest) {
      f.delete();
      cache.delete(f.name.replace('.jsonl', ''));
    }
  }
}

export function clearTrack() {
  if (DIR.exists) DIR.delete();
  cache.clear();
}
