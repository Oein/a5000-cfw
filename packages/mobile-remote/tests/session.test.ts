import assert from 'node:assert/strict';
import { test } from 'node:test';

import { chooseAnswer } from '../src/features/geotag/answer.ts';
import { positionAt, type TrackPoint } from '../src/features/geotag/trackMath.ts';
import { backoffMs, parseEvent } from '../src/features/link/protocol.ts';

const NOW = Date.UTC(2026, 10, 21, 3, 0, 0);
const KST = 9 * 3600_000; // camera wall clock = UTC+9 read as UTC
const p = (t: number, acc: number | null = 8): TrackPoint => ({ t, lat: 37.5, lon: 127, alt: null, acc });
const track = [p(NOW - 3600_000), p(NOW - 3540_000)];
const lookup = (utc: number) => positionAt(track, utc);

test('fresh shot answered from the live fix', () => {
  const a = chooseAnswer(NOW + KST, KST, p(NOW - 10_000), lookup, NOW);
  assert.equal(a.source, 'live');
  assert.equal(a.fix?.acc, 8);
});

test('shot taken while disconnected answered from the track', () => {
  const a = chooseAnswer(NOW - 3570_000 + KST, KST, p(NOW - 5000), lookup, NOW);
  assert.equal(a.source, 'track');
  assert.equal(a.fix?.t, NOW - 3570_000);
});

test('inaccurate live fix is not used; recent shot may wait for a fix', () => {
  const a = chooseAnswer(NOW + KST, KST, p(NOW, 900), lookup, NOW);
  assert.equal(a.source, 'none');
  assert.equal(a.mayWaitForFix, true);
});

test('old shot with no track: none, never a fresh fix', () => {
  const a = chooseAnswer(NOW - 5 * 3600_000 + KST, KST, p(NOW), lookup, NOW);
  assert.equal(a.source, 'none');
  assert.equal(a.mayWaitForFix, false);
});

test('without a clock offset a request is treated as "now"', () => {
  assert.equal(chooseAnswer(123, null, p(NOW - 1000), lookup, NOW).source, 'live');
});

test('event parsing ignores junk', () => {
  assert.equal(parseEvent('nope'), null);
  assert.equal(parseEvent('{"type":"gps.request","reqId":"a"}'), null);
  assert.equal(parseEvent('{"type":"unknown"}'), null);
  assert.deepEqual(parseEvent('{"type":"gps.request","reqId":"a","capturedAt":1}'), { type: 'gps.request', reqId: 'a', capturedAt: 1 });
  assert.equal(parseEvent('{"type":"hello","model":"x","clock":1,"v":1}')?.type, 'hello');
});

test('backoff grows and caps', () => {
  assert.deepEqual([0, 1, 2, 3, 4, 5, 9].map(backoffMs), [1000, 2000, 5000, 10000, 20000, 30000, 30000]);
});
