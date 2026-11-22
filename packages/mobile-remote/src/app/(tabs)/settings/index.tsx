import Ionicons from '@expo/vector-icons/Ionicons';
import { useState } from 'react';
import { Alert, Pressable, ScrollView, StyleSheet, Text, TextInput, View } from 'react-native';
import { useSafeAreaInsets } from 'react-native-safe-area-context';

import { LocationTrackSettings, SessionSettings, SyncSettings } from '@/components/BackgroundSettings';
import { ActionRow, Chips, Help, Row, Section } from '@/components/Form';
import { DEFAULT_HOST } from '@/lib/camera';
import { useCamera } from '@/lib/CameraProvider';
import { CACHE_SIZES_MB, clearThumbCache, setCacheLimitMB, useCacheInfo } from '@/lib/thumbCache';
import { colors } from '@/lib/theme';
import { toast } from '@/lib/toast';
import { useSession } from '@/features/session/session';

function formatSize(bytes: number) {
  return bytes >= 1024 * 1024 * 1024 ? `${(bytes / 1024 / 1024 / 1024).toFixed(1)} GB` : `${(bytes / 1024 / 1024).toFixed(1)} MB`;
}
const sizeLabel = (mb: number) => (mb >= 1000 ? `${mb / 1000}GB` : `${mb}MB`);

/** Settings: connection first, then the background session, sync, location record, storage. */
export default function SettingsScreen() {
  const { host, setHost, connected, info } = useCamera();
  const session = useSession();
  const [draft, setDraft] = useState(host);
  const [editing, setEditing] = useState(false);
  // The saved address loads after the first render (and can change elsewhere): show it unless typing
  const [shownHost, setShownHost] = useState(host);
  if (host !== shownHost) {
    setShownHost(host);
    if (!editing) setDraft(host);
  }
  const cache = useCacheInfo();
  // In landscape the notch is at a side: keep the content clear of it
  const insets = useSafeAreaInsets();
  const apply = (h: string) => {
    const next = h.trim() || DEFAULT_HOST;
    setDraft(next);
    setHost(next);
    toast(`${next}에 연결하는 중…`, 'info');
  };

  return (
    <ScrollView
      style={styles.screen}
      contentContainerStyle={[styles.content, { paddingLeft: 16 + insets.left, paddingRight: 16 + insets.right }]}
      keyboardShouldPersistTaps="handled"
      contentInsetAdjustmentBehavior="automatic">
      {/* Connection card */}
      <View style={[styles.conn, { borderColor: connected ? 'rgba(62,207,110,0.5)' : 'rgba(255,77,77,0.4)' }]}>
        <View style={styles.connHead}>
          <Ionicons name={connected ? 'camera' : 'camera-outline'} size={26} color={connected ? colors.ok : colors.textDim} />
          <View style={{ flex: 1 }}>
            <Text style={styles.connTitle}>{connected ? (info?.model ?? '카메라') : '카메라 연결 안 됨'}</Text>
            <Text style={styles.connDetail}>
              {connected
                ? `펌웨어 ${info?.firmware} · 앱 ${info?.appVersion}${session.battery >= 0 ? ` · 배터리 ${session.battery}%` : ''}`
                : '카메라 Wi-Fi에 폰을 연결하고 주소를 확인하세요'}
            </Text>
          </View>
        </View>
        <View style={styles.hostRow}>
          <TextInput
            style={styles.input}
            value={draft}
            onChangeText={setDraft}
            onSubmitEditing={() => apply(draft)}
            onFocus={() => setEditing(true)}
            onBlur={() => setEditing(false)}
            autoCapitalize="none"
            autoCorrect={false}
            keyboardType="numbers-and-punctuation"
            returnKeyType="done"
            placeholder={DEFAULT_HOST}
            placeholderTextColor={colors.textDim}
            accessibilityLabel="카메라 주소"
          />
          <Pressable style={[styles.connect, draft.trim() === host && connected && styles.connectDone]} onPress={() => apply(draft)}>
            <Text style={[styles.connectText, draft.trim() === host && connected && { color: colors.ok }]}>{draft.trim() === host && connected ? '연결됨' : '연결'}</Text>
          </Pressable>
        </View>
        <Chips values={[DEFAULT_HOST, '127.0.0.1']} value={host} onChange={apply} label={(h) => (h === DEFAULT_HOST ? `카메라 AP ${h}` : `mock ${h}`)} />
        <Help summary="AP 모드: 폰을 카메라 Wi-Fi에 연결 → 192.168.122.1.">
          Station 모드(집 Wi-Fi)면 카메라 화면 위쪽에 나오는 주소를 입력하세요. 개발 중 mock 카메라는 127.0.0.1 (npm run mock).
        </Help>
      </View>

      <SessionSettings />
      <SyncSettings />
      <LocationTrackSettings />

      <Section title="저장 공간">
        <Row label="갤러리 썸네일 캐시" detail={`${formatSize(cache.used)} / ${sizeLabel(cache.limitMB)} — 넘으면 오래 안 본 것부터 지움`} />
        <Chips values={CACHE_SIZES_MB} value={cache.limitMB} onChange={setCacheLimitMB} label={sizeLabel} />
        <ActionRow
          label="썸네일 캐시 비우기"
          danger
          onPress={() =>
            Alert.alert('썸네일 캐시를 비울까요?', '다시 볼 때 카메라에서 받아요.', [
              { text: '취소', style: 'cancel' },
              { text: '비우기', style: 'destructive', onPress: () => (clearThumbCache(), toast('캐시를 비웠어요', 'info')) },
            ])
          }
        />
      </Section>
    </ScrollView>
  );
}

const styles = StyleSheet.create({
  screen: { flex: 1, backgroundColor: colors.background },
  content: { paddingTop: 16, gap: 8, paddingBottom: 40 },
  conn: { backgroundColor: colors.panel, borderRadius: 14, borderCurve: 'continuous', padding: 14, gap: 8, borderWidth: 1 },
  connHead: { flexDirection: 'row', alignItems: 'center', gap: 12 },
  connTitle: { color: colors.text, fontSize: 18, fontWeight: '700' },
  connDetail: { color: colors.textDim, fontSize: 12, marginTop: 2 },
  hostRow: { flexDirection: 'row', gap: 8, marginTop: 4 },
  input: {
    flex: 1,
    height: 44,
    borderRadius: 10, borderCurve: 'continuous',
    backgroundColor: colors.raised,
    color: colors.text,
    fontSize: 16,
    paddingHorizontal: 12,
    fontVariant: ['tabular-nums'],
  },
  connect: { height: 44, borderRadius: 10, borderCurve: 'continuous', backgroundColor: colors.accent, paddingHorizontal: 18, justifyContent: 'center' },
  connectDone: { backgroundColor: colors.raised },
  connectText: { color: '#000', fontWeight: '700' },
});
