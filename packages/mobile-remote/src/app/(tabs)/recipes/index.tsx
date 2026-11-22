import Ionicons from '@expo/vector-icons/Ionicons';
import { router, useNavigation } from 'expo-router';
import { useLayoutEffect, useMemo, useState } from 'react';
import { Alert, Pressable, ScrollView, SectionList, Share, StyleSheet, Text, View } from 'react-native';
import { useSafeAreaInsets } from 'react-native-safe-area-context';

import { Glass } from '@/components/Glass';
import { Swatch } from '@/components/recipe/Controls';
import { RecipeRow, SectionHeader } from '@/components/recipe/Library';
import { showActions } from '@/lib/actionSheet';
import { useCamera } from '@/lib/CameraProvider';
import { useHideTabBar } from '@/lib/tabBar';
import { colors } from '@/lib/theme';
import { toast } from '@/lib/toast';
import { applyWithFeedback, confirmDelete, newFromCamera, openDuplicate, openEditor, openShare } from '@/features/recipes/actions';
import { buildSections, displayName, groupKeys, groupLabel, groupOf, type Section } from '@/features/recipes/groups';
import { toShareLink } from '@/features/recipes/shareCode';
import {
  deleteRecipes,
  markApplied,
  plainRecipe,
  setAllCollapsed,
  setFavorites,
  setGroupCollapsed,
  setGroupHidden,
  toggleFavorite,
  useRecipes,
  type StoredRecipe,
} from '@/features/recipes/store';
import { swatchColors } from '@/features/recipes/swatch';

/**
 * Recipe library (feature 3), built to stay manageable with 90+ recipes:
 * - groups (최근 · 즐겨찾기 · 내 레시피 · 기본 · each film brand) that fold, remembered;
 * - scope chips to look at one group only; search over name/brand/style/effect from anywhere;
 * - groups can be hidden from the quick switch (they sink to the bottom here);
 * - select mode for favouriting, sharing or deleting many at once.
 * Tap a recipe to open the editor (live preview), the button to apply it, hold for more.
 */
export default function RecipesScreen() {
  const navigation = useNavigation();
  const insets = useSafeAreaInsets();
  const { client, connected } = useCamera();
  const { recipes, appliedId, recentIds, collapsed, hidden } = useRecipes();
  const [query, setQuery] = useState('');
  const [scope, setScope] = useState<string>('all');
  const [busy, setBusy] = useState<string | null>(null);
  const [selecting, setSelecting] = useState(false);
  const [selected, setSelected] = useState<Set<string>>(new Set());
  useHideTabBar('recipes', selecting);

  const create = () =>
    showActions('새 레시피', [
      { title: '빈 레시피에서 시작', onPress: () => router.push({ pathname: '/recipe/[id]', params: { id: 'new' } }) },
      ...(connected ? [{ title: '카메라 지금 설정으로 만들기', onPress: () => newFromCamera(client) }] : []),
      { title: '가져오기 (링크 · 파일 · 텍스트)', onPress: () => router.push('/recipe/import') },
    ]);

  const stopSelecting = () => {
    setSelecting(false);
    setSelected(new Set());
  };

  useLayoutEffect(() => {
    navigation.setOptions({
      headerSearchBarOptions: {
        placeholder: `${recipes.length}개에서 검색 — 이름·브랜드·스타일`,
        hideWhenScrolling: false,
        autoCapitalize: 'none',
        onChangeText: (e: { nativeEvent: { text: string } }) => setQuery(e.nativeEvent.text),
        onCancelButtonPress: () => setQuery(''),
      },
      headerLeft: () =>
        recipes.length > 1 ? (
          <Pressable hitSlop={10} onPress={() => (selecting ? stopSelecting() : setSelecting(true))} style={styles.headerText}>
            <Text style={styles.headerButton}>{selecting ? '완료' : '선택'}</Text>
          </Pressable>
        ) : null,
      headerRight: () =>
        selecting ? null : (
          <Pressable accessibilityLabel="새 레시피" hitSlop={10} onPress={create} style={styles.headerIcon}>
            <Ionicons name="add" size={26} color={colors.accent} />
          </Pressable>
        ),
    });
  });

  const groups = useMemo(() => groupKeys(recipes), [recipes]);
  const favCount = recipes.filter((r) => r.favorite).length;
  const scopes = ['all', ...(favCount ? ['fav'] : []), ...(recentIds.length ? ['recent'] : []), ...groups];
  const activeScope = scopes.includes(scope) ? scope : 'all';
  const scopeLabel = (s: string) =>
    s === 'all' ? '전체' : s === 'fav' ? `★ ${favCount}` : s === 'recent' ? '최근' : `${groupLabel(s)} ${recipes.filter((r) => groupOf(r) === s).length}`;

  const sections: Section<StoredRecipe>[] = useMemo(() => {
    if (query.trim() || activeScope === 'all') return buildSections(recipes, { query, recentIds, collapsed, hidden });
    const byId = new Map(recipes.map((r) => [r.id, r]));
    const items =
      activeScope === 'fav'
        ? recipes.filter((r) => r.favorite)
        : activeScope === 'recent'
          ? recentIds.map((id) => byId.get(id)).filter((r): r is StoredRecipe => !!r)
          : recipes.filter((r) => groupOf(r) === activeScope);
    return [{ key: activeScope, title: activeScope === 'fav' ? '즐겨찾기' : activeScope === 'recent' ? '최근 사용' : groupLabel(activeScope), count: items.length, collapsed: false, hidden: hidden.has(activeScope), data: items, items }];
  }, [recipes, query, activeScope, recentIds, collapsed, hidden]);

  const groupSections = sections.filter((s) => groups.includes(s.key));
  const allFolded = groupSections.length > 0 && groupSections.every((s) => s.collapsed);
  const applied = recipes.find((r) => r.id === appliedId) ?? null;
  const searching = !!query.trim();

  const apply = async (r: StoredRecipe) => {
    setBusy(r.id);
    if (await applyWithFeedback(client, r)) await markApplied(r.id);
    setBusy(null);
  };

  const toggleSelect = (ids: string[], on?: boolean) =>
    setSelected((s) => {
      const n = new Set(s);
      const turnOn = on ?? !ids.every((id) => n.has(id));
      for (const id of ids) {
        if (turnOn) n.add(id);
        else n.delete(id);
      }
      return n;
    });

  const more = (r: StoredRecipe) =>
    showActions(displayName(r), [
      ...(connected ? [{ title: '카메라에 적용', onPress: () => apply(r) }] : []),
      { title: r.builtin ? '편집 (사본으로 저장)' : '편집', onPress: () => openEditor(r) },
      { title: '복제', onPress: () => openDuplicate(r) },
      { title: '공유 (QR · 링크)', onPress: () => openShare(r) },
      { title: r.favorite ? '즐겨찾기 해제' : '즐겨찾기', onPress: () => toggleFavorite(r.id).then((on) => toast(on ? '즐겨찾기에 추가' : '즐겨찾기 해제', 'info')) },
      { title: '여러 개 선택…', onPress: () => (setSelecting(true), setSelected(new Set([r.id]))) },
      ...(!r.builtin ? [{ title: '삭제', destructive: true, onPress: () => confirmDelete(r) }] : []),
    ]);

  const sectionMenu = (s: Section<StoredRecipe>) => {
    const isGroup = groups.includes(s.key);
    showActions(`${s.title} (${s.count})`, [
      ...(isGroup && s.count > 0 ? [{ title: '이 그룹만 보기', onPress: () => setScope(s.key) }] : []),
      ...(isGroup
        ? [
            {
              title: hidden.has(s.key) ? '빠른 전환에 다시 보이기' : '빠른 전환에서 숨기기',
              onPress: () =>
                setGroupHidden(s.key, !hidden.has(s.key)).then(() =>
                  toast(hidden.has(s.key) ? `${s.title}: 다시 보여요` : `${s.title}: 빠른 전환에서 숨겼어요 (검색하면 나와요)`, 'info'),
                ),
            },
          ]
        : []),
      { title: '모두 즐겨찾기', onPress: () => setFavorites(s.items.map((r) => r.id), true).then(() => toast(`${s.items.length}개 즐겨찾기`, 'info')) },
    ]);
  };

  // ---- bulk actions
  const chosen = recipes.filter((r) => selected.has(r.id));
  const chosenMine = chosen.filter((r) => !r.builtin);
  const allFav = chosen.length > 0 && chosen.every((r) => r.favorite);
  const bulkFavorite = async () => {
    await setFavorites(chosen.map((r) => r.id), !allFav);
    toast(allFav ? `${chosen.length}개 즐겨찾기 해제` : `${chosen.length}개 즐겨찾기`, 'info');
    stopSelecting();
  };
  const bulkShare = async () => {
    const text = chosen.map((r) => `${displayName(r)}\n${toShareLink(plainRecipe(r))}`).join('\n\n');
    await Share.share({ message: text }).catch(() => {});
  };
  const bulkDelete = () =>
    Alert.alert(`내 레시피 ${chosenMine.length}개를 지울까요?`, chosen.length > chosenMine.length ? `기본 레시피 ${chosen.length - chosenMine.length}개는 지울 수 없어 건너뛰어요.` : '폰에서만 지워져요.', [
      { text: '취소', style: 'cancel' },
      {
        text: '지우기',
        style: 'destructive',
        onPress: async () => {
          const n = await deleteRecipes(chosenMine.map((r) => r.id));
          toast(`${n}개 지웠어요`, 'info');
          stopSelecting();
        },
      },
    ]);

  const header = (
    <View style={styles.headerArea}>
      {!selecting ? (
        <View style={styles.nowCard}>
          {applied ? <Swatch stops={swatchColors(applied)} size={40} radius={10} /> : <View style={styles.nowIcon}><Ionicons name="camera-outline" size={20} color={colors.textDim} /></View>}
          <View style={{ flex: 1 }}>
            <Text style={styles.nowLabel}>{connected ? '카메라에 적용된 레시피' : '카메라 연결 안 됨'}</Text>
            <Text style={styles.nowName} numberOfLines={1}>
              {applied ? displayName(applied) : '없음 (카메라 자체 설정)'}
            </Text>
          </View>
          {connected ? (
            <Pressable style={styles.nowButton} onPress={() => newFromCamera(client)} accessibilityLabel="카메라 지금 설정으로 레시피 만들기">
              <Ionicons name="download-outline" size={15} color={colors.accent} />
              <Text style={styles.nowButtonText}>지금 설정 저장</Text>
            </Pressable>
          ) : null}
        </View>
      ) : null}
      {!searching ? (
        <ScrollView horizontal showsHorizontalScrollIndicator={false} contentContainerStyle={styles.scopes}>
          {scopes.map((s) => {
            const on = s === activeScope;
            return (
              <Pressable key={s} onPress={() => setScope(s)} style={[styles.scope, on && styles.scopeOn, hidden.has(s) && !on && { opacity: 0.5 }]} accessibilityState={{ selected: on }}>
                <Text style={[styles.scopeText, on && styles.scopeTextOn]}>{scopeLabel(s)}</Text>
              </Pressable>
            );
          })}
        </ScrollView>
      ) : null}
      {activeScope === 'all' && !searching && groupSections.length > 1 ? (
        <View style={styles.toolsRow}>
          <Text style={styles.packText}>{`${groupSections.filter((g) => !g.collapsed).length}/${groupSections.length}개 그룹 펼침`}</Text>
          <Pressable hitSlop={8} onPress={() => setAllCollapsed(groups, !allFolded)}>
            <Text style={styles.link}>{allFolded ? '모두 펼치기' : '모두 접기'}</Text>
          </Pressable>
        </View>
      ) : null}
    </View>
  );

  return (
    <View style={styles.screen}>
      <SectionList
        style={styles.screen}
        contentContainerStyle={[styles.list, selecting && { paddingBottom: 110 + insets.bottom }]}
        sections={sections}
        keyExtractor={(r, i) => `${r.id}-${i}`}
        ListHeaderComponent={header}
        keyboardShouldPersistTaps="handled"
        keyboardDismissMode="on-drag"
        contentInsetAdjustmentBehavior="automatic"
        stickySectionHeadersEnabled
        renderSectionHeader={({ section }) => {
          const s = section as Section<StoredRecipe>;
          const foldable = activeScope === 'all' && s.key !== 'search';
          const sectionIds = s.items.map((r) => r.id);
          return (
            <SectionHeader
              section={s}
              preview={s.items}
              onToggle={foldable ? () => setGroupCollapsed(s.key, !s.collapsed) : undefined}
              onMenu={selecting || s.key === 'search' ? undefined : () => sectionMenu(s)}
              right={
                selecting && !s.collapsed && s.items.length ? (
                  <Pressable hitSlop={8} onPress={() => toggleSelect(sectionIds)}>
                    <Text style={styles.link}>{sectionIds.every((id) => selected.has(id)) ? '선택 해제' : '모두 선택'}</Text>
                  </Pressable>
                ) : null
              }
            />
          );
        }}
        renderItem={({ item, section }) => {
          const isApplied = item.id === appliedId;
          const mixed = ['recent', 'fav', 'search'].includes((section as Section<StoredRecipe>).key);
          return (
            <RecipeRow
              recipe={item}
              applied={isApplied && !selecting}
              selecting={selecting}
              selected={selected.has(item.id)}
              showGroup={mixed}
              onPress={() => (selecting ? toggleSelect([item.id]) : openEditor(item))}
              onLongPress={selecting ? undefined : () => more(item)}
              right={
                selecting ? null : isApplied ? (
                  <View style={styles.appliedPill}>
                    <Ionicons name="checkmark" size={14} color="#000" />
                    <Text style={styles.appliedText}>적용됨</Text>
                  </View>
                ) : (
                  <Pressable
                    style={[styles.applyPill, (!connected || busy !== null) && styles.applyOff]}
                    disabled={!connected || busy !== null}
                    hitSlop={6}
                    accessibilityLabel={`${displayName(item)} 카메라에 적용`}
                    onPress={() => apply(item)}>
                    <Text style={[styles.applyText, (!connected || busy !== null) && { color: colors.textFaint }]}>{busy === item.id ? '…' : '적용'}</Text>
                  </Pressable>
                )
              }
            />
          );
        }}
        ListEmptyComponent={
          <View style={styles.empty}>
            <Text style={styles.emptyText}>{searching ? `"${query.trim()}"에 맞는 레시피가 없어요.` : '레시피가 없어요.'}</Text>
            {!searching ? (
              <Pressable style={styles.emptyButton} onPress={create}>
                <Text style={styles.emptyButtonText}>+ 새 레시피</Text>
              </Pressable>
            ) : null}
          </View>
        }
        ListFooterComponent={
          activeScope === 'all' && !searching && !selecting ? (
            <Text style={styles.footer}>그룹 제목을 누르면 접고 펼쳐요. ⋯ 메뉴에서 그룹을 빠른 전환에서 숨길 수 있어요.</Text>
          ) : null
        }
      />
      {selecting ? (
        <Glass style={[styles.bulkBar, { paddingBottom: insets.bottom ? insets.bottom : 16 }]}>
          <Text style={styles.bulkCount}>{chosen.length}개 선택</Text>
          <View style={styles.bulkActions}>
            <BulkButton icon={allFav ? 'star' : 'star-outline'} label={allFav ? '해제' : '즐겨찾기'} disabled={!chosen.length} onPress={bulkFavorite} />
            <BulkButton icon="share-outline" label="공유" disabled={!chosen.length} onPress={bulkShare} />
            <BulkButton icon="trash-outline" label="삭제" danger disabled={!chosenMine.length} onPress={bulkDelete} />
          </View>
        </Glass>
      ) : null}
    </View>
  );
}

function BulkButton({ icon, label, onPress, disabled, danger }: { icon: keyof typeof Ionicons.glyphMap; label: string; onPress: () => void; disabled?: boolean; danger?: boolean }) {
  const color = danger ? colors.danger : colors.accent;
  return (
    <Pressable style={[styles.bulkButton, disabled && styles.disabled]} disabled={disabled} onPress={onPress} accessibilityRole="button" accessibilityLabel={label}>
      <Ionicons name={icon} size={20} color={color} />
      <Text style={[styles.bulkLabel, { color }]}>{label}</Text>
    </Pressable>
  );
}

const styles = StyleSheet.create({
  screen: { flex: 1, backgroundColor: colors.background },
  list: { paddingHorizontal: 12, paddingBottom: 24 },
  headerIcon: { width: 36, height: 36, alignItems: 'center', justifyContent: 'center' },
  headerText: { paddingHorizontal: 8 },
  headerButton: { color: colors.accent, fontSize: 16, fontWeight: '600' },
  headerArea: { gap: 8, paddingTop: 12, paddingBottom: 4 },
  nowCard: { flexDirection: 'row', alignItems: 'center', gap: 12, backgroundColor: colors.panel, borderRadius: 14, borderCurve: 'continuous', padding: 10 },
  nowIcon: { width: 40, height: 40, borderRadius: 10, borderCurve: 'continuous', backgroundColor: colors.raised, alignItems: 'center', justifyContent: 'center' },
  nowLabel: { color: colors.textDim, fontSize: 12 },
  nowName: { color: colors.text, fontSize: 16, fontWeight: '700', marginTop: 2 },
  nowButton: { flexDirection: 'row', alignItems: 'center', gap: 4, paddingHorizontal: 10, height: 30, borderRadius: 15, borderCurve: 'continuous', borderWidth: 1, borderColor: colors.accent },
  nowButtonText: { color: colors.accent, fontSize: 12, fontWeight: '700' },
  scopes: { gap: 6, paddingVertical: 2 },
  scope: { paddingHorizontal: 12, height: 30, borderRadius: 15, borderCurve: 'continuous', backgroundColor: colors.raised, justifyContent: 'center' },
  scopeOn: { backgroundColor: colors.accent },
  scopeText: { color: colors.text, fontSize: 13, fontWeight: '600' },
  scopeTextOn: { color: '#000' },
  toolsRow: { flexDirection: 'row', alignItems: 'center', justifyContent: 'space-between', gap: 8, paddingHorizontal: 4 },
  link: { color: colors.accent, fontSize: 13, fontWeight: '600' },
  applyPill: { paddingHorizontal: 14, height: 30, borderRadius: 15, borderCurve: 'continuous', backgroundColor: colors.accent, alignItems: 'center', justifyContent: 'center' },
  applyOff: { backgroundColor: colors.raised },
  applyText: { color: '#000', fontWeight: '700', fontSize: 13 },
  appliedPill: { flexDirection: 'row', alignItems: 'center', gap: 2, paddingHorizontal: 10, height: 30, borderRadius: 15, borderCurve: 'continuous', backgroundColor: colors.ok },
  appliedText: { color: '#000', fontWeight: '700', fontSize: 12 },
  disabled: { opacity: 0.35 },
  packText: { color: colors.textDim, fontSize: 12, lineHeight: 17, flexShrink: 1 },
  empty: { alignItems: 'center', padding: 32, gap: 14 },
  emptyText: { color: colors.textDim, textAlign: 'center', lineHeight: 20 },
  emptyButton: { paddingHorizontal: 18, height: 40, borderRadius: 20, borderCurve: 'continuous', backgroundColor: colors.accent, justifyContent: 'center' },
  emptyButtonText: { color: '#000', fontWeight: '700' },
  footer: { color: colors.textDim, fontSize: 12, textAlign: 'center', paddingVertical: 16, paddingHorizontal: 20, lineHeight: 17 },
  bulkBar: { position: 'absolute', left: 0, right: 0, bottom: 0, paddingTop: 10, paddingHorizontal: 16, gap: 6, overflow: 'hidden' },
  bulkCount: { color: colors.textDim, fontSize: 12, textAlign: 'center' },
  bulkActions: { flexDirection: 'row', justifyContent: 'space-around' },
  bulkButton: { alignItems: 'center', gap: 2, minWidth: 72, paddingVertical: 4 },
  bulkLabel: { fontSize: 12, fontWeight: '600' },
});
