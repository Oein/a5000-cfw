import AsyncStorage from '@react-native-async-storage/async-storage';

import { DEFAULT_HOST } from './camera';

/** Persisted camera address; shared by the UI (CameraProvider) and background tasks. */
export const HOST_KEY = 'cameraHost';
/** Dev convenience: EXPO_PUBLIC_CAMERA_HOST=127.0.0.1 (mock server) or the camera's Station-mode address. */
export const INITIAL_HOST = process.env.EXPO_PUBLIC_CAMERA_HOST || DEFAULT_HOST;

export async function loadHost() {
  try {
    return (await AsyncStorage.getItem(HOST_KEY)) || INITIAL_HOST;
  } catch {
    return INITIAL_HOST;
  }
}

const listeners = new Set<(host: string) => void>();

/** Persists a new camera address and tells background code (the session) about it. */
export async function saveHost(host: string) {
  listeners.forEach((l) => l(host));
  await AsyncStorage.setItem(HOST_KEY, host);
}

export function onHostChange(l: (host: string) => void) {
  listeners.add(l);
  return () => listeners.delete(l);
}
