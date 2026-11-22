import Ionicons from '@expo/vector-icons/Ionicons';
import { router, useNavigation } from 'expo-router';
import { useLayoutEffect, useMemo, useState } from 'react';
import { ActivityIndicator, Alert, FlatList, Pressable, StyleSheet, Text, TextInput, View } from 'react-native';

import { Chips } from '@/components/Form';
import { Segmented, Swatch } from '@/components/recipe/Controls';
import { showActions } from '@/lib/actionSheet';
import { useCamera } from '@/lib/CameraProvider';
import { colors } from '@/lib/theme';
import { toast } from '@/lib/toast';
import { applyWithFeedback, confirmDelete, newFromCamera, openDuplicate, openEditor, openShare } from '@/features/recipes/actions';
import { EFFECT_LABELS, formatThirds, STYLE_LABELS, type Recipe } from '@/features/recipes/model';
import { downloadRecipeLab, markApplied, packInfo, removeRecipeLab, toggleFavorite, useRecipes, type StoredRecipe } from '@/features/recipes/store';
import { swatchColors } from '@/features/recipes/swatch';

type Filter = 'all' | 'fav' | 'mine' | 'builtin' | 'lab';
const FILTER_LABEL: Record<Filter, string> = { all: '전체', fav: '★', mine: '내 것', builtin: '기본', lab: 'Lab' };
const ALL_BRANDS = '전체 브랜드';

function fetchedText(t: number) {
  const d = new Date(t);
  return `${d.getFullYear()}.${d.getMonth() + 1}.${d.getDate()} 받음`;
}

/** Up to three short tags for a row. */
function tags(r: Recipe & { brand?: string }) {
  const t = r.brand ? [r.brand, STYLE_LABELS[r.style]] : [STYLE_LABELS[r.style]];
  if (r.effect !== 'off') t.push(EFFECT_LABELS[r.effect]);
  else if (typeof r.whiteBalance === 'number') t.push(`${r.whiteBalance}K`);
  if (r.exposureThirds) t.push(`${formatThirds(r.exposureThirds)}EV`);
  else if (r.saturation) t.push(`채도${r.saturation > 0 ? '+' : ''}${r.saturation}`);
  return t.slice(0, 3);
}

/**
 * Recipe library (feature 3). Tap a recipe to open the editor (with live preview), the button to
 * apply it straight away, hold for more. The card on top says what the camera has now.
 */
export default function RecipesScreen() {
  const navigation = useNavigation();
  const { client, connected } = useCamera();
  const { recipes, appliedId } = useRecipes();
  const [query, setQuery] = useState('');
  const [filter, setFilter] = useState<Filter>('all');
  const [busy, setBusy] = useState<string | null>(null);
  const [brand, setBrand] = useState(ALL_BRANDS);
  const [downloading, setDownloading] = useState(false);
  const pack = packInfo();
  const brands = useMemo(() => [ALL_BRANDS, ...new Set(recipes.flatMap((r) => (r.brand ? [r.brand] : [])))], [recipes]);

  const download = async () => {
    setDownloading(true);
    try {
      const info = await downloadRecipeLab();
      toast(`Recipe Lab 레시피 ${info.count}개${info.skipped ? ` (${info.skipped}개는 읽지 못함)` : ''}`);
      setFilter('lab');
    } catch (e) {
      toast(`받지 못했어요: ${e instanceof Error ? e.message : String(e)}`, 'error');
    }
    setDownloading(false);
  };
  const managePack = () =>
    showActions('Recipe Lab 레시피', [
      { title: '다시 받기 (최신으로)', onPress: download },
      {
        title: '폰에서 지우기',
        destructive: true,
        onPress: () =>
          Alert.alert('Recipe Lab 레시피를 지울까요?', '복사해 저장한 내 레시피는 그대로예요. 언제든 다시 받을 수 있어요.', [
            { text: '취소', style: 'cancel' },
            { text: '지우기', style: 'destructive', onPress: () => removeRecipeLab().then(() => setFilter('all')) },
          ]),
      },
    ]);

  const create = () =>
    showActions('새 레시피', [
      { title: '빈 레시피에서 시작', onPress: () => router.push({ pathname: '/recipe/[id]', params: { id: 'new' } }) },
      ...(connected ? [{ title: '카메라 지금 설정으로 만들기', onPress: () => newFromCamera(client) }] : []),
      { title: '가져오기 (링크 · 파일 · 텍스트)', onPress: () => router.push('/recipe/import') },
      { title: pack ? 'Recipe Lab 레시피 다시 받기' : 'Recipe Lab 레시피 받기', onPress: download },
    ]);

  useLayoutEffect(() => {
    navigation.setOptions({
      headerRight: () => (
        <Pressable accessibilityLabel="새 레시피" hitSlop={10} onPress={create} style={{ marginRight: 14 }}>
          <Ionicons name="add-circle" size={26} color={colors.accent} />
        </Pressable>
      ),
    });
  });

  const list = useMemo(() => {
    const q = query.trim().toLowerCase();
    return recipes.filter((r) => {
      if (filter === 'fav' && !r.favorite) return false;
      if (filter === 'mine' && r.builtin) return false;
      if (filter === 'builtin' && (!r.builtin || r.pack)) return false;
      if (filter === 'lab' && (!r.pack || (brand !== ALL_BRANDS && r.brand !== brand))) return false;
      return !q || `${r.name} ${r.title ?? ''} ${tags(r).join(' ')}`.toLowerCase().includes(q);
    });
  }, [recipes, query, filter, brand]);

  const applied = recipes.find((r) => r.id === appliedId) ?? null;

  const apply = async (r: StoredRecipe) => {
    setBusy(r.id);
    if (await applyWithFeedback(client, r)) await markApplied(r.id);
    setBusy(null);
  };

  const more = (r: StoredRecipe) =>
    showActions(r.title ?? r.name, [
      ...(connected ? [{ title: '카메라에 적용', onPress: () => apply(r) }] : []),
      { title: r.builtin ? '편집 (사본으로 저장)' : '편집', onPress: () => openEditor(r) },
      { title: '복제', onPress: () => openDuplicate(r) },
      { title: '공유 (QR · 링크)', onPress: () => openShare(r) },
      { title: r.favorite ? '즐겨찾기 해제' : '즐겨찾기', onPress: () => toggleFavorite(r.id).then((on) => toast(on ? '즐겨찾기에 추가' : '즐겨찾기 해제', 'info')) },
      ...(!r.builtin ? [{ title: '삭제', destructive: true, onPress: () => confirmDelete(r) }] : []),
    ]);

  const header = (
    <View style={styles.headerArea}>
      <View style={styles.nowCard}>
        {applied ? <Swatch stops={swatchColors(applied)} size={44} radius={11} /> : <View style={styles.nowIcon}><Ionicons name="camera-outline" size={22} color={colors.textDim} /></View>}
        <View style={{ flex: 1 }}>
          <Text style={styles.nowLabel}>{connected ? '카메라에 적용된 레시피' : '카메라 연결 안 됨'}</Text>
          <Text style={styles.nowName} numberOfLines={1}>
            {applied ? applied.name : '없음 (카메라 자체 설정)'}
          </Text>
        </View>
        {connected ? (
          <Pressable style={styles.nowButton} onPress={() => newFromCamera(client)} accessibilityLabel="카메라 지금 설정으로 레시피 만들기">
            <Ionicons name="download-outline" size={16} color={colors.accent} />
            <Text style={styles.nowButtonText}>지금 설정 저장</Text>
          </Pressable>
        ) : null}
      </View>
      <View style={styles.searchWrap}>
        <Ionicons name="search" size={16} color={colors.textDim} />
        <TextInput style={styles.search} value={query} onChangeText={setQuery} placeholder="이름·스타일 검색" placeholderTextColor={colors.textDim} autoCorrect={false} clearButtonMode="while-editing" />
      </View>
      <Segmented values={['all', 'fav', 'mine', 'builtin', 'lab'] as Filter[]} value={filter} onChange={setFilter} label={(f) => FILTER_LABEL[f]} />
      {!pack && (filter === 'all' || filter === 'lab') ? (
        <Pressable style={styles.packCard} onPress={download} disabled={downloading} accessibilityRole="button">
          <Ionicons name="cloud-download-outline" size={26} color={colors.accent} />
          <View style={{ flex: 1 }}>
            <Text style={styles.packTitle}>Recipe Lab 레시피 받기</Text>
            <Text style={styles.packText}>Fuji·Kodak·Ricoh·Leica 등 필름 룩 70여 개. GitHub(voxivoid/recipe-lab-sony-pmca)에서 받아 폰에 보관해요.</Text>
          </View>
          {downloading ? <ActivityIndicator color={colors.accent} /> : <Ionicons name="chevron-forward" size={18} color={colors.textDim} />}
        </Pressable>
      ) : null}
      {pack && filter === 'lab' ? (
        <>
          <Chips values={brands} value={brand} onChange={setBrand} />
          <Pressable style={styles.packMeta} onPress={managePack} hitSlop={6}>
            <Text style={styles.packText}>
              {`${pack.count}개 · ${fetchedText(pack.fetchedAt)} · 원작 voxivoid/Recipe Lab (GPL-3.0)`}
            </Text>
            {downloading ? <ActivityIndicator size="small" color={colors.accent} /> : <Text style={styles.packManage}>관리</Text>}
          </Pressable>
        </>
      ) : null}
    </View>
  );

  return (
    <FlatList
      style={styles.screen}
      contentContainerStyle={styles.list}
      data={list}
      keyExtractor={(r) => r.id}
      ListHeaderComponent={header}
      keyboardShouldPersistTaps="handled"
      renderItem={({ item }) => {
        const isApplied = item.id === appliedId;
        return (
          <Pressable
            style={({ pressed }) => [styles.item, isApplied && styles.itemApplied, pressed && { opacity: 0.7 }]}
            onPress={() => openEditor(item)}
            onLongPress={() => more(item)}
            accessibilityHint="길게 누르면 메뉴">
            <Swatch stops={swatchColors(item)} size={46} radius={11} />
            <View style={styles.itemText}>
              <View style={styles.nameRow}>
                <Text style={styles.name} numberOfLines={1}>
                  {item.title ?? item.name}
                </Text>
                {item.favorite ? <Ionicons name="star" size={13} color={colors.accent} /> : null}
              </View>
              <View style={styles.tags}>
                {tags(item).map((t) => (
                  <Text key={t} style={styles.tag}>
                    {t}
                  </Text>
                ))}
              </View>
            </View>
            {isApplied ? (
              <View style={styles.appliedPill}>
                <Ionicons name="checkmark" size={14} color="#000" />
                <Text style={styles.appliedText}>적용됨</Text>
              </View>
            ) : (
              <Pressable
                style={[styles.applyPill, (!connected || busy !== null) && styles.disabled]}
                disabled={!connected || busy !== null}
                hitSlop={6}
                accessibilityLabel={`${item.name} 카메라에 적용`}
                onPress={() => apply(item)}>
                <Text style={styles.applyText}>{busy === item.id ? '…' : '적용'}</Text>
              </Pressable>
            )}
          </Pressable>
        );
      }}
      ListEmptyComponent={
        <View style={styles.empty}>
          <Text style={styles.emptyText}>
            {filter === 'fav' ? '즐겨찾기한 레시피가 없어요. 레시피를 길게 눌러 추가하세요.' : filter === 'mine' ? '아직 만든 레시피가 없어요.' : filter === 'lab' && !pack ? '' : '찾는 레시피가 없어요.'}
          </Text>
          {filter === 'mine' ? (
            <Pressable style={styles.emptyButton} onPress={create}>
              <Text style={styles.emptyButtonText}>+ 새 레시피</Text>
            </Pressable>
          ) : null}
        </View>
      }
    />
  );
}

const styles = StyleSheet.create({
  screen: { flex: 1, backgroundColor: colors.background },
  list: { paddingHorizontal: 12, paddingBottom: 24 },
  headerArea: { gap: 8, paddingTop: 12, paddingBottom: 8 },
  nowCard: { flexDirection: 'row', alignItems: 'center', gap: 12, backgroundColor: colors.panel, borderRadius: 14, padding: 12 },
  nowIcon: { width: 44, height: 44, borderRadius: 11, backgroundColor: '#1d1d20', alignItems: 'center', justifyContent: 'center' },
  nowLabel: { color: colors.textDim, fontSize: 12 },
  nowName: { color: colors.text, fontSize: 16, fontWeight: '700', marginTop: 2 },
  nowButton: { flexDirection: 'row', alignItems: 'center', gap: 4, paddingHorizontal: 10, height: 32, borderRadius: 16, borderWidth: 1, borderColor: colors.accent },
  nowButtonText: { color: colors.accent, fontSize: 12, fontWeight: '700' },
  searchWrap: { flexDirection: 'row', alignItems: 'center', gap: 8, paddingHorizontal: 12, height: 38, borderRadius: 10, backgroundColor: colors.panel },
  search: { flex: 1, color: colors.text, fontSize: 15 },
  item: { flexDirection: 'row', alignItems: 'center', gap: 12, backgroundColor: colors.panel, borderRadius: 14, padding: 10, marginBottom: 6, borderWidth: 1.5, borderColor: 'transparent' },
  itemApplied: { borderColor: colors.ok },
  itemText: { flex: 1, gap: 5 },
  nameRow: { flexDirection: 'row', alignItems: 'center', gap: 6 },
  name: { color: colors.text, fontSize: 16, fontWeight: '600', flexShrink: 1 },
  tags: { flexDirection: 'row', gap: 5, flexWrap: 'wrap' },
  tag: { color: colors.textDim, fontSize: 11, backgroundColor: '#1d1d20', paddingHorizontal: 6, paddingVertical: 2, borderRadius: 5, overflow: 'hidden' },
  applyPill: { paddingHorizontal: 14, height: 32, borderRadius: 16, backgroundColor: colors.accent, alignItems: 'center', justifyContent: 'center' },
  applyText: { color: '#000', fontWeight: '700', fontSize: 13 },
  appliedPill: { flexDirection: 'row', alignItems: 'center', gap: 2, paddingHorizontal: 10, height: 32, borderRadius: 16, backgroundColor: colors.ok },
  appliedText: { color: '#000', fontWeight: '700', fontSize: 12 },
  disabled: { opacity: 0.35 },
  packCard: { flexDirection: 'row', alignItems: 'center', gap: 12, padding: 12, borderRadius: 14, borderWidth: 1, borderColor: colors.accent, backgroundColor: colors.panel },
  packTitle: { color: colors.text, fontSize: 15, fontWeight: '700' },
  packText: { color: colors.textDim, fontSize: 12, lineHeight: 17, flexShrink: 1 },
  packMeta: { flexDirection: 'row', alignItems: 'center', justifyContent: 'space-between', gap: 8, paddingHorizontal: 4 },
  packManage: { color: colors.accent, fontSize: 13, fontWeight: '600' },
  empty: { alignItems: 'center', padding: 32, gap: 14 },
  emptyText: { color: colors.textDim, textAlign: 'center', lineHeight: 20 },
  emptyButton: { paddingHorizontal: 18, height: 40, borderRadius: 20, backgroundColor: colors.accent, justifyContent: 'center' },
  emptyButtonText: { color: '#000', fontWeight: '700' },
});
