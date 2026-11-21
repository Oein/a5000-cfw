import * as Location from 'expo-location';
import * as TaskManager from 'expo-task-manager';
import { Platform } from 'react-native';

import { appendPoints, pruneTrack } from './track';
import type { TrackPoint } from './trackMath';

/**
 * Background location: the phone's side of geotagging, and the reason the app stays alive in
 * the background at all.
 *
 * iOS keeps an app with the "location" background mode running while it receives standard
 * location updates — screen off, other apps in front. Everything else in the background session
 * (the camera event socket, answering gps.request, photo sync, Live Activity updates) runs on that
 * time. Android does the same through expo-location's foreground service (with its notification).
 *
 * Two modes, switched by the session:
 *  - "linked":  camera connected. High accuracy, 5 m filter — each shot gets a fresh fix.
 *  - "standby": no camera. Balanced (~100 m), 50 m filter, deferred batches — keeps the track
 *               going for shots taken out of range (asked for on reconnect) at low battery cost.
 *
 * defineTask runs at module load (the root layout imports this through the session).
 */

export const LOCATION_TASK = 'a5k-location';

export type LocationMode = 'linked' | 'standby';

type Listener = (points: TrackPoint[]) => void;
let listener: Listener | null = null;
let lastPrune = 0;
let keepDays = 14;

/** The session receives every batch (one listener; the task works without one too). */
export function setLocationListener(l: Listener | null) {
  listener = l;
}

export function setTrackDays(days: number) {
  keepDays = days;
}

const toPoint = (l: Location.LocationObject): TrackPoint => ({
  t: l.timestamp,
  lat: l.coords.latitude,
  lon: l.coords.longitude,
  alt: l.coords.altitude ?? null,
  acc: l.coords.accuracy ?? null,
});

TaskManager.defineTask<{ locations: Location.LocationObject[] }>(LOCATION_TASK, async ({ data, error }) => {
  if (error || !data?.locations?.length) return;
  const points = data.locations.map(toPoint);
  try {
    appendPoints(points);
    if (Date.now() - lastPrune > 6 * 3600_000) {
      lastPrune = Date.now();
      pruneTrack(keepDays);
    }
  } catch {
    // Storage trouble: the live fix still reaches the session
  }
  listener?.(points);
});

const OPTIONS: Record<LocationMode, Location.LocationTaskOptions> = {
  linked: {
    accuracy: Location.Accuracy.High,
    distanceInterval: 5,
    timeInterval: 5000,
    pausesUpdatesAutomatically: false,
    activityType: Location.ActivityType.Fitness,
    showsBackgroundLocationIndicator: true,
    foregroundService: {
      notificationTitle: '카메라 연결됨',
      notificationBody: '찍는 사진에 위치를 넣기 위해 위치를 확인하고 있어요',
      notificationColor: '#f39800',
      killServiceOnDestroy: false,
    },
  },
  standby: {
    accuracy: Location.Accuracy.Balanced,
    distanceInterval: 50,
    timeInterval: 60_000,
    deferredUpdatesInterval: 5 * 60_000,
    deferredUpdatesDistance: 200,
    pausesUpdatesAutomatically: false,
    activityType: Location.ActivityType.Other,
    showsBackgroundLocationIndicator: true,
    foregroundService: {
      notificationTitle: '카메라 대기 중',
      notificationBody: '카메라가 다시 연결되면 사진 위치를 알려주려고 위치를 기록하고 있어요',
      notificationColor: '#f39800',
      killServiceOnDestroy: false,
    },
  },
};

let currentMode: LocationMode | null = null;

export type PermissionResult = { ok: true } | { ok: false; reason: string };

/** Foreground, then "always" (needed for screen-off). Only call from the foreground (it prompts). */
export async function requestLocationPermissions(): Promise<PermissionResult> {
  const fg = await Location.requestForegroundPermissionsAsync();
  if (!fg.granted) return { ok: false, reason: '위치 권한이 거부됐어요' };
  const bg = await Location.requestBackgroundPermissionsAsync();
  if (!bg.granted) {
    return {
      ok: false,
      reason:
        Platform.OS === 'ios'
          ? '화면이 꺼져도 동작하려면 위치 권한을 "항상"으로 바꿔야 해요 (설정 › a5k Remote › 위치)'
          : '화면이 꺼져도 동작하려면 위치 권한을 "항상 허용"으로 바꿔야 해요',
    };
  }
  return { ok: true };
}

export async function hasBackgroundPermission() {
  const bg = await Location.getBackgroundPermissionsAsync().catch(() => null);
  return bg?.granted ?? false;
}

/** Starts updates in a mode, or switches mode (restarting updates with the new options). */
export async function setLocationMode(mode: LocationMode) {
  const started = await Location.hasStartedLocationUpdatesAsync(LOCATION_TASK).catch(() => false);
  if (started && currentMode === mode) return;
  if (started) await Location.stopLocationUpdatesAsync(LOCATION_TASK).catch(() => {});
  await Location.startLocationUpdatesAsync(LOCATION_TASK, OPTIONS[mode]);
  currentMode = mode;
}

export async function stopLocation() {
  currentMode = null;
  if (await Location.hasStartedLocationUpdatesAsync(LOCATION_TASK).catch(() => false)) {
    await Location.stopLocationUpdatesAsync(LOCATION_TASK);
  }
}

export function locationMode() {
  return currentMode;
}

/** One fresh fix (for a shot taken just now when no recent fix exists), or null after timeoutMs. */
export async function freshFix(timeoutMs = 8000): Promise<TrackPoint | null> {
  const timeout = new Promise<null>((r) => setTimeout(() => r(null), timeoutMs));
  const fix = Location.getCurrentPositionAsync({ accuracy: Location.Accuracy.High })
    .then(toPoint)
    .catch(() => null);
  return Promise.race([fix, timeout]);
}
