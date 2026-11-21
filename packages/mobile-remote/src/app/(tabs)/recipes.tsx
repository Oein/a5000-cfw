import Ionicons from '@expo/vector-icons/Ionicons';
import { router, useNavigation } from 'expo-router';
import { useLayoutEffect, useMemo, useState } from 'react';
import { Pressable, SectionList, StyleSheet, Text, TextInput, View } from 'react-native';

import { useCamera } from '@/lib/CameraProvider';
import { colors } from '@/lib/theme';
import { applyRecipe } from '@/features/recipes/applyToCamera';
import { recipeSummary } from '@/features/recipes/summary';
import { toggleFavorite, useRecipes, type StoredRecipe } from '@/features/recipes/store';

/**
 * Recipe library (feature 3): favourites, the user's recipes, built-ins. Tap to open, the round
 * button applies it to the camera at once.
 */
export default function RecipesScreen() {
  const navigation = useNavigation();
  const { client, connected } = useCamera();
  const { recipes, appliedId } = useRecipes();
  const [query, setQuery] = useState('');
  const [busy, setBusy] = useState<string | null>(null);
  const [message, setMessage] = useState<string | null>(null);

  useLayoutEffect(() => {
    navigation.setOptions({
      headerRight: () => (
        <View style={styles.headerButtons}>
          <Pressable accessibilityLabel="레시피 가져오기" hitSlop={8} onPress={() => router.push('/recipe/import')}>
            <Ionicons name="download-outline" size={22} color={colors.accent} />
          </Pressable>
          <Pressable accessibilityLabel="새 레시피" hitSlop={8} onPress={() => router.push('/recipe/new')}>
            <Ionicons name="add" size={26} color={colors.accent} />
          </Pressable>
        </View>
      ),
    });
  }, [navigation]);

  const sections = useMemo(() => {
    const q = query.trim().toLowerCase();
    const match = (r: StoredRecipe) => !q || r.name.toLowerCase().includes(q) || recipeSummary(r).toLowerCase().includes(q);
    const list = recipes.filter(match);
    return [
      { title: '즐겨찾기', data: list.filter((r) => r.favorite) },
      { title: '내 레시피', data: list.filter((r) => !r.builtin && !r.favorite) },
      { title: '기본', data: list.filter((r) => r.builtin) },
    ].filter((s) => s.data.length > 0);
  }, [recipes, query]);

  const apply = async (r: StoredRecipe) => {
    setBusy(r.id);
    setMessage(null);
    try {
      await applyRecipe(client, r);
      setMessage(`"${r.name}" 적용됨`);
    } catch (e) {
      setMessage(`적용 실패: ${e instanceof Error ? e.message : String(e)}`);
    } finally {
      setBusy(null);
    }
  };

  return (
    <View style={styles.screen}>
      <View style={styles.searchWrap}>
        <Ionicons name="search" size={16} color={colors.textDim} />
        <TextInput
          style={styles.search}
          value={query}
          onChangeText={setQuery}
          placeholder="레시피 검색"
          placeholderTextColor={colors.textDim}
          autoCorrect={false}
          clearButtonMode="while-editing"
        />
      </View>
      {message ? <Text style={styles.message}>{message}</Text> : null}
      {!connected ? <Text style={styles.offline}>카메라 연결 안 됨 — 편집·가져오기는 되고, 적용은 연결 후에 돼요</Text> : null}
      <SectionList
        sections={sections}
        keyExtractor={(r) => r.id}
        stickySectionHeadersEnabled={false}
        contentContainerStyle={styles.list}
        renderSectionHeader={({ section }) => <Text style={styles.section}>{section.title}</Text>}
        renderItem={({ item }) => (
          <Pressable style={styles.item} onPress={() => router.push({ pathname: '/recipe/[id]', params: { id: item.id } })}>
            <View style={styles.itemText}>
              <View style={styles.nameRow}>
                <Text style={styles.name} numberOfLines={1}>
                  {item.name}
                </Text>
                {item.id === appliedId ? <Text style={styles.badge}>적용됨</Text> : null}
              </View>
              <Text style={styles.summary} numberOfLines={1}>
                {recipeSummary(item)}
              </Text>
            </View>
            {!item.builtin ? (
              <Pressable hitSlop={8} accessibilityLabel="즐겨찾기" onPress={() => toggleFavorite(item.id)}>
                <Ionicons name={item.favorite ? 'star' : 'star-outline'} size={20} color={item.favorite ? colors.accent : colors.textDim} />
              </Pressable>
            ) : null}
            <Pressable
              style={[styles.apply, (!connected || busy !== null) && styles.disabled]}
              disabled={!connected || busy !== null}
              accessibilityLabel={`${item.name} 적용`}
              onPress={() => apply(item)}>
              <Ionicons name={busy === item.id ? 'hourglass-outline' : 'color-wand-outline'} size={18} color="#000" />
            </Pressable>
          </Pressable>
        )}
        ListEmptyComponent={<Text style={styles.offline}>찾는 레시피가 없어요</Text>}
      />
    </View>
  );
}

const styles = StyleSheet.create({
  screen: { flex: 1, backgroundColor: colors.background },
  headerButtons: { flexDirection: 'row', gap: 18, alignItems: 'center', marginRight: 12 },
  searchWrap: { flexDirection: 'row', alignItems: 'center', gap: 8, margin: 12, marginBottom: 4, paddingHorizontal: 12, height: 38, borderRadius: 10, backgroundColor: colors.panel },
  search: { flex: 1, color: colors.text, fontSize: 15 },
  message: { color: colors.accent, paddingHorizontal: 16, paddingTop: 6 },
  offline: { color: colors.textDim, paddingHorizontal: 16, paddingTop: 6, fontSize: 13 },
  list: { paddingHorizontal: 12, paddingBottom: 24 },
  section: { color: colors.textDim, fontSize: 13, fontWeight: '600', marginTop: 16, marginBottom: 6, marginLeft: 4 },
  item: { flexDirection: 'row', alignItems: 'center', gap: 12, backgroundColor: colors.panel, borderRadius: 12, padding: 12, marginBottom: 6 },
  itemText: { flex: 1 },
  nameRow: { flexDirection: 'row', alignItems: 'center', gap: 8 },
  name: { color: colors.text, fontSize: 16, fontWeight: '600', flexShrink: 1 },
  badge: { color: '#000', backgroundColor: colors.ok, fontSize: 11, fontWeight: '700', paddingHorizontal: 6, paddingVertical: 1, borderRadius: 4, overflow: 'hidden' },
  summary: { color: colors.textDim, fontSize: 12, marginTop: 3 },
  apply: { width: 36, height: 36, borderRadius: 18, backgroundColor: colors.accent, alignItems: 'center', justifyContent: 'center' },
  disabled: { opacity: 0.35 },
});
