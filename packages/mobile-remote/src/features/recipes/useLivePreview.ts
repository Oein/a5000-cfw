import { useCallback, useEffect, useRef, useState } from 'react';

import type { CameraChanges, CameraClient } from '@/lib/camera';

import { recipeChanges } from './apply';
import { lookSnapshot } from './fromCamera';
import type { Recipe } from './model';

export type PreviewStatus = 'off' | 'sending' | 'live' | 'error';

const DEBOUNCE_MS = 350;

/**
 * Live preview while editing a recipe: every change goes to the camera (debounced), so the live
 * view shows the look as it's being made — the way the camera's own menus work. The camera's look
 * from before is snapshotted first; leaving without "apply" or "save & apply" puts it back.
 */
export function useLivePreview(client: CameraClient, connected: boolean, recipe: Recipe, enabled: boolean) {
  const [status, setStatus] = useState<PreviewStatus>('off');
  const snapshot = useRef<CameraChanges | null>(null);
  const evStep = useRef(1 / 3);
  const touched = useRef(false);
  const kept = useRef(false);
  const timer = useRef<ReturnType<typeof setTimeout> | null>(null);
  const seq = useRef(0);
  const clientRef = useRef(client);
  useEffect(() => {
    clientRef.current = client;
  }, [client]);

  // Snapshot once, when the camera is first reachable
  useEffect(() => {
    if (!connected || snapshot.current) return;
    client
      .state()
      .then((s) => {
        snapshot.current = lookSnapshot(s);
        evStep.current = s.evStep || 1 / 3;
      })
      .catch(() => {});
  }, [client, connected]);

  const key = JSON.stringify(recipe);
  useEffect(() => {
    if (!enabled || !connected) return;
    if (timer.current) clearTimeout(timer.current);
    timer.current = setTimeout(() => {
      const mine = ++seq.current;
      touched.current = true;
      kept.current = false;
      setStatus('sending');
      clientRef.current
        .apply(recipeChanges(recipe, evStep.current))
        .then(() => mine === seq.current && setStatus('live'))
        .catch(() => mine === seq.current && setStatus('error'));
    }, DEBOUNCE_MS);
    return () => {
      if (timer.current) clearTimeout(timer.current);
    };
    // recipe is captured through key
    // eslint-disable-next-line react-hooks/exhaustive-deps
  }, [key, enabled, connected]);

  const restore = useCallback(async () => {
    if (timer.current) clearTimeout(timer.current);
    seq.current++;
    if (!touched.current || !snapshot.current) return;
    touched.current = false;
    setStatus('off');
    await clientRef.current.apply(snapshot.current).catch(() => {});
  }, []);

  /** The camera keeps what it has now (applied for real); the snapshot becomes the new baseline. */
  const keep = useCallback(() => {
    kept.current = true;
    touched.current = false;
  }, []);

  // Turning preview off puts the old look back
  useEffect(() => {
    if (!enabled) restore();
  }, [enabled, restore]);

  // Leaving the screen
  useEffect(
    () => () => {
      if (touched.current && !kept.current && snapshot.current) clientRef.current.apply(snapshot.current).catch(() => {});
    },
    [],
  );

  return { status: enabled ? status : 'off', restore, keep, hasSnapshot: () => snapshot.current !== null };
}
