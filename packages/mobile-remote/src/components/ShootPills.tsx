import Ionicons from '@expo/vector-icons/Ionicons';
import { router } from 'expo-router';
import { useState } from 'react';
import { FlatList, Modal, Pressable, StyleSheet, Text, View } from 'react-native';
import { useSafeAreaInsets } from 'react-native-safe-area-context';

import { Swatch } from '@/components/recipe/Controls';
import { useCamera } from '@/lib/CameraProvider';
import { colors } from '@/lib/theme';
import { applyWithFeedback } from '@/features/recipes/actions';
import { markApplied, useRecipes, type StoredRecipe } from '@/features/recipes/store';
import { recipeSummary } from '@/features/recipes/summary';
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
        <Pressable style={styles.pill} onPress={() => setOpen(true)} disabled={!connected} accessibilityLabel="레시피 바꾸기">
          {applied ? <Swatch stops={swatchColors(applied)} size={16} radius={4} /> : <Ionicons name="color-palette-outline" size={14} color={colors.text} />}
          <Text style={styles.text} numberOfLines={1}>
            {applied ? applied.name : '레시피'}
          </Text>
          <Ionicons name="chevron-down" size={12} color={colors.textDim} />
        </Pressable>
        <Pressable style={styles.pill} onPress={() => router.push('/settings')} accessibilityLabel="위치·동기화 상태">
          <Ionicons name={gps ? 'location' : 'location-outline'} size={13} color={gps ? colors.accent : colors.textDim} />
          <Text style={[styles.text, !gps && { color: colors.textDim }]}>{gps ?? '위치 꺼짐'}</Text>
          {session.tagged > 0 ? <Text style={styles.text}>· {session.tagged}장</Text> : null}
          {session.pending > 0 ? (
            <>
              <Ionicons name="arrow-down-circle-outline" size={13} color={colors.textDim} />
              <Text style={styles.text}>{session.pending}</Text>
            </>
          ) : null}
        </Pressable>
      </View>
      <RecipeSheet visible={open} onClose={() => setOpen(false)} appliedId={appliedId} recipes={recipes} />
    </>
  );
}

function RecipeSheet({ visible, onClose, recipes, appliedId }: { visible: boolean; onClose: () => void; recipes: StoredRecipe[]; appliedId: string | null }) {
  const insets = useSafeAreaInsets();
  const { client } = useCamera();
  const [busy, setBusy] = useState<string | null>(null);
  // Favourites first, then the user's own, then built-ins
  const sorted = [...recipes].sort((a, b) => Number(!!b.favorite) - Number(!!a.favorite) || Number(a.builtin) - Number(b.builtin));
  const pick = async (r: StoredRecipe) => {
    setBusy(r.id);
    if (await applyWithFeedback(client, r)) {
      await markApplied(r.id);
      onClose();
    }
    setBusy(null);
  };
  return (
    <Modal visible={visible} transparent animationType="slide" onRequestClose={onClose}>
      <Pressable style={styles.backdrop} onPress={onClose} accessibilityLabel="닫기" />
      <View style={[styles.sheet, { paddingBottom: insets.bottom + 8 }]}>
        <View style={styles.grabber} />
        <View style={styles.sheetHead}>
          <Text style={styles.sheetTitle}>레시피 바꾸기</Text>
          <Pressable
            onPress={() => {
              onClose();
              router.push('/recipes');
            }}>
            <Text style={styles.link}>레시피 관리</Text>
          </Pressable>
        </View>
        <FlatList
          data={sorted}
          keyExtractor={(r) => r.id}
          style={{ maxHeight: 420 }}
          renderItem={({ item }) => (
            <Pressable style={[styles.item, item.id === appliedId && styles.itemOn]} onPress={() => pick(item)} disabled={busy !== null}>
              <Swatch stops={swatchColors(item)} size={36} radius={9} />
              <View style={{ flex: 1 }}>
                <Text style={styles.itemName} numberOfLines={1}>
                  {item.favorite ? '★ ' : ''}
                  {item.name}
                </Text>
                <Text style={styles.itemSummary} numberOfLines={1}>
                  {recipeSummary(item)}
                </Text>
              </View>
              {busy === item.id ? (
                <Text style={styles.itemSummary}>…</Text>
              ) : item.id === appliedId ? (
                <Ionicons name="checkmark-circle" size={22} color={colors.ok} />
              ) : null}
            </Pressable>
          )}
        />
      </View>
    </Modal>
  );
}

const styles = StyleSheet.create({
  row: { flexDirection: 'row', gap: 6, marginTop: 6 },
  pill: { flexDirection: 'row', alignItems: 'center', gap: 5, height: 28, paddingHorizontal: 10, borderRadius: 14, backgroundColor: colors.overlay, maxWidth: 200 },
  text: { color: colors.text, fontSize: 12, fontWeight: '600' },
  backdrop: { flex: 1, backgroundColor: 'rgba(0,0,0,0.45)' },
  sheet: { backgroundColor: colors.panel, borderTopLeftRadius: 18, borderTopRightRadius: 18, paddingHorizontal: 12 },
  grabber: { alignSelf: 'center', width: 38, height: 5, borderRadius: 3, backgroundColor: colors.panelBorder, marginTop: 8 },
  sheetHead: { flexDirection: 'row', justifyContent: 'space-between', alignItems: 'center', paddingVertical: 12, paddingHorizontal: 4 },
  sheetTitle: { color: colors.text, fontSize: 17, fontWeight: '700' },
  link: { color: colors.accent, fontSize: 14, fontWeight: '600' },
  item: { flexDirection: 'row', alignItems: 'center', gap: 12, padding: 10, borderRadius: 12 },
  itemOn: { backgroundColor: 'rgba(62,207,110,0.12)' },
  itemName: { color: colors.text, fontSize: 15, fontWeight: '600' },
  itemSummary: { color: colors.textDim, fontSize: 12, marginTop: 2 },
});
