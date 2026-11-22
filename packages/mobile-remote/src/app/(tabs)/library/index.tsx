import Ionicons from '@expo/vector-icons/Ionicons';
import { Image } from 'expo-image';
import { router } from 'expo-router';
import { useMemo } from 'react';
import { Pressable, RefreshControl, SectionList, StyleSheet, Text, useWindowDimensions, View } from 'react-native';

import { useCamera } from '@/lib/CameraProvider';
import { usePrefs } from '@/lib/prefs';
import { colors } from '@/lib/theme';
import { toast } from '@/lib/toast';
import { syncOnce, useSyncState, type SyncedItem } from '@/features/sync/syncEngine';

const GAP = 2;
const WEEKDAYS = ['일', '월', '화', '수', '목', '금', '토'];

function ago(t: number | null) {
  if (!t) return '아직 안 함';
  const s = Math.round((Date.now() - t) / 1000);
  return s < 60 ? '방금' : s < 3600 ? `${Math.round(s / 60)}분 전` : `${Math.round(s / 3600)}시간 전`;
}

/** Camera dates are its wall clock read as UTC → group by the UTC calendar day. */
function dayTitle(t: number) {
  const d = new Date(t);
  return `${d.getUTCFullYear()}.${d.getUTCMonth() + 1}.${d.getUTCDate()} (${WEEKDAYS[d.getUTCDay()]})`;
}

/**
 * The phone's synced copy of the camera's photos (feature 2), grouped by day: works with the
 * camera off or out of range. Pull down to sync now.
 */
export default function LibraryScreen() {
  const { width } = useWindowDimensions();
  const { client, connected } = useCamera();
  const prefs = usePrefs();
  const { status, items } = useSyncState();
  const columns = Math.max(3, Math.round(width / 125));
  const size = (width - GAP * (columns - 1)) / columns;

  const sections = useMemo(() => {
    const byDay = new Map<string, SyncedItem[]>();
    for (const item of items) {
      const key = dayTitle(item.date);
      byDay.set(key, [...(byDay.get(key) ?? []), item]);
    }
    return [...byDay].map(([title, list]) => {
      const rows: SyncedItem[][] = [];
      for (let i = 0; i < list.length; i += columns) rows.push(list.slice(i, i + columns));
      return { title, count: list.length, data: rows };
    });
  }, [items, columns]);

  const refresh = async () => {
    if (!connected) {
      toast('카메라에 연결되면 동기화돼요', 'info');
      return;
    }
    const result = await syncOnce(client);
    toast(result || '최신 상태', 'info');
  };

  const statusLine =
    prefs.syncLevel === 'off'
      ? '동기화 꺼짐 — 설정에서 켤 수 있어요'
      : status.running
        ? '동기화 중…'
        : `${items.length}장 · 마지막 동기화 ${ago(status.lastRun)}${status.pending > 0 ? ` · ${status.pending}장 남음` : ''}`;

  return (
    <SectionList
      style={styles.screen}
      sections={sections}
      keyExtractor={(row) => row[0].key}
      stickySectionHeadersEnabled
      contentInsetAdjustmentBehavior="automatic"
      refreshControl={<RefreshControl refreshing={status.running} onRefresh={refresh} tintColor={colors.accent} />}
      ListHeaderComponent={
        <View style={styles.status}>
          <Ionicons name={connected ? 'cloud-done-outline' : 'cloud-offline-outline'} size={16} color={connected ? colors.ok : colors.textDim} />
          <Text style={styles.statusText}>{statusLine}</Text>
          {status.lastError ? <Text style={styles.error}>{status.lastError}</Text> : null}
        </View>
      }
      renderSectionHeader={({ section }) => (
        <View style={styles.sectionHeader}>
          <Text style={styles.sectionTitle}>{section.title}</Text>
          <Text style={styles.sectionCount}>{section.count}장</Text>
        </View>
      )}
      renderItem={({ item: row }) => (
        <View style={[styles.row, { gap: GAP, marginBottom: GAP }]}>
          {row.map((item) => (
            <Pressable key={item.key} onPress={() => router.push({ pathname: '/library/[key]', params: { key: item.key } })} accessibilityLabel={`사진 ${item.id}`}>
              <Image source={item.thumb ? { uri: item.thumb } : null} style={{ width: size, height: size, backgroundColor: colors.panel }} contentFit="cover" />
              {item.savedToPhotos ? <Ionicons name="checkmark-circle" size={14} color={colors.ok} style={styles.saved} /> : null}
            </Pressable>
          ))}
        </View>
      )}
      ListEmptyComponent={
        <View style={styles.empty}>
          <Ionicons name="albums-outline" size={42} color={colors.textDim} />
          <Text style={styles.emptyTitle}>{connected ? '아직 받은 사진이 없어요' : '카메라 없이도 보는 보관함'}</Text>
          <Text style={styles.emptyText}>
            {connected
              ? '카메라로 찍으면 자동으로 들어와요. 아래로 당기면 지금 동기화해요.'
              : '카메라에 연결되면 새 사진을 받아오고, 받은 사진은 카메라가 꺼져 있어도 여기서 볼 수 있어요.'}
          </Text>
        </View>
      }
    />
  );
}

const styles = StyleSheet.create({
  screen: { flex: 1, backgroundColor: colors.background },
  status: { flexDirection: 'row', alignItems: 'center', flexWrap: 'wrap', gap: 6, paddingHorizontal: 14, paddingVertical: 10 },
  statusText: { color: colors.textDim, fontSize: 13 },
  error: { color: colors.danger, fontSize: 12, width: '100%' },
  sectionHeader: { flexDirection: 'row', justifyContent: 'space-between', alignItems: 'baseline', paddingHorizontal: 14, paddingVertical: 8, backgroundColor: 'rgba(0,0,0,0.92)' },
  sectionTitle: { color: colors.text, fontSize: 15, fontWeight: '700' },
  sectionCount: { color: colors.textDim, fontSize: 12 },
  row: { flexDirection: 'row' },
  saved: { position: 'absolute', right: 4, bottom: 4 },
  empty: { alignItems: 'center', padding: 40, gap: 10 },
  emptyTitle: { color: colors.text, fontSize: 17, fontWeight: '600' },
  emptyText: { color: colors.textDim, textAlign: 'center', lineHeight: 20 },
});
