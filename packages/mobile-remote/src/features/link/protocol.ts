/**
 * Camera ⇄ phone event channel, PROTOCOL v0+ (placeholder until the CFW protocol is designed,
 * see docs/PROTOCOL.md). JSON text frames over a WebSocket the PHONE opens to the camera
 * (ws://<camera>:8080/api/events), so the phone never has to accept connections — which iOS
 * doesn't allow in the background anyway.
 *
 * Pure types + helpers (runs under `node --test`).
 */

import type { PhotoItem } from '../../lib/camera.ts';

export const PROTOCOL_VERSION = 1;

/** Camera → phone. */
export type CameraEvent =
  | { type: 'hello'; v: number; model: string; clock: number; battery?: number }
  /**
   * After a shot: "where was the phone at capturedAt?". The camera writes the answer into the
   * shot's EXIF itself. Shots taken while the link was down are asked for again on reconnect,
   * with their original capturedAt (camera wall clock, see geotag/trackMath).
   */
  | { type: 'gps.request'; reqId: string; shotId?: number; capturedAt: number }
  | { type: 'photo.added'; item: PhotoItem }
  | { type: 'status'; battery?: number; shotsLeft?: number; mode?: string }
  | { type: 'ping'; t: number };

export type GpsSource = 'live' | 'track' | 'none';

/** Position in a gps.response; t is UTC epoch ms of the fix. */
export type WireFix = { lat: number; lon: number; alt: number | null; acc: number | null; t: number };

/** Phone → camera. */
export type PhoneMessage =
  | { type: 'hello'; v: number; app: string; background: boolean }
  | { type: 'gps.response'; reqId: string; source: GpsSource; fix: WireFix | null; reason?: string }
  | { type: 'phone.state'; background: boolean }
  | { type: 'pong'; t: number };

const EVENT_TYPES = new Set(['hello', 'gps.request', 'photo.added', 'status', 'ping']);

/** Parses one frame; null for anything malformed or unknown (ignored, never thrown). */
export function parseEvent(text: string): CameraEvent | null {
  let value: unknown;
  try {
    value = JSON.parse(text);
  } catch {
    return null;
  }
  if (!value || typeof value !== 'object') return null;
  const e = value as Record<string, unknown>;
  if (typeof e.type !== 'string' || !EVENT_TYPES.has(e.type)) return null;
  if (e.type === 'gps.request' && (typeof e.reqId !== 'string' || typeof e.capturedAt !== 'number')) return null;
  if (e.type === 'hello' && typeof e.clock !== 'number') return null;
  if (e.type === 'photo.added' && (!e.item || typeof (e.item as PhotoItem).id !== 'number')) return null;
  return e as CameraEvent;
}

/** Reconnect delay after the n-th consecutive failure (0-based): 1, 2, 5, 10, 20, 30, 30 … s. */
export function backoffMs(failures: number) {
  const steps = [1000, 2000, 5000, 10_000, 20_000, 30_000];
  return steps[Math.min(failures, steps.length - 1)];
}

/** No frame (ping or other) for this long means the link is dead even if the socket says open. */
export const LINK_SILENCE_MS = 45_000;
