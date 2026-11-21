import { Platform } from 'react-native';
import { requireOptionalNativeModule } from 'expo';

/** Mirrors A5kSessionAttributes.ContentState (ios/A5kSessionAttributes.swift). */
export type LiveSessionState = {
  link: 'linked' | 'connecting' | 'lost' | 'standby';
  tagged: number;
  untagged: number;
  battery: number;
  pending: number;
  accuracy: number;
  /** Epoch ms of the last shot, or null. */
  lastShot: number | null;
  message: string;
  /** Seconds until the system shows the activity as stale. */
  staleAfter?: number;
};

type NativeModule = {
  isEnabled(): boolean;
  isRunning(): boolean;
  start(camera: string, state: LiveSessionState): Promise<string>;
  update(state: LiveSessionState): Promise<boolean>;
  end(state: LiveSessionState | null, linger: boolean): Promise<void>;
};

/** null on Android, in Expo Go and on web: callers just skip the Live Activity. */
const native = Platform.OS === 'ios' ? requireOptionalNativeModule<NativeModule>('A5kLiveActivity') : null;

export const LiveActivity = {
  available: native !== null,
  isEnabled: () => native?.isEnabled() ?? false,
  isRunning: () => native?.isRunning() ?? false,
  start: (camera: string, state: LiveSessionState) => native?.start(camera, state) ?? Promise.resolve(null),
  update: (state: LiveSessionState) => native?.update(state) ?? Promise.resolve(false),
  end: (state: LiveSessionState | null = null, linger = true) => native?.end(state, linger) ?? Promise.resolve(),
};
