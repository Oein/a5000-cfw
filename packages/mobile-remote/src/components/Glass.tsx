import { GlassView, isLiquidGlassAvailable } from 'expo-glass-effect';
import type { ReactNode } from 'react';
import { StyleSheet, View, type StyleProp, type ViewStyle } from 'react-native';

import { colors } from '@/lib/theme';

const LIQUID = isLiquidGlassAvailable();

/**
 * A Liquid Glass surface (iOS 26+) for controls floating over the live view; elsewhere a dark
 * translucent fill. `style` should not set a background.
 */
export function Glass({ style, children, interactive, tint }: { style?: StyleProp<ViewStyle>; children?: ReactNode; interactive?: boolean; tint?: string }) {
  if (LIQUID) {
    return (
      <GlassView style={style} glassEffectStyle="regular" colorScheme="dark" isInteractive={interactive} tintColor={tint}>
        {children}
      </GlassView>
    );
  }
  return <View style={[styles.fallback, tint ? { backgroundColor: tint } : null, style]}>{children}</View>;
}

const styles = StyleSheet.create({ fallback: { backgroundColor: colors.overlay } });
