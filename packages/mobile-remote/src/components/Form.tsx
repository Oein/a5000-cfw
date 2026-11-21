import type { ReactNode } from 'react';
import { Pressable, ScrollView, StyleSheet, Switch, Text, View } from 'react-native';

import { colors } from '@/lib/theme';

/** Small form controls shared by the recipe editor and the settings screen. */

export function Section({ title, children }: { title: string; children: ReactNode }) {
  return (
    <View style={styles.sectionWrap}>
      <Text style={styles.section}>{title}</Text>
      <View style={styles.card}>{children}</View>
    </View>
  );
}

export function Row({ label, detail, children }: { label: string; detail?: string; children?: ReactNode }) {
  return (
    <View style={styles.row}>
      <View style={styles.rowText}>
        <Text style={styles.label}>{label}</Text>
        {detail ? <Text style={styles.detail}>{detail}</Text> : null}
      </View>
      {children}
    </View>
  );
}

export function ToggleRow({ label, detail, value, onChange, disabled }: { label: string; detail?: string; value: boolean; onChange: (v: boolean) => void; disabled?: boolean }) {
  return (
    <Row label={label} detail={detail}>
      <Switch value={value} onValueChange={onChange} disabled={disabled} trackColor={{ true: colors.accent }} />
    </Row>
  );
}

/** − value + with a range; shows signed numbers. */
export function Stepper({ value, min, max, onChange, format }: { value: number; min: number; max: number; onChange: (v: number) => void; format?: (v: number) => string }) {
  const text = format ? format(value) : value > 0 ? `+${value}` : String(value);
  return (
    <View style={styles.stepper}>
      <Pressable accessibilityLabel="감소" style={[styles.stepButton, value <= min && styles.disabled]} disabled={value <= min} onPress={() => onChange(value - 1)}>
        <Text style={styles.stepText}>−</Text>
      </Pressable>
      <Text style={styles.stepValue}>{text}</Text>
      <Pressable accessibilityLabel="증가" style={[styles.stepButton, value >= max && styles.disabled]} disabled={value >= max} onPress={() => onChange(value + 1)}>
        <Text style={styles.stepText}>+</Text>
      </Pressable>
    </View>
  );
}

/** Horizontally scrolling chips, one selected. */
export function Chips<T extends string | number>({ values, value, onChange, label }: { values: readonly T[]; value: T; onChange: (v: T) => void; label?: (v: T) => string }) {
  return (
    <ScrollView horizontal showsHorizontalScrollIndicator={false} contentContainerStyle={styles.chips}>
      {values.map((v) => {
        const selected = v === value;
        return (
          <Pressable key={String(v)} onPress={() => onChange(v)} style={[styles.chip, selected && styles.chipSelected]} accessibilityRole="button" accessibilityState={{ selected }}>
            <Text style={[styles.chipText, selected && styles.chipTextSelected]}>{label ? label(v) : String(v)}</Text>
          </Pressable>
        );
      })}
    </ScrollView>
  );
}

export function Button({ title, onPress, kind = 'primary', disabled }: { title: string; onPress: () => void; kind?: 'primary' | 'secondary' | 'danger'; disabled?: boolean }) {
  return (
    <Pressable
      onPress={onPress}
      disabled={disabled}
      style={[styles.button, kind !== 'primary' && styles.secondary, disabled && styles.disabled]}
      accessibilityRole="button">
      <Text style={[styles.buttonText, kind === 'secondary' && { color: colors.text }, kind === 'danger' && { color: colors.danger }]}>{title}</Text>
    </Pressable>
  );
}

export const formStyles = StyleSheet.create({
  help: { color: colors.textDim, fontSize: 13, lineHeight: 19 },
  buttons: { flexDirection: 'row', flexWrap: 'wrap', gap: 10 },
});

const styles = StyleSheet.create({
  sectionWrap: { gap: 6, marginTop: 10 },
  section: { color: colors.textDim, fontSize: 13, fontWeight: '600' },
  card: { backgroundColor: colors.panel, borderRadius: 12, paddingHorizontal: 14, paddingVertical: 4 },
  row: { flexDirection: 'row', alignItems: 'center', minHeight: 48, gap: 10, paddingVertical: 6, borderBottomColor: colors.hairline, borderBottomWidth: StyleSheet.hairlineWidth },
  rowText: { flex: 1 },
  label: { color: colors.text, fontSize: 15 },
  detail: { color: colors.textDim, fontSize: 12, marginTop: 2 },
  stepper: { flexDirection: 'row', alignItems: 'center', gap: 4 },
  stepButton: { width: 36, height: 32, borderRadius: 8, backgroundColor: '#1d1d20', alignItems: 'center', justifyContent: 'center' },
  stepText: { color: colors.text, fontSize: 20, lineHeight: 22 },
  stepValue: { color: colors.text, minWidth: 52, textAlign: 'center', fontSize: 15, fontVariant: ['tabular-nums'] },
  chips: { gap: 6, paddingVertical: 8 },
  chip: { paddingHorizontal: 12, height: 32, borderRadius: 8, borderWidth: 1, borderColor: colors.panelBorder, justifyContent: 'center' },
  chipSelected: { backgroundColor: colors.accent, borderColor: colors.accent },
  chipText: { color: colors.text, fontSize: 13, fontWeight: '600' },
  chipTextSelected: { color: '#000' },
  button: { height: 44, borderRadius: 10, backgroundColor: colors.accent, paddingHorizontal: 18, justifyContent: 'center' },
  secondary: { backgroundColor: colors.panel, borderWidth: 1, borderColor: colors.panelBorder },
  buttonText: { color: '#000', fontWeight: '700', fontSize: 15 },
  disabled: { opacity: 0.4 },
});
