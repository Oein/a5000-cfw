import Ionicons from '@expo/vector-icons/Ionicons';
import { router } from 'expo-router';
import { useMemo, useState } from 'react';
import { ActivityIndicator, KeyboardAvoidingView, Modal, Platform, Pressable, SectionList, StyleSheet, Text, TextInput, useWindowDimensions, View } from 'react-native';
import { useSafeAreaInsets } from 'react-native-safe-area-context';

import { Swatch } from '@/components/recipe/Controls';
import { RecipeRow, SectionHeader } from '@/components/recipe/Library';
import { useCamera } from '@/lib/CameraProvider';
import { colors } from '@/lib/theme';
import { toast } from '@/lib/toast';
import { applyWithFeedback } from '@/features/recipes/actions';
import { buildSections, displayName, type Section } from '@/features/recipes/groups';
import { markApplied, toggleFavorite, useRecipes, type StoredRecipe } from '@/features/recipes/store';
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
            {applied ? displayName(applied) : '레시피'}
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
  const { height } = useWindowDimensions();
  const { client } = useCamera();
  const { recentIds, hidden } = useRecipes();
  const [busy, setBusy] = useState<string | null>(null);
  const [query, setQuery] = useState('');
  // Groups open in this sheet (separate from the recipe tab's folding: here everything starts folded
  // except the shortcuts, so the sheet opens on what you use)
  const [open, setOpen] = useState<Set<string>>(new Set());
  const sections = useMemo(() => {
    const all = buildSections(recipes, { query, recentIds, hidden, dropHidden: true });
    return all.map((s) => {
      const shortcut = s.key === 'recent' || s.key === 'fav' || s.key === 'search';
      const folded = shortcut ? false : !open.has(s.key);
      return { ...s, collapsed: folded, data: folded ? [] : s.items };
    });
  }, [recipes, query, recentIds, hidden, open]);
  const hasShortcuts = sections.some((s) => s.key === 'fav' || s.key === 'recent');
  const hiddenCount = hidden.size;

  const pick = async (r: StoredRecipe) => {
    setBusy(r.id);
    if (await applyWithFeedback(client, r)) {
      await markApplied(r.id);
      setQuery('');
      onClose();
    }
    setBusy(null);
  };
  const favorite = (r: StoredRecipe) => toggleFavorite(r.id).then((on) => toast(on ? `★ ${displayName(r)} — 맨 위에 나와요` : '즐겨찾기 해제', 'info'));

  return (
    <Modal visible={visible} transparent animationType="slide" onRequestClose={onClose}>
      <Pressable style={styles.backdrop} onPress={onClose} accessibilityLabel="닫기" />
      <KeyboardAvoidingView behavior={Platform.OS === 'ios' ? 'padding' : undefined}>
        <View style={[styles.sheet, { paddingBottom: insets.bottom + 8, maxHeight: height * 0.72 }]}>
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
          <View style={styles.searchWrap}>
            <Ionicons name="search" size={15} color={colors.textDim} />
            <TextInput
              style={styles.search}
              value={query}
              onChangeText={setQuery}
              placeholder="검색 — 이름·브랜드·스타일"
              placeholderTextColor={colors.textDim}
              autoCorrect={false}
              clearButtonMode="while-editing"
            />
          </View>
          <SectionList
            sections={sections}
            keyExtractor={(r, i) => `${r.id}-${i}`}
            keyboardShouldPersistTaps="handled"
            stickySectionHeadersEnabled={false}
            renderSectionHeader={({ section }) => {
              const s = section as Section<StoredRecipe>;
              const shortcut = s.key === 'recent' || s.key === 'fav' || s.key === 'search';
              return (
                <SectionHeader
                  section={s}
                  preview={s.items}
                  onToggle={
                    shortcut
                      ? undefined
                      : () =>
                          setOpen((o) => {
                            const n = new Set(o);
                            if (n.has(s.key)) n.delete(s.key);
                            else n.add(s.key);
                            return n;
                          })
                  }
                />
              );
            }}
            renderItem={({ item, section }) => (
              <RecipeRow
                recipe={item}
                compact
                applied={item.id === appliedId}
                showGroup={['recent', 'fav', 'search'].includes((section as Section<StoredRecipe>).key)}
                onPress={() => pick(item)}
                onLongPress={() => favorite(item)}
                right={
                  busy === item.id ? (
                    <ActivityIndicator size="small" color={colors.accent} />
                  ) : item.id === appliedId ? (
                    <Ionicons name="checkmark-circle" size={22} color={colors.ok} />
                  ) : null
                }
              />
            )}
            ListHeaderComponent={
              !query && !hasShortcuts ? <Text style={styles.hint}>레시피를 길게 누르면 즐겨찾기 — 이 목록 맨 위에 나와요.</Text> : null
            }
            ListEmptyComponent={<Text style={styles.hint}>맞는 레시피가 없어요.</Text>}
            ListFooterComponent={
              !query && hiddenCount ? <Text style={styles.hint}>{`숨긴 그룹 ${hiddenCount}개는 검색하면 나와요.`}</Text> : null
            }
          />
        </View>
      </KeyboardAvoidingView>
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
  searchWrap: { flexDirection: 'row', alignItems: 'center', gap: 8, paddingHorizontal: 10, height: 36, borderRadius: 10, backgroundColor: '#1d1d20', marginBottom: 4 },
  search: { flex: 1, color: colors.text, fontSize: 15 },
  hint: { color: colors.textDim, fontSize: 12, textAlign: 'center', paddingVertical: 12, paddingHorizontal: 12 },
});
