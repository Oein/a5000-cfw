import { Image } from 'expo-image';
import { useLocalSearchParams } from 'expo-router';
import { useState } from 'react';
import { StyleSheet, Text, View } from 'react-native';
import { useSafeAreaInsets } from 'react-native-safe-area-context';

import { Button } from '@/components/Form';
import { useCamera } from '@/lib/CameraProvider';
import { canSaveToPhotos, saveOriginal } from '@/lib/save';
import { colors } from '@/lib/theme';
import { useSyncState } from '@/features/sync/syncEngine';

/** One synced photo: the phone's copy (preview if synced, else the thumbnail). */
export default function LibraryItemScreen() {
  const { key } = useLocalSearchParams<{ key: string }>();
  const insets = useSafeAreaInsets();
  const { client, connected } = useCamera();
  const { items } = useSyncState();
  const item = items.find((i) => i.key === key);
  const [message, setMessage] = useState<string | null>(null);
  if (!item) return <Text style={styles.missing}>사진이 없어요</Text>;
  const uri = item.preview ?? item.thumb;
  // Photo dates are the camera's wall clock read as UTC: show them in UTC to show that wall clock
  const date = new Date(item.date).toISOString().slice(0, 19).replace('T', ' ');

  const save = async () => {
    setMessage(null);
    try {
      if (!(await canSaveToPhotos())) return setMessage('사진 보관함 접근 권한이 필요해요');
      await saveOriginal(client, item);
      setMessage('원본을 사진 앱에 저장했어요');
    } catch (e) {
      setMessage(`저장 실패: ${String(e)} — 카메라에서 지워졌을 수 있어요`);
    }
  };

  return (
    <View style={[styles.screen, { paddingBottom: insets.bottom + 12 }]}>
      <Image source={uri ? { uri } : null} style={styles.image} contentFit="contain" />
      <View style={styles.info}>
        <Text style={styles.text}>{date}</Text>
        <Text style={styles.dim}>{item.preview ? '미리보기 사본' : '썸네일 사본'} · 카메라 사진 #{item.id}</Text>
        {message ? <Text style={styles.message}>{message}</Text> : null}
        <Button title="원본 받아서 사진 앱에 저장" onPress={save} disabled={!connected} />
      </View>
    </View>
  );
}

const styles = StyleSheet.create({
  screen: { flex: 1, backgroundColor: colors.background },
  image: { flex: 1 },
  info: { padding: 16, gap: 6 },
  text: { color: colors.text, fontSize: 15 },
  dim: { color: colors.textDim, fontSize: 13 },
  message: { color: colors.accent, fontSize: 13 },
  missing: { color: colors.textDim, padding: 32, textAlign: 'center' },
});
