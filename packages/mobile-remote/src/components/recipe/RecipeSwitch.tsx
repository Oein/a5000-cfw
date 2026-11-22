import Ionicons from '@expo/vector-icons/Ionicons';
import { router } from 'expo-router';
import { useMemo, useState } from 'react';
import {
  ActivityIndicator,
  Animated,
  KeyboardAvoidingView,
  Modal,
  PanResponder,
  Platform,
  Pressable,
  SectionList,
  StyleSheet,
  Text,
  TextInput,
  useWindowDimensions,
  View,
} from 'react-native';
import { useSafeAreaInsets } from 'react-native-safe-area-context';

import { Glass } from '@/components/Glass';
import { RecipeRow, SectionHeader } from '@/components/recipe/Library';
import { useCamera } from '@/lib/CameraProvider';
import { colors } from '@/lib/theme';
import { toast } from '@/lib/toast';
import { applyWithFeedback } from '@/features/recipes/actions';
import { buildSections, displayName, type Section } from '@/features/recipes/groups';
import { markApplied, toggleFavorite, useRecipes, type StoredRecipe } from '@/features/recipes/store';

const SHORTCUTS = ['recent', 'fav', 'search'];

/**
 * Quick recipe switch from the shooting screen, in a glass half sheet so the live view stays
 * visible above it. Recent and favourite recipes first, groups folded, groups hidden in the recipe
 * tab left out (search still finds them). Hold a recipe to favourite it.
 */
export function RecipeSwitchSheet({ visible, onClose }: { visible: boolean; onClose: () => void }) {
  const insets = useSafeAreaInsets();
  const { height } = useWindowDimensions();
  const { client } = useCamera();
  const { recipes, appliedId, recentIds, hidden } = useRecipes();
  const [busy, setBusy] = useState<string | null>(null);
  const [query, setQuery] = useState('');
  const [open, setOpen] = useState<Set<string>>(new Set());

  const sections = useMemo(
    () =>
      buildSections(recipes, {
        query,
        recentIds,
        hidden,
        dropHidden: true,
      }).map((s) => {
        const folded = !SHORTCUTS.includes(s.key) && !open.has(s.key);
        return { ...s, collapsed: folded, data: folded ? [] : s.items };
      }),
    [recipes, query, recentIds, hidden, open],
  );
  const hasShortcuts = sections.some((s) => s.key === 'fav' || s.key === 'recent');

  // Drag the top of the sheet down to close it, like a system sheet
  const [drag] = useState(() => new Animated.Value(0));
  const pan = useMemo(
    () =>
      PanResponder.create({
        onStartShouldSetPanResponder: () => true,
        onMoveShouldSetPanResponder: (_, g) => g.dy > 6 && Math.abs(g.dy) > Math.abs(g.dx),
        onPanResponderTerminationRequest: () => false,
        onPanResponderMove: (_, g) => drag.setValue(Math.max(0, g.dy)),
        onPanResponderRelease: (_, g) => {
          if (g.dy > 90 || g.vy > 0.8) {
            Animated.timing(drag, { toValue: 600, duration: 180, useNativeDriver: true }).start(() => {
              onClose();
              drag.setValue(0);
            });
          } else Animated.spring(drag, { toValue: 0, useNativeDriver: true }).start();
        },
      }),
    [drag, onClose],
  );

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
        <Animated.View style={{ transform: [{ translateY: drag }] }}>
          <Glass style={[styles.screen, { height: height * 0.58, paddingBottom: insets.bottom }]}>
            <View {...pan.panHandlers}>
              <View style={styles.grabber} />
              <View style={styles.head}>
                <Text style={styles.title}>레시피 바꾸기</Text>
                <Pressable
                  hitSlop={8}
                  onPress={() => {
                    onClose();
                    router.navigate('/recipes');
                  }}>
                  <Text style={styles.link}>관리</Text>
                </Pressable>
              </View>
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
              keyboardDismissMode="on-drag"
              stickySectionHeadersEnabled={false}
              contentContainerStyle={styles.list}
              renderSectionHeader={({ section }) => {
                const s = section as Section<StoredRecipe>;
                return (
                  <SectionHeader
                    plain
                    section={s}
                    preview={s.items}
                    onToggle={
                      SHORTCUTS.includes(s.key)
                        ? undefined
                        : () =>
                            setOpen((o) => {
                              const n = new Set(o);
                              if (!n.delete(s.key)) n.add(s.key);
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
                  showGroup={SHORTCUTS.includes((section as Section<StoredRecipe>).key)}
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
              ListHeaderComponent={!query && !hasShortcuts ? <Text style={styles.hint}>레시피를 길게 누르면 즐겨찾기 — 이 목록 맨 위에 나와요.</Text> : null}
              ListEmptyComponent={<Text style={styles.hint}>맞는 레시피가 없어요.</Text>}
              ListFooterComponent={!query && hidden.size ? <Text style={styles.hint}>{`숨긴 그룹 ${hidden.size}개는 검색하면 나와요.`}</Text> : null}
            />
          </Glass>
        </Animated.View>
      </KeyboardAvoidingView>
    </Modal>
  );
}

const styles = StyleSheet.create({
  backdrop: { flex: 1, backgroundColor: 'rgba(0,0,0,0.2)' },
  screen: {
    paddingHorizontal: 12,
    borderTopLeftRadius: 30,
    borderTopRightRadius: 30,
    borderCurve: 'continuous',
    overflow: 'hidden',
  },
  grabber: {
    alignSelf: 'center',
    width: 36,
    height: 5,
    borderRadius: 3,
    backgroundColor: 'rgba(235,235,245,0.3)',
    marginTop: 6,
    marginBottom: 8,
  },
  head: {
    flexDirection: 'row',
    justifyContent: 'space-between',
    alignItems: 'center',
    paddingHorizontal: 6,
    paddingBottom: 10,
  },
  title: { color: colors.text, fontSize: 20, fontWeight: '700' },
  link: { color: colors.accent, fontSize: 16, fontWeight: '600' },
  searchWrap: {
    flexDirection: 'row',
    alignItems: 'center',
    gap: 8,
    paddingHorizontal: 10,
    height: 38,
    borderRadius: 12,
    borderCurve: 'continuous',
    backgroundColor: 'rgba(118,118,128,0.24)',
    marginBottom: 4,
  },
  search: { flex: 1, color: colors.text, fontSize: 16 },
  list: { paddingBottom: 30 },
  hint: {
    color: colors.textDim,
    fontSize: 12,
    textAlign: 'center',
    paddingVertical: 12,
    paddingHorizontal: 12,
  },
});
