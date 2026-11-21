import * as Haptics from 'expo-haptics';
import { useState } from 'react';
import { Pressable, StyleSheet, Text, View, type LayoutChangeEvent, type GestureResponderEvent } from 'react-native';

import { colors } from '@/lib/theme';

const tick = () => Haptics.selectionAsync().catch(() => {});

/** Three-band colour chip showing a recipe's look. */
export function Swatch({ stops, size = 40, radius = 10 }: { stops: [string, string, string]; size?: number; radius?: number }) {
  return (
    <View style={[styles.swatch, { width: size, height: size, borderRadius: radius }]}>
      {stops.map((c, i) => (
        <View key={i} style={{ flex: 1, backgroundColor: c }} />
      ))}
    </View>
  );
}

/**
 * min..max as a row of cells, tap one to pick it (faster than − / +). The zero cell is marked;
 * a long press anywhere resets to 0.
 */
export function ValueStrip({
  label,
  value,
  min,
  max,
  onChange,
  format = (v) => (v > 0 ? `+${v}` : String(v)),
  disabled,
}: {
  label: string;
  value: number;
  min: number;
  max: number;
  onChange: (v: number) => void;
  format?: (v: number) => string;
  disabled?: boolean;
}) {
  const values = Array.from({ length: max - min + 1 }, (_, i) => min + i);
  return (
    <View style={styles.stripRow} accessibilityRole="adjustable" accessibilityLabel={label} accessibilityValue={{ text: format(value) }}
      accessibilityActions={[{ name: 'increment' }, { name: 'decrement' }]}
      onAccessibilityAction={(e) => {
        if (e.nativeEvent.actionName === 'increment' && value < max) onChange(value + 1);
        if (e.nativeEvent.actionName === 'decrement' && value > min) onChange(value - 1);
      }}>
      <View style={styles.stripHead}>
        <Text style={styles.stripLabel}>{label}</Text>
        <Text style={[styles.stripValue, value !== 0 && { color: colors.accent }]}>{format(value)}</Text>
      </View>
      <View style={[styles.strip, disabled && styles.disabled]}>
        {values.map((v) => {
          const selected = v === value;
          const between = (value > 0 && v > 0 && v < value) || (value < 0 && v < 0 && v > value);
          return (
            <Pressable
              key={v}
              disabled={disabled}
              onPress={() => {
                if (v !== value) tick();
                onChange(v);
              }}
              onLongPress={() => {
                tick();
                onChange(0);
              }}
              style={[styles.cell, between && styles.cellBetween, selected && styles.cellSelected]}
              hitSlop={{ top: 8, bottom: 8 }}>
              {v === 0 ? <View style={[styles.zero, selected && { backgroundColor: '#000' }]} /> : null}
            </Pressable>
          );
        })}
      </View>
    </View>
  );
}

/** Segmented control. */
export function Segmented<T extends string | number>({
  values,
  value,
  onChange,
  label,
}: {
  values: readonly T[];
  value: T;
  onChange: (v: T) => void;
  label: (v: T) => string;
}) {
  return (
    <View style={styles.segmented}>
      {values.map((v) => {
        const selected = v === value;
        return (
          <Pressable
            key={String(v)}
            onPress={() => {
              if (!selected) tick();
              onChange(v);
            }}
            style={[styles.segment, selected && styles.segmentSelected]}
            accessibilityRole="button"
            accessibilityState={{ selected }}>
            <Text style={[styles.segmentText, selected && styles.segmentTextSelected]} numberOfLines={1}>
              {label(v)}
            </Text>
          </Pressable>
        );
      })}
    </View>
  );
}

/** Tiles in a grid, each with an optional swatch; for styles and picture effects. */
export function TileGrid<T extends string>({
  values,
  value,
  onChange,
  label,
  swatch,
  columns = 3,
  disabled,
}: {
  values: readonly T[];
  value: T;
  onChange: (v: T) => void;
  label: (v: T) => string;
  swatch?: (v: T) => [string, string, string];
  columns?: number;
  disabled?: boolean;
}) {
  return (
    <View style={[styles.grid, disabled && styles.disabled]}>
      {values.map((v) => {
        const selected = v === value;
        return (
          <Pressable
            key={v}
            disabled={disabled}
            onPress={() => {
              if (!selected) tick();
              onChange(v);
            }}
            style={[styles.tile, { width: `${100 / columns - 2}%` }, selected && styles.tileSelected]}
            accessibilityRole="button"
            accessibilityState={{ selected }}>
            {swatch ? <Swatch stops={swatch(v)} size={22} radius={6} /> : null}
            <Text style={[styles.tileText, selected && { color: colors.accent }]} numberOfLines={1}>
              {label(v)}
            </Text>
          </Pressable>
        );
      })}
    </View>
  );
}

const PAD_STEPS = 15; // -7..+7

/**
 * White balance fine-tune as a 2D pad, like the camera's own: x = blue (−) … amber (+),
 * y = magenta (−) … green (+), recipe-file convention. Tap or drag; double-tap the centre resets.
 */
export function WbPad({
  amberBlue,
  greenMagenta,
  onChange,
  disabled,
}: {
  amberBlue: number;
  greenMagenta: number;
  onChange: (ab: number, gm: number) => void;
  disabled?: boolean;
}) {
  const [size, setSize] = useState(0);
  const cell = size / PAD_STEPS;
  const pick = (e: GestureResponderEvent) => {
    if (!size || disabled) return;
    const { locationX, locationY } = e.nativeEvent;
    const ab = Math.max(-7, Math.min(7, Math.floor(locationX / cell) - 7));
    const gm = Math.max(-7, Math.min(7, 7 - Math.floor(locationY / cell)));
    if (ab !== amberBlue || gm !== greenMagenta) {
      tick();
      onChange(ab, gm);
    }
  };
  return (
    <View style={styles.padWrap}>
      <Text style={styles.padAxis}>G</Text>
      <View style={styles.padRow}>
        <Text style={styles.padAxis}>B</Text>
        <View
          style={[styles.pad, disabled && styles.disabled]}
          onLayout={(e: LayoutChangeEvent) => setSize(e.nativeEvent.layout.width)}
          onStartShouldSetResponder={() => !disabled}
          onMoveShouldSetResponder={() => !disabled}
          onResponderGrant={pick}
          onResponderMove={pick}
          accessibilityLabel={`화이트 밸런스 미세조정 A ${amberBlue}, G ${greenMagenta}`}
          accessibilityRole="adjustable">
          {/* tinted quadrants */}
          <View style={[StyleSheet.absoluteFill, styles.padBg]}>
            <View style={styles.padHalf}>
              <View style={[styles.padQ, { backgroundColor: '#1b3a3a' }]} />
              <View style={[styles.padQ, { backgroundColor: '#3a3a16' }]} />
            </View>
            <View style={styles.padHalf}>
              <View style={[styles.padQ, { backgroundColor: '#24243f' }]} />
              <View style={[styles.padQ, { backgroundColor: '#3f2430' }]} />
            </View>
          </View>
          <View style={[styles.padLineH, { top: size / 2 }]} />
          <View style={[styles.padLineV, { left: size / 2 }]} />
          {size ? (
            <View
              style={[
                styles.padDot,
                { width: cell * 1.4, height: cell * 1.4, borderRadius: cell, left: (amberBlue + 7) * cell + cell / 2 - cell * 0.7, top: (7 - greenMagenta) * cell + cell / 2 - cell * 0.7 },
              ]}
            />
          ) : null}
        </View>
        <Text style={styles.padAxis}>A</Text>
      </View>
      <Text style={styles.padAxis}>M</Text>
      <View style={styles.padFooter}>
        <Text style={styles.padValue}>
          A-B {amberBlue > 0 ? `A${amberBlue}` : amberBlue < 0 ? `B${-amberBlue}` : '0'} · G-M {greenMagenta > 0 ? `G${greenMagenta}` : greenMagenta < 0 ? `M${-greenMagenta}` : '0'}
        </Text>
        {amberBlue || greenMagenta ? (
          <Pressable hitSlop={8} onPress={() => (tick(), onChange(0, 0))}>
            <Text style={styles.reset}>초기화</Text>
          </Pressable>
        ) : null}
      </View>
    </View>
  );
}

const styles = StyleSheet.create({
  swatch: { overflow: 'hidden', flexDirection: 'row', borderWidth: StyleSheet.hairlineWidth, borderColor: 'rgba(255,255,255,0.2)' },
  stripRow: { paddingVertical: 8, gap: 6 },
  stripHead: { flexDirection: 'row', justifyContent: 'space-between' },
  stripLabel: { color: colors.text, fontSize: 15 },
  stripValue: { color: colors.textDim, fontSize: 15, fontVariant: ['tabular-nums'], fontWeight: '600' },
  strip: { flexDirection: 'row', gap: 3, height: 30 },
  cell: { flex: 1, borderRadius: 5, backgroundColor: '#1d1d20', alignItems: 'center', justifyContent: 'center' },
  cellBetween: { backgroundColor: 'rgba(243,152,0,0.3)' },
  cellSelected: { backgroundColor: colors.accent },
  zero: { width: 4, height: 4, borderRadius: 2, backgroundColor: colors.textDim },
  segmented: { flexDirection: 'row', backgroundColor: '#1d1d20', borderRadius: 9, padding: 2, marginVertical: 6 },
  segment: { flex: 1, height: 32, borderRadius: 7, alignItems: 'center', justifyContent: 'center', paddingHorizontal: 4 },
  segmentSelected: { backgroundColor: colors.accent },
  segmentText: { color: colors.text, fontSize: 13, fontWeight: '600' },
  segmentTextSelected: { color: '#000' },
  grid: { flexDirection: 'row', flexWrap: 'wrap', gap: 6, paddingVertical: 8 },
  tile: {
    flexDirection: 'row',
    alignItems: 'center',
    gap: 6,
    height: 40,
    paddingHorizontal: 8,
    borderRadius: 9,
    backgroundColor: '#1d1d20',
    borderWidth: 1.5,
    borderColor: 'transparent',
  },
  tileSelected: { borderColor: colors.accent },
  tileText: { color: colors.text, fontSize: 13, fontWeight: '600', flexShrink: 1 },
  disabled: { opacity: 0.4 },
  padWrap: { alignItems: 'center', paddingVertical: 8, gap: 4 },
  padRow: { flexDirection: 'row', alignItems: 'center', gap: 8 },
  pad: { width: 210, height: 210, borderRadius: 10, overflow: 'hidden' },
  padBg: { flexDirection: 'column' },
  padHalf: { flex: 1, flexDirection: 'row' },
  padQ: { flex: 1 },
  padLineH: { position: 'absolute', left: 0, right: 0, height: StyleSheet.hairlineWidth, backgroundColor: 'rgba(255,255,255,0.35)' },
  padLineV: { position: 'absolute', top: 0, bottom: 0, width: StyleSheet.hairlineWidth, backgroundColor: 'rgba(255,255,255,0.35)' },
  padDot: { position: 'absolute', backgroundColor: colors.accent, borderWidth: 2, borderColor: '#fff' },
  padAxis: { color: colors.textDim, fontSize: 12, fontWeight: '700', width: 12, textAlign: 'center' },
  padFooter: { flexDirection: 'row', gap: 14, alignItems: 'center' },
  padValue: { color: colors.text, fontSize: 13, fontVariant: ['tabular-nums'] },
  reset: { color: colors.accent, fontSize: 13, fontWeight: '600' },
});
