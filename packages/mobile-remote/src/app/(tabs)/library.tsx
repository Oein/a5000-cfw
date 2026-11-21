import Ionicons from '@expo/vector-icons/Ionicons';
import { Image } from 'expo-image';
import { router, useNavigation } from 'expo-router';
import { useLayoutEffect } from 'react';
import { ActivityIndicator, FlatList, Pressable, StyleSheet, Text, useWindowDimensions, View } from 'react-native';

import { useCamera } from '@/lib/CameraProvider';
import { usePrefs } from '@/lib/prefs';
import { colors } from '@/lib/theme';
import { syncOnce, useSyncState } from '@/features/sync/syncEngine';

const GAP = 2;

function ago(t: number | null) {
  if (!t) return '아직 안 함';
  const s = Math.round((Date.now() - t) / 1000);
  return s < 60 ? `${s}초 전` : s < 3600 ? `${Math.round(s / 60)}분 전` : `${Math.round(s / 3600)}시간 전`;
}

/**
 * The phone's synced copy of the camera's photos (feature 2): works with the camera off or out of
 * range. The camera gallery tab shows what's on the card right now; this shows what the phone has.
 */
export default function LibraryScreen() {
  const navigation = useNavigation();
  const { width } = useWindowDimensions();
  const { client, connected } = useCamera();
  const prefs = usePrefs();
  const { status, items } = useSyncState();
  const columns = Math.max(3, Math.round(width / 125));
  const size = (width - GAP * (columns - 1)) / columns;

  useLayoutEffect(() => {
    navigation.setOptions({
      headerRight: () =>
        status.running ? (
          <ActivityIndicator style={styles.headerButton} color={colors.accent} />
        ) : (
          <Pressable
            style={styles.headerButton}
            accessibilityLabel="지금 동기화"
            hitSlop={8}
            disabled={!connected}
            onPress={() => syncOnce(client)}>
            <Ionicons name="sync" size={22} color={connected ? colors.accent : colors.textDim} />
          </Pressable>
        ),
    });
  }, [navigation, status.running, connected, client]);

  return (
    <View style={styles.screen}>
      <View style={styles.status}>
        <Text style={styles.statusText}>
          {prefs.syncLevel === 'off'
            ? '동기화 꺼짐 (설정에서 켜기)'
            : `${items.length}장 · 마지막 동기화 ${ago(status.lastRun)}${status.pending > 0 ? ` · ${status.pending}장 남음` : ''}`}
        </Text>
        {status.lastError ? <Text style={styles.error}>{status.lastError}</Text> : null}
      </View>
      <FlatList
        data={items}
        key={columns}
        numColumns={columns}
        keyExtractor={(i) => i.key}
        columnWrapperStyle={columns > 1 ? { gap: GAP } : undefined}
        contentContainerStyle={{ gap: GAP }}
        renderItem={({ item }) => (
          <Pressable onPress={() => router.push({ pathname: '/library/[key]', params: { key: item.key } })}>
            <Image source={item.thumb ? { uri: item.thumb } : null} style={{ width: size, height: size, backgroundColor: colors.panel }} contentFit="cover" />
            {item.savedToPhotos ? <Ionicons name="checkmark-circle" size={13} color={colors.ok} style={styles.saved} /> : null}
          </Pressable>
        )}
        ListEmptyComponent={
          <Text style={styles.empty}>
            {connected ? '아직 받은 사진이 없어요. 카메라로 찍으면 자동으로 들어와요.' : '카메라에 연결되면 사진을 받아와요. 받은 사진은 카메라 없이도 볼 수 있어요.'}
          </Text>
        }
      />
    </View>
  );
}

const styles = StyleSheet.create({
  screen: { flex: 1, backgroundColor: colors.background },
  headerButton: { marginRight: 14 },
  status: { paddingHorizontal: 14, paddingVertical: 8, gap: 2 },
  statusText: { color: colors.textDim, fontSize: 13 },
  error: { color: colors.danger, fontSize: 12 },
  empty: { color: colors.textDim, textAlign: 'center', padding: 32, lineHeight: 20 },
  saved: { position: 'absolute', right: 4, bottom: 4 },
});
