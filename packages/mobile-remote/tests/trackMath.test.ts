import assert from 'node:assert/strict';
import { test } from 'node:test';

import { clockOffset, daysAround, photoUtc, positionAt, thin, type TrackPoint } from '../src/features/geotag/trackMath.ts';

const p = (t: number, lat: number, lon: number): TrackPoint => ({ t, lat, lon, alt: null, acc: 10 });
const track = [p(0, 10, 20), p(60_000, 11, 22), p(30 * 60_000, 50, 50)];

test('interpolates between close points', () => {
  const r = positionAt(track, 30_000)!;
  assert.equal(r.lat, 10.5);
  assert.equal(r.lon, 21);
});

test('takes the nearer point across a long gap, null past maxGap', () => {
  assert.equal(positionAt(track, 60_000 + 5 * 60_000)!.lat, 11);
  assert.equal(positionAt(track, 30 * 60_000 - 4 * 60_000)!.lat, 50);
  assert.equal(positionAt(track, 15 * 60_000), null);
  assert.equal(positionAt(track, -11 * 60_000), null);
  assert.equal(positionAt([], 0), null);
});

test('clock offset folds time zone and drift', () => {
  const phoneUtc = Date.UTC(2026, 10, 21, 3, 0, 0);
  const cameraWall = Date.UTC(2026, 10, 21, 12, 0, 7); // KST wall clock read as UTC, 7 s fast
  const off = clockOffset(cameraWall, phoneUtc);
  assert.equal(photoUtc(Date.UTC(2026, 10, 21, 12, 30, 7), off), Date.UTC(2026, 10, 21, 3, 30, 0));
});

test('thin and day keys', () => {
  assert.equal(thin([p(0, 0, 0), p(1000, 0, 0), p(6000, 0, 0)], 5000).length, 2);
  assert.deepEqual(daysAround(Date.UTC(2026, 0, 1, 0, 5), 10 * 60_000), ['2025-12-31', '2026-01-01']);
});
