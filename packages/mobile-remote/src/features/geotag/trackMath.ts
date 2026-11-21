/**
 * Matching a photo to the phone's location track. Pure functions (run under `node --test`).
 *
 * Time model: the camera has no time zone in its file dates. Photo dates arrive as "camera wall
 * clock as epoch ms" (the wall-clock fields read as if they were UTC). When the phone talks to the
 * camera it reads the camera's clock the same way, so
 *     offsetMs = cameraClock - phoneUtcNow
 * folds the time zone and the camera clock's drift into one number, and a photo's real UTC time
 * is photoDate - offsetMs.
 */

export type TrackPoint = {
  /** UTC epoch ms. */
  t: number;
  lat: number;
  lon: number;
  /** Metres above sea level, or null. */
  alt: number | null;
  /** Horizontal accuracy in metres, or null. */
  acc: number | null;
};

export type MatchOptions = {
  /** A photo further than this from any point gets no position. */
  maxGapMs: number;
  /** Between two points further apart than this, take the nearer one rather than interpolate. */
  maxInterpolateMs: number;
};

export const DEFAULT_MATCH: MatchOptions = { maxGapMs: 10 * 60_000, maxInterpolateMs: 5 * 60_000 };

export function clockOffset(cameraClockMs: number, phoneUtcMs: number) {
  return cameraClockMs - phoneUtcMs;
}

export function photoUtc(photoDateMs: number, offsetMs: number) {
  return photoDateMs - offsetMs;
}

/** Index of the last point with t <= time (points sorted by t), or -1. */
function floorIndex(points: TrackPoint[], time: number) {
  let lo = 0;
  let hi = points.length - 1;
  let found = -1;
  while (lo <= hi) {
    const mid = (lo + hi) >> 1;
    if (points[mid].t <= time) {
      found = mid;
      lo = mid + 1;
    } else hi = mid - 1;
  }
  return found;
}

/**
 * The position at {@code time}: linear between the points around it when they are close in time,
 * otherwise the nearer point, or null when nothing is within maxGapMs. {@code points} sorted by t.
 */
export function positionAt(points: TrackPoint[], time: number, options: MatchOptions = DEFAULT_MATCH): TrackPoint | null {
  if (points.length === 0) return null;
  const i = floorIndex(points, time);
  const before = i >= 0 ? points[i] : null;
  const after = i + 1 < points.length ? points[i + 1] : null;
  if (before && before.t === time) return before;
  if (before && after && after.t - before.t <= options.maxInterpolateMs) {
    const f = (time - before.t) / (after.t - before.t);
    const lerp = (a: number, b: number) => a + (b - a) * f;
    return {
      t: time,
      lat: lerp(before.lat, after.lat),
      lon: lerp(before.lon, after.lon),
      alt: before.alt != null && after.alt != null ? lerp(before.alt, after.alt) : (before.alt ?? after.alt),
      acc: before.acc != null && after.acc != null ? Math.max(before.acc, after.acc) : null,
    };
  }
  const nearest = [before, after]
    .filter((p): p is TrackPoint => p !== null)
    .sort((a, b) => Math.abs(a.t - time) - Math.abs(b.t - time))[0];
  return nearest && Math.abs(nearest.t - time) <= options.maxGapMs ? nearest : null;
}

/** Drops points closer than minMs to the previous kept one, and any out of order. */
export function thin(points: TrackPoint[], minMs: number) {
  const out: TrackPoint[] = [];
  for (const p of points) {
    const last = out[out.length - 1];
    if (!last || p.t - last.t >= minMs) out.push(p);
  }
  return out;
}

/** "YYYY-MM-DD" of a UTC time, the name of the day's track file. */
export function dayKey(t: number) {
  return new Date(t).toISOString().slice(0, 10);
}

/** Day keys whose files may hold points within ±gap of time. */
export function daysAround(t: number, gapMs: number) {
  return [...new Set([dayKey(t - gapMs), dayKey(t), dayKey(t + gapMs)])];
}
