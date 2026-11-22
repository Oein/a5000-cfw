import Ionicons from '@expo/vector-icons/Ionicons';
import { router } from 'expo-router';
import { useIsFocused } from 'expo-router/react-navigation';
import { useState } from 'react';
import { Pressable, StyleSheet, Text, View } from 'react-native';

import { Glass } from '@/components/Glass';
import { Swatch } from '@/components/recipe/Controls';
import { RecipeSwitchSheet } from '@/components/recipe/RecipeSwitch';
import { useCamera } from '@/lib/CameraProvider';
import { colors } from '@/lib/theme';
import { displayName } from '@/features/recipes/groups';
import { useRecipes } from '@/features/recipes/store';
import { swatchColors } from '@/features/recipes/swatch';
import { useSession } from '@/features/session/session';

/**
 * Second row of pills on the live view: the applied recipe (tap → quick switch without leaving the
 * shooting screen) and the GPS/sync state of the camera session (tap → settings).
 */
export function ShootPills() {
  const { connected } = useCamera();
  const { recipes, appliedId } = useRecipes();
  const session = useSession();
  const [open, setOpen] = useState(false);
  // Leaving the shooting screen closes the sheet (it must not float over other tabs)
  const focused = useIsFocused();
  if (!focused && open) setOpen(false);
  const applied = recipes.find((r) => r.id === appliedId);
  const acc = session.latestFix?.acc;
  const gps =
    session.locationMode || session.lastAnswer
      ? typeof acc === 'number'
        ? `±${Math.round(acc)}m`
        : 'GPS'
      : null;

  return (
    <>
      <View style={styles.row}>
        <Pressable onPress={() => setOpen(true)} disabled={!connected} accessibilityLabel="레시피 바꾸기">
          <Glass style={styles.pill} interactive>
          {applied ? <Swatch stops={swatchColors(applied)} size={16} radius={4} /> : <Ionicons name="color-palette-outline" size={14} color={colors.text} />}
          <Text style={styles.text} numberOfLines={1}>
            {applied ? displayName(applied) : '레시피'}
          </Text>
          <Ionicons name="chevron-down" size={12} color={colors.textDim} />
          </Glass>
        </Pressable>
        <Pressable onPress={() => router.push('/settings')} accessibilityLabel="위치·동기화 상태">
          <Glass style={styles.pill} interactive>
          <Ionicons name={gps ? 'location' : 'location-outline'} size={13} color={gps ? colors.accent : colors.textDim} />
          <Text style={[styles.text, !gps && { color: colors.textDim }]}>{gps ?? '위치 꺼짐'}</Text>
          {session.tagged > 0 ? <Text style={styles.text}>· {session.tagged}장</Text> : null}
          {session.pending > 0 ? (
            <>
              <Ionicons name="arrow-down-circle-outline" size={13} color={colors.textDim} />
              <Text style={styles.text}>{session.pending}</Text>
            </>
          ) : null}
          </Glass>
        </Pressable>
      </View>
      <RecipeSwitchSheet visible={open && focused} onClose={() => setOpen(false)} />
    </>
  );
}

const styles = StyleSheet.create({
  row: { flexDirection: 'row', gap: 6, marginTop: 6 },
  pill: { flexDirection: 'row', alignItems: 'center', gap: 5, height: 28, paddingHorizontal: 10, borderRadius: 14, borderCurve: 'continuous', overflow: 'hidden', maxWidth: 200 },
  text: { color: colors.text, fontSize: 12, fontWeight: '600' },
});
