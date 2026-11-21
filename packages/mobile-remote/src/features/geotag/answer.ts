/**
 * Choosing the position to answer a camera's gps.request with. Pure (runs under `node --test`).
 *
 *  1. live  — the latest fix, if it is within LIVE_WINDOW_MS of the shot and accurate enough;
 *  2. track — the recorded track around the shot (shots taken while the link was down are asked
 *             for again on reconnect, so this is the normal path for them);
 *  3. none  — nothing close enough. The caller may then ask the OS for a fresh fix, but only for
 *             a shot taken just now (a fresh fix says nothing about where an old shot was).
 */

import { DEFAULT_MATCH, photoUtc, type MatchOptions, type TrackPoint } from './trackMath.ts';
import type { GpsSource, WireFix } from '../link/protocol.ts';

export const LIVE_WINDOW_MS = 60_000;
/** Worse than this the fix isn't used (city-block level is still a useful geotag). */
export const MAX_ACCURACY_M = 200;
/** A shot this recent may wait for a fresh fix. */
export const FRESH_FIX_WINDOW_MS = 2 * 60_000;

export type Answer = { source: GpsSource; fix: WireFix | null; reason?: string };

const wire = (p: TrackPoint): WireFix => ({ lat: p.lat, lon: p.lon, alt: p.alt, acc: p.acc, t: p.t });
const accurate = (p: TrackPoint) => p.acc === null || p.acc <= MAX_ACCURACY_M;

export function chooseAnswer(
  capturedAtCamera: number,
  clockOffsetMs: number | null,
  latest: TrackPoint | null,
  track: (utc: number, options: MatchOptions) => TrackPoint | null,
  now: number,
): Answer & { shotUtc: number; mayWaitForFix: boolean } {
  // Without a measured offset assume the shot is "now": requests normally come right after a shot
  const shotUtc = clockOffsetMs === null ? now : photoUtc(capturedAtCamera, clockOffsetMs);
  const recent = Math.abs(now - shotUtc) <= FRESH_FIX_WINDOW_MS;
  if (latest && accurate(latest) && Math.abs(latest.t - shotUtc) <= LIVE_WINDOW_MS) {
    return { source: 'live', fix: wire(latest), shotUtc, mayWaitForFix: false };
  }
  const fromTrack = track(shotUtc, DEFAULT_MATCH);
  if (fromTrack && accurate(fromTrack)) return { source: 'track', fix: wire(fromTrack), shotUtc, mayWaitForFix: false };
  return {
    source: 'none',
    fix: null,
    reason: recent ? 'no fix yet' : 'no position recorded around that time',
    shotUtc,
    mayWaitForFix: recent,
  };
}
