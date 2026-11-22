import * as Haptics from 'expo-haptics';
import { useEffect, useRef, useState } from 'react';
import { Animated, StyleSheet, Text } from 'react-native';
import { useSafeAreaInsets } from 'react-native-safe-area-context';

import { colors } from './theme';

/**
 * One app-wide toast: short feedback for actions (applied, saved, failed) that doesn't push the
 * layout around like inline messages did. Success/error also give a haptic tick.
 */

type Kind = 'ok' | 'error' | 'info';
type Toast = { id: number; text: string; kind: Kind };

let next = 1;
let listener: ((t: Toast) => void) | null = null;

export function toast(text: string, kind: Kind = 'ok') {
  if (kind === 'ok') Haptics.notificationAsync(Haptics.NotificationFeedbackType.Success).catch(() => {});
  else if (kind === 'error') Haptics.notificationAsync(Haptics.NotificationFeedbackType.Error).catch(() => {});
  listener?.({ id: next++, text, kind });
}

export const toastError = (prefix: string, e: unknown) => toast(`${prefix}: ${e instanceof Error ? e.message : String(e)}`, 'error');

/** Mounted once in the root layout. */
export function ToastHost() {
  const insets = useSafeAreaInsets();
  const [current, setCurrent] = useState<Toast | null>(null);
  const [opacity] = useState(() => new Animated.Value(0));
  const timer = useRef<ReturnType<typeof setTimeout> | null>(null);

  useEffect(() => {
    listener = (t) => {
      setCurrent(t);
      Animated.timing(opacity, { toValue: 1, duration: 140, useNativeDriver: true }).start();
      if (timer.current) clearTimeout(timer.current);
      timer.current = setTimeout(
        () => Animated.timing(opacity, { toValue: 0, duration: 220, useNativeDriver: true }).start(() => setCurrent(null)),
        t.kind === 'error' ? 3500 : 1800,
      );
    };
    return () => {
      listener = null;
    };
  }, [opacity]);

  if (!current) return null;
  return (
    <Animated.View pointerEvents="none" style={[styles.wrap, { top: insets.top + 8, opacity }]}>
      <Text
        style={[styles.toast, current.kind === 'error' && styles.error, current.kind === 'info' && styles.info]}
        accessibilityLiveRegion="polite"
        numberOfLines={3}>
        {current.text}
      </Text>
    </Animated.View>
  );
}

const styles = StyleSheet.create({
  wrap: { position: 'absolute', left: 16, right: 16, alignItems: 'center', zIndex: 1000 },
  toast: {
    backgroundColor: '#1f2a1f',
    color: colors.text,
    borderColor: colors.ok,
    borderWidth: StyleSheet.hairlineWidth,
    paddingHorizontal: 14,
    paddingVertical: 10,
    borderRadius: 12, borderCurve: 'continuous',
    overflow: 'hidden',
    fontSize: 14,
    fontWeight: '600',
  },
  error: { backgroundColor: '#2a1a1a', borderColor: colors.danger },
  info: { backgroundColor: colors.raised, borderColor: colors.panelBorder },
});
