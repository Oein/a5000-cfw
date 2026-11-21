import { AppState, type AppStateStatus } from 'react-native';
import { useEffect, useState } from 'react';

import { LiveActivity, type LiveSessionState } from '../../../modules/a5k-live-activity';
import { createCameraClient, type CameraClient } from '@/lib/camera';
import { loadHost, onHostChange } from '@/lib/host';
import { loadPrefs, onPrefsChange, setPrefs, type Prefs } from '@/lib/prefs';
import { chooseAnswer } from '@/features/geotag/answer';
import {
  freshFix,
  hasBackgroundPermission,
  setLocationListener,
  setLocationMode,
  setTrackDays,
  stopLocation,
  type LocationMode,
} from '@/features/geotag/locationTask';
import { findPosition } from '@/features/geotag/track';
import { clockOffset, type TrackPoint } from '@/features/geotag/trackMath';
import { EventLink, eventsUrl, type LinkState } from '@/features/link/eventLink';
import type { CameraEvent } from '@/features/link/protocol';
import { BACKGROUND_BUDGET, FOREGROUND_BUDGET, syncOnce, syncStatus } from '@/features/sync/syncEngine';

/**
 * The camera session: everything that must keep working with the phone in a pocket.
 *
 *   camera ──ws events──▶ EventLink ──▶ session ──▶ gps.response (camera writes EXIF itself)
 *                                         │  ├──▶ photo sync (thumbnails, on photo.added)
 *   CoreLocation ──task──▶ track + latest ┘  └──▶ Live Activity (lock screen / Dynamic Island)
 *
 * Runs whenever the app is in the foreground, and — with "backgroundSession" on and "always"
 * location permission — in the background too. iOS keeps the app running there because of the
 * location updates (background mode "location"); nothing else here asks the OS for time.
 *
 * Power: location is precise only while the camera is linked; LOST_TO_STANDBY_MS after the link
 * drops it falls back to the low-power standby mode. The socket carries only small JSON events —
 * no live view (the shooting screen's MJPEG stops when the app leaves the foreground), and sync
 * pulls only what photo.added announces.
 *
 * Module-level singleton, started by initSession() at bundle load (root layout), so a background
 * relaunch gets the same behaviour without any screen mounting.
 */

const LOST_TO_STANDBY_MS = 3 * 60_000;
/** Standby this long ends the Live Activity (it can't be restarted from the background). */
const STANDBY_ENDS_ACTIVITY_MS = 4 * 3600_000;
const LIVE_ACTIVITY_MIN_INTERVAL_MS = 3000;
const SYNC_DEBOUNCE_MS = 1500;

export type SessionLink = 'off' | 'connecting' | 'linked' | 'lost';
export type LiveActivityStatus = 'off' | 'running' | 'unavailable' | 'disabled' | 'needs-foreground';

export type SessionState = {
  active: boolean;
  appActive: boolean;
  link: SessionLink;
  linkDetail: string | null;
  camera: string | null;
  battery: number;
  tagged: number;
  untagged: number;
  lastShot: number | null;
  lastAnswer: { source: string; accuracy: number | null; at: number } | null;
  latestFix: TrackPoint | null;
  locationMode: LocationMode | null;
  backgroundReady: boolean;
  liveActivity: LiveActivityStatus;
  pending: number;
};

let state: SessionState = {
  active: false,
  appActive: AppState.currentState === 'active',
  link: 'off',
  linkDetail: null,
  camera: null,
  battery: -1,
  tagged: 0,
  untagged: 0,
  lastShot: null,
  lastAnswer: null,
  latestFix: null,
  locationMode: null,
  backgroundReady: false,
  liveActivity: LiveActivity.available ? 'off' : 'unavailable',
  pending: 0,
};

const listeners = new Set<() => void>();
let prefs: Prefs | null = null;
let host = '';
let client: CameraClient | null = null;
let link: EventLink | null = null;
let lostSince: number | null = null;
let standbyTimer: ReturnType<typeof setTimeout> | null = null;
let syncTimer: ReturnType<typeof setTimeout> | null = null;
let laTimer: ReturnType<typeof setTimeout> | null = null;
let laLastPush = 0;
let laLastJson = '';
let initialized = false;

function set(changes: Partial<SessionState>) {
  state = { ...state, ...changes };
  listeners.forEach((l) => l());
}

export function sessionState() {
  return state;
}

export function useSession() {
  const [s, setS] = useState(state);
  useEffect(() => {
    const l = () => setS(state);
    listeners.add(l);
    l();
    return () => {
      listeners.delete(l);
    };
  }, []);
  return s;
}

// ---------------------------------------------------------------- lifecycle

/** Idempotent; called at bundle load. */
export function initSession() {
  if (initialized) return;
  initialized = true;
  setLocationListener(onLocations);
  AppState.addEventListener('change', onAppState);
  onPrefsChange((p) => {
    prefs = p;
    setTrackDays(p.trackDays);
    reconcile();
  });
  onHostChange((h) => {
    if (h === host) return;
    host = h;
    restartLink();
  });
  Promise.all([loadPrefs(), loadHost()]).then(([p, h]) => {
    prefs = p;
    host = h;
    setTrackDays(p.trackDays);
    reconcile();
  });
}

function onAppState(next: AppStateStatus) {
  const appActive = next === 'active';
  if (appActive === state.appActive) return;
  set({ appActive });
  link?.setBackground(!appActive);
  if (appActive) {
    link?.kick();
    if (state.link === 'linked') startLiveActivity();
  }
  reconcile();
}

/** Brings link / location / Live Activity in line with prefs, permission and app state. */
async function reconcile() {
  if (!prefs) return;
  const backgroundReady = prefs.backgroundSession && (await hasBackgroundPermission());
  const active = state.appActive || backgroundReady;
  set({ backgroundReady, active });
  if (!active) {
    stopSession();
    return;
  }
  if (!link) restartLink();
  if (backgroundReady) {
    const mode: LocationMode = state.link === 'linked' || (lostSince !== null && Date.now() - lostSince < LOST_TO_STANDBY_MS) ? 'linked' : 'standby';
    await applyLocationMode(mode);
  } else if (state.locationMode) {
    // Foreground only: gps requests are answered from a fresh fix, no continuous updates
    await stopLocation().catch(() => {});
    set({ locationMode: null });
  }
  if (!prefs.liveActivity) endLiveActivity(false);
}

async function applyLocationMode(mode: LocationMode) {
  if (state.locationMode === mode) return;
  try {
    await setLocationMode(mode);
    set({ locationMode: mode });
  } catch (e) {
    set({ linkDetail: `위치 시작 실패: ${String(e)}` });
  }
}

function restartLink() {
  link?.stop();
  link = null;
  if (!host) return;
  client = createCameraClient(host);
  link = new EventLink(eventsUrl(host), { onEvent, onState: onLinkState });
  link.setBackground(!state.appActive);
  link.start();
}

function stopSession() {
  link?.stop();
  link = null;
  if (standbyTimer) clearTimeout(standbyTimer);
  standbyTimer = null;
  lostSince = null;
  stopLocation().catch(() => {});
  endLiveActivity(true);
  set({ link: 'off', locationMode: null, linkDetail: null });
}

// ---------------------------------------------------------------- link

function onLinkState(s: LinkState, detail?: string) {
  if (s === 'open') {
    // Waits for the camera's hello before calling it linked
    set({ link: state.link === 'linked' ? 'linked' : 'connecting', linkDetail: null });
    return;
  }
  if (s === 'connecting') {
    if (state.link === 'off') set({ link: 'connecting' });
    return;
  }
  if (s === 'waiting') {
    if (state.link === 'linked') {
      lostSince = Date.now();
      if (standbyTimer) clearTimeout(standbyTimer);
      standbyTimer = setTimeout(onLongLoss, LOST_TO_STANDBY_MS);
    }
    set({ link: state.link === 'linked' || state.link === 'lost' ? 'lost' : 'connecting', linkDetail: detail ?? null });
    pushLiveActivity(true);
  }
}

function onLongLoss() {
  standbyTimer = null;
  if (state.link === 'linked') return;
  reconcile(); // → standby location mode
  pushLiveActivity(true);
  standbyTimer = setTimeout(() => {
    standbyTimer = null;
    if (state.link !== 'linked') endLiveActivity(true);
  }, STANDBY_ENDS_ACTIVITY_MS - LOST_TO_STANDBY_MS);
}

function onEvent(e: CameraEvent) {
  switch (e.type) {
    case 'hello': {
      lostSince = null;
      if (standbyTimer) clearTimeout(standbyTimer);
      standbyTimer = null;
      set({ link: 'linked', camera: e.model, battery: e.battery ?? state.battery, linkDetail: null });
      const offset = clockOffset(e.clock, Date.now());
      if (prefs && (prefs.clockOffsetMs === null || Math.abs(prefs.clockOffsetMs - offset) > 1500)) setPrefs({ clockOffsetMs: offset });
      reconcile(); // → precise location
      if (state.appActive) startLiveActivity();
      else pushLiveActivity(true);
      syncSoon();
      return;
    }
    case 'gps.request':
      answerGps(e);
      return;
    case 'photo.added':
      syncSoon();
      return;
    case 'status':
      if (typeof e.battery === 'number') {
        set({ battery: e.battery });
        pushLiveActivity(false);
      }
      return;
  }
}

// ---------------------------------------------------------------- GPS answers

function onLocations(points: TrackPoint[]) {
  const latest = points.reduce((a, b) => (b.t > a.t ? b : a));
  set({ latestFix: latest });
  pushLiveActivity(false);
}

async function answerGps(e: Extract<CameraEvent, { type: 'gps.request' }>) {
  const now = Date.now();
  const offset = prefs?.clockOffsetMs ?? null;
  let trackError: string | null = null;
  const lookup = (utc: number) => {
    try {
      return findPosition(utc);
    } catch (err) {
      trackError = String(err instanceof Error ? err.message : err);
      return null;
    }
  };
  let answer = chooseAnswer(e.capturedAt, offset, state.latestFix, lookup, now);
  if (answer.source === 'none' && answer.mayWaitForFix) {
    const fix = await freshFix();
    if (fix) {
      set({ latestFix: fix });
      answer = chooseAnswer(e.capturedAt, offset, fix, lookup, Date.now());
    }
  }
  // reason also carries a track read failure (diagnostics on the camera side)
  const reason = [answer.reason, trackError && `track: ${trackError}`].filter(Boolean).join('; ') || undefined;
  const sent = link?.send({ type: 'gps.response', reqId: e.reqId, source: answer.source, fix: answer.fix, reason }) ?? false;
  if (!sent) return; // the camera asks again after reconnecting
  const tagged = answer.fix !== null;
  set({
    tagged: state.tagged + (tagged ? 1 : 0),
    untagged: state.untagged + (tagged ? 0 : 1),
    lastShot: Date.now(),
    lastAnswer: { source: answer.source, accuracy: answer.fix?.acc ?? null, at: Date.now() },
  });
  pushLiveActivity(true);
}

// ---------------------------------------------------------------- sync

function syncSoon() {
  if (syncTimer) clearTimeout(syncTimer);
  syncTimer = setTimeout(async () => {
    syncTimer = null;
    if (!client) return;
    await syncOnce(client, state.appActive ? FOREGROUND_BUDGET : BACKGROUND_BUDGET).catch(() => {});
    const pending = syncStatus().pending;
    set({ pending });
    pushLiveActivity(false);
    // More left (budget reached): continue shortly, still small steps
    if (pending > 0 && state.link === 'linked') syncSoon();
  }, SYNC_DEBOUNCE_MS);
}

// ---------------------------------------------------------------- Live Activity

function laState(): LiveSessionState {
  const acc = state.latestFix?.acc;
  const message =
    state.link === 'lost'
      ? '재연결 중…'
      : state.link === 'connecting'
        ? '카메라 찾는 중…'
        : state.lastAnswer
          ? state.lastAnswer.source === 'none'
            ? '마지막 사진: 위치 없음'
            : `마지막 사진: 위치 ${state.lastAnswer.source === 'live' ? '실시간' : '기록'}${state.lastAnswer.accuracy !== null ? ` ±${Math.round(state.lastAnswer.accuracy)}m` : ''}`
          : '사진을 찍으면 위치를 알려줘요';
  return {
    link: state.link === 'off' ? 'standby' : state.locationMode === 'standby' && state.link !== 'linked' ? 'standby' : state.link,
    tagged: state.tagged,
    untagged: state.untagged,
    battery: state.battery,
    pending: state.pending,
    accuracy: typeof acc === 'number' ? acc : -1,
    lastShot: state.lastShot,
    message,
    staleAfter: state.link === 'linked' ? 180 : 900,
  };
}

/** ActivityKit only starts activities from the foreground. */
async function startLiveActivity() {
  if (!LiveActivity.available || !prefs?.liveActivity) return;
  if (!LiveActivity.isEnabled()) {
    set({ liveActivity: 'disabled' });
    return;
  }
  if (!state.appActive) {
    if (!LiveActivity.isRunning()) set({ liveActivity: 'needs-foreground' });
    return;
  }
  try {
    await LiveActivity.start(state.camera ?? 'Camera', laState());
    laLastPush = Date.now();
    laLastJson = JSON.stringify(laState());
    set({ liveActivity: 'running' });
  } catch {
    set({ liveActivity: 'disabled' });
  }
}

/** Coalesced update; `important` changes (a shot, link up/down) skip the wait. */
function pushLiveActivity(important: boolean) {
  if (!LiveActivity.available || state.liveActivity !== 'running') return;
  const run = () => {
    laTimer = null;
    const next = laState();
    const json = JSON.stringify(next);
    if (json === laLastJson) return;
    laLastJson = json;
    laLastPush = Date.now();
    LiveActivity.update(next).then((alive) => {
      // Ended by the user or the system (8 h limit)
      if (!alive) set({ liveActivity: state.appActive ? 'off' : 'needs-foreground' });
    });
  };
  const wait = important ? 0 : Math.max(0, LIVE_ACTIVITY_MIN_INTERVAL_MS - (Date.now() - laLastPush));
  if (laTimer) {
    if (!important) return;
    clearTimeout(laTimer);
  }
  laTimer = setTimeout(run, wait);
}

function endLiveActivity(linger: boolean) {
  if (!LiveActivity.available || state.liveActivity === 'off' || state.liveActivity === 'unavailable') return;
  if (laTimer) clearTimeout(laTimer);
  laTimer = null;
  LiveActivity.end({ ...laState(), link: 'standby', message: '연결 종료' }, linger).catch(() => {});
  set({ liveActivity: 'off' });
}

/** For the settings screen: start the Live Activity now (the app is in front). */
export function showLiveActivityNow() {
  return startLiveActivity();
}

/** Resets this session's counters (not the camera's photos or the track). */
export function resetCounters() {
  set({ tagged: 0, untagged: 0, lastShot: null, lastAnswer: null });
  pushLiveActivity(true);
}
