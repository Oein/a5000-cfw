import { useState } from 'react';
import { Alert, Linking, Platform } from 'react-native';

import { ActionRow, Chips, Help, Row, Section, ToggleRow } from '@/components/Form';
import { setPrefs, usePrefs, type SyncLevel } from '@/lib/prefs';
import { toast } from '@/lib/toast';
import { requestLocationPermissions } from '@/features/geotag/locationTask';
import { clearTrack, trackSummary, type TrackSummary } from '@/features/geotag/track';
import { resetCounters, showLiveActivityNow, useSession, type SessionState } from '@/features/session/session';
import { setBackgroundSync } from '@/features/sync/backgroundTask';
import { clearLibrary, libraryBytes, useSyncState } from '@/features/sync/syncEngine';

const mb = (b: number) => `${(b / 1024 / 1024).toFixed(1)} MB`;

function readSummary(): TrackSummary | null {
  try {
    return trackSummary();
  } catch {
    return null;
  }
}

function offsetText(ms: number | null) {
  if (ms === null) return '아직 측정 안 됨 (카메라에 한 번 연결하면 측정)';
  const h = ms / 3600_000;
  const whole = Math.round(h * 4) / 4;
  const drift = Math.round((ms - whole * 3600_000) / 1000);
  return `카메라 = UTC${whole >= 0 ? '+' : ''}${whole}시간${drift ? `, ${drift > 0 ? '+' : ''}${drift}초 어긋남` : ''}`;
}

const LINK_TEXT: Record<SessionState['link'], string> = {
  off: '꺼짐',
  connecting: '카메라 찾는 중',
  linked: '연결됨',
  lost: '끊김 — 재연결 중',
};

function locationText(s: SessionState) {
  if (s.locationMode === 'linked') return '정밀 (카메라 연결 중)';
  if (s.locationMode === 'standby') return '저전력 대기 (카메라 없음)';
  return s.appActive ? '사진 찍을 때만 확인 (앱 화면 켜져 있을 때)' : '꺼짐';
}

const LA_TEXT: Record<SessionState['liveActivity'], string> = {
  off: '꺼짐',
  running: '표시 중',
  unavailable: '이 기기/빌드에서 사용 불가',
  disabled: 'iOS 설정에서 실시간 현황이 꺼져 있어요',
  'needs-foreground': '앱을 한 번 열면 표시돼요',
};

/** Features 1 + 2 in the background: the camera session (GPS answers, sync, Live Activity). */
export function SessionSettings() {
  const prefs = usePrefs();
  const session = useSession();

  const toggleBackground = async (on: boolean) => {
    if (on) {
      const result = await requestLocationPermissions();
      if (!result.ok) {
        Alert.alert('백그라운드 연동을 켤 수 없어요', result.reason, [
          { text: '닫기', style: 'cancel' },
          { text: '설정 열기', onPress: () => Linking.openSettings() },
        ]);
        return;
      }
    }
    await setPrefs({ backgroundSession: on });
    toast(on ? '화면을 꺼도 카메라와 계속 연동해요' : '앱 화면이 켜져 있을 때만 연동해요', 'info');
  };

  const last = session.lastAnswer;
  return (
    <>
      <Section title="카메라 연동 · 위치">
        <ToggleRow
          label="화면 꺼도 계속 연동"
          detail={
            prefs.backgroundSession
              ? session.backgroundReady
                ? `켜짐 · ${LINK_TEXT[session.link]}`
                : '위치 권한 "항상"이 필요해요 — 눌러서 다시 요청'
              : '앱 화면이 켜져 있을 때만'
          }
          value={prefs.backgroundSession}
          onChange={toggleBackground}
        />
        <Row label="위치" detail={locationText(session)} />
        <Row
          label="이번 연결"
          detail={`위치 보낸 사진 ${session.tagged}장${session.untagged ? ` · 위치 없음 ${session.untagged}장` : ''}${last ? ` · 마지막 ${last.source === 'live' ? '실시간' : last.source === 'track' ? '기록' : '없음'}${last.accuracy !== null ? ` ±${Math.round(last.accuracy)}m` : ''}` : ''}`}
        />
        {Platform.OS === 'ios' ? (
          <ToggleRow
            label="잠금 화면 실시간 현황"
            detail={LA_TEXT[session.liveActivity]}
            value={prefs.liveActivity}
            onChange={async (v) => {
              await setPrefs({ liveActivity: v });
              if (v) showLiveActivityNow();
            }}
          />
        ) : null}
        <ActionRow label="카운터 초기화" onPress={() => (resetCounters(), toast('초기화했어요', 'info'))} />
      </Section>
      <Help summary="사진을 찍으면 카메라가 위치를 묻고, 카메라가 직접 EXIF에 써요.">
        폰은 위치만 알려줘요. 연결이 끊긴 동안 찍은 사진은 다시 연결될 때 카메라가 촬영 시각과 함께 물어보고, 폰은 위치 기록에서 그 시각의 위치로 답해요.
        {'\n'}배터리: 카메라가 연결돼 있을 때만 정밀 위치를 쓰고, 끊긴 지 3분이 지나면 저전력으로 바뀌어요. 라이브뷰는 촬영 탭이 화면에 있을 때만 받아요.
      </Help>
    </>
  );
}

const LEVELS: SyncLevel[] = ['off', 'thumbs', 'previews', 'originals'];
const LEVEL_LABEL: Record<SyncLevel, string> = { off: '끔', thumbs: '썸네일', previews: '미리보기', originals: '원본' };
const LEVEL_DETAIL: Record<SyncLevel, string> = {
  off: '사진을 받지 않아요',
  thumbs: '작은 썸네일만 (장당 몇 KB) — 가장 가벼움',
  previews: '화면용 미리보기까지 — 보관함에서 크게 보기',
  originals: '원본 JPEG까지 — 데이터·시간이 가장 많이 듦',
};

/** Feature 2: what the photo sync copies. */
export function SyncSettings() {
  const prefs = usePrefs();
  const { items } = useSyncState();
  const setLevel = async (syncLevel: SyncLevel) => {
    await setPrefs({ syncLevel, autoSaveToPhotos: syncLevel === 'originals' ? prefs.autoSaveToPhotos : false });
    await setBackgroundSync(syncLevel !== 'off').catch(() => {});
  };
  return (
    <>
      <Section title="사진 동기화">
        <Row label="받아올 것" detail={LEVEL_DETAIL[prefs.syncLevel]} />
        <Chips values={LEVELS} value={prefs.syncLevel} onChange={setLevel} label={(l) => LEVEL_LABEL[l]} />
        {prefs.syncLevel === 'originals' ? (
          <ToggleRow label="새 사진을 사진 앱에 자동 저장" detail="원본 JPEG (위치는 카메라가 넣은 것)" value={prefs.autoSaveToPhotos} onChange={(v) => setPrefs({ autoSaveToPhotos: v })} />
        ) : null}
        <Row label="폰에 있는 사본" detail={`${items.length}장 · ${mb(libraryBytes())}`} />
        <ActionRow
          label="폰 사본 지우기"
          danger
          onPress={() =>
            Alert.alert('폰에 받은 사본을 지울까요?', '카메라의 사진과 사진 앱에 저장한 사진은 그대로예요.', [
              { text: '취소', style: 'cancel' },
              { text: '지우기', style: 'destructive', onPress: () => (clearLibrary(), toast('사본을 지웠어요', 'info')) },
            ])
          }
        />
      </Section>
      <Help summary="카메라가 새 사진을 알려줄 때만 받아요 (폴링 없음).">
        백그라운드에서는 한 번에 조금씩(8장·20초) 받아서 폰·카메라 모두 부담이 적어요. 남은 건 다음에 이어서 받아요.
      </Help>
    </>
  );
}

/** The phone's location record (answers for shots taken while disconnected). */
export function LocationTrackSettings() {
  const prefs = usePrefs();
  const [summary, setSummary] = useState<TrackSummary | null>(readSummary);
  return (
    <>
      <Section title="위치 기록">
        <Row label="카메라 시계" detail={offsetText(prefs.clockOffsetMs)} />
        <Row label="기록" detail={summary ? `${summary.days}일 · ${summary.points}개 지점 · ${mb(summary.bytes)}` : '없음'} />
        <Row label="보관 기간" />
        <Chips values={[1, 3, 7, 14, 30]} value={prefs.trackDays} onChange={(trackDays) => setPrefs({ trackDays })} label={(d) => `${d}일`} />
        <ActionRow
          label="위치 기록 지우기"
          danger
          onPress={() =>
            Alert.alert('위치 기록을 모두 지울까요?', '이미 카메라가 사진에 쓴 위치는 그대로예요.', [
              { text: '취소', style: 'cancel' },
              { text: '지우기', style: 'destructive', onPress: () => (clearTrack(), setSummary(readSummary()), toast('위치 기록을 지웠어요', 'info')) },
            ])
          }
        />
      </Section>
      <Help summary="위치 기록은 폰에만 있어요. 카메라에는 사진 한 장의 위치만 보내요." />
    </>
  );
}
