import Ionicons from '@expo/vector-icons/Ionicons';
import { router, useLocalSearchParams, useNavigation } from 'expo-router';
import { usePreventRemove } from 'expo-router/react-navigation';
import { useLayoutEffect, useMemo, useRef, useState } from 'react';
import { Alert, Pressable, ScrollView, StyleSheet, Text, TextInput, useWindowDimensions, View } from 'react-native';
import { useSafeAreaInsets } from 'react-native-safe-area-context';

import { Chips, Stepper } from '@/components/Form';
import { LiveView } from '@/components/LiveView';
import { Segmented, Swatch, TileGrid, ValueStrip, WbPad } from '@/components/recipe/Controls';
import { showActions } from '@/lib/actionSheet';
import { useCamera } from '@/lib/CameraProvider';
import { colors } from '@/lib/theme';
import { toast, toastError } from '@/lib/toast';
import { applyWarnings } from '@/features/recipes/apply';
import { applyRecipe } from '@/features/recipes/applyToCamera';
import {
  defaultRecipe,
  EFFECT_LABELS,
  EFFECT_OPTIONS,
  EFFECTS,
  formatThirds,
  NAME_PATTERN,
  problem,
  sameLook,
  STYLE_LABELS,
  STYLES,
  type Dro,
  type Recipe,
} from '@/features/recipes/model';
import { deleteRecipe, freeName, getRecipe, markApplied, saveRecipe, useRecipes, plainRecipe } from '@/features/recipes/store';
import { swatchColors } from '@/features/recipes/swatch';
import { useLivePreview } from '@/features/recipes/useLivePreview';

type WbMode = 'auto' | 'keep' | 'kelvin';
const WB_LABEL: Record<WbMode, string> = { auto: '자동', keep: '카메라 값', kelvin: '색온도' };
const KELVIN_PRESETS = [3200, 4300, 5500, 6500, 7500];
const KELVIN_NAMES: Record<number, string> = { 3200: '백열등', 4300: '형광등', 5500: '주광', 6500: '흐림', 7500: '그늘' };
const DROS: Dro[] = ['off', 'auto', 1, 2, 3, 4, 5];
const EV_QUICK = [-3, -2, -1, 0, 1, 2, 3];
const OPTION_LABEL: Record<string, string> = {
  normal: '보통', cool: '차갑게', warm: '따뜻하게', green: '그린', magenta: '마젠타', blue: '블루', pink: '핑크',
  red: '레드', yellow: '옐로', 'posterization-color': '컬러', 'posterization-bw': '흑백',
};

function sanitizeName(name: string) {
  return name.replace(/[^A-Za-z0-9 \-.'()&+]/g, '').slice(0, 24);
}

/**
 * Recipe editor. Changes preview live on the camera (the live view on top shows them); "적용"
 * keeps them on the camera, "저장" stores the recipe. Built-in recipes are editable too — saving
 * one makes a copy. Leaving with unsaved edits asks first, and an abandoned preview puts the
 * camera's previous look back.
 */
export default function RecipeScreen() {
  const { id, draft } = useLocalSearchParams<{ id: string; draft?: string }>();
  const navigation = useNavigation();
  const insets = useSafeAreaInsets();
  const { width } = useWindowDimensions();
  const { client, connected, info } = useCamera();
  useRecipes(); // re-render when the library loads
  const stored = id === 'new' ? null : getRecipe(id);

  const initial = useMemo<Recipe>(() => {
    if (draft) {
      try {
        return JSON.parse(draft) as Recipe;
      } catch {
        // fall through
      }
    }
    if (stored) return plainRecipe(stored);
    return defaultRecipe(freeName('My Recipe'));
    // eslint-disable-next-line react-hooks/exhaustive-deps
  }, [id, draft, stored?.id]);

  const [recipe, setRecipe] = useState<Recipe>(initial);
  const [saved, setSaved] = useState<Recipe>(initial);
  const [previewOn, setPreviewOn] = useState(true);
  const [busy, setBusy] = useState(false);
  const isBuiltin = stored?.builtin ?? false;
  const isNew = !stored || !!draft;
  const dirty = isNew || !sameLook(recipe, saved) || recipe.name !== saved.name;
  const invalid = problem(recipe);
  const preview = useLivePreview(client, connected, recipe, previewOn);
  const leaving = useRef(false);
  // While a control is being dragged (WB pad) the page must not scroll or swipe back; with unsaved
  // changes, swipe-back is off too (the back button still asks what to do with them).
  const [dragging, setDragging] = useState(false);

  useLayoutEffect(() => {
    navigation.setOptions({
      gestureEnabled: !dirty && !dragging,
      fullScreenGestureEnabled: false,
      title: isNew ? '새 레시피' : '레시피 편집',
      headerRight: () => (
        <Pressable hitSlop={10} onPress={more} accessibilityLabel="더 보기" style={{ marginRight: 4 }}>
          <Ionicons name="ellipsis-horizontal" size={22} color={colors.text} />
        </Pressable>
      ),
    });
  });

  usePreventRemove(dirty && !leaving.current, ({ data }) => {
    Alert.alert('저장하지 않은 변경', isBuiltin ? '기본 레시피는 사본으로 저장돼요.' : undefined, [
      { text: '계속 편집', style: 'cancel' },
      {
        text: '버리기',
        style: 'destructive',
        onPress: () => {
          leaving.current = true;
          navigation.dispatch(data.action);
        },
      },
      {
        text: '저장',
        onPress: async () => {
          if (await save()) {
            leaving.current = true;
            navigation.dispatch(data.action);
          }
        },
      },
    ]);
  });

  const set = (changes: Partial<Recipe>) => setRecipe((r) => ({ ...r, ...changes }));
  const wbMode: WbMode = typeof recipe.whiteBalance === 'number' ? 'kelvin' : recipe.whiteBalance;

  /** Saves (built-ins and new ones as a new recipe). Returns false if it couldn't. */
  async function save(): Promise<boolean> {
    if (invalid) {
      toast(invalid, 'error');
      return false;
    }
    try {
      const asNew = isBuiltin || isNew;
      const target = asNew ? { ...recipe, name: freeName(recipe.name) } : recipe;
      const result = await saveRecipe({ ...target, madeOn: target.madeOn ?? info?.model }, asNew ? undefined : stored!.id);
      setSaved(target);
      setRecipe(target);
      toast(asNew ? `"${target.name}" 저장됨` : '저장됨');
      if (asNew) {
        leaving.current = true;
        router.replace({ pathname: '/recipe/[id]', params: { id: result.id } });
      }
      return true;
    } catch (e) {
      toastError('저장 실패', e);
      return false;
    }
  }

  async function apply() {
    setBusy(true);
    try {
      const state = await client.state().catch(() => null);
      const warnings = applyWarnings(recipe, state);
      await applyRecipe(client, recipe);
      if (!dirty && stored) await markApplied(stored.id);
      preview.keep();
      toast(warnings.length ? `적용됨 — ${warnings[0]}` : '카메라에 적용됨', warnings.length ? 'info' : 'ok');
    } catch (e) {
      toastError('적용 실패', e);
    } finally {
      setBusy(false);
    }
  }

  function more() {
    showActions(recipe.name, [
      { title: '공유 (QR · 링크 · 파일)', onPress: () => router.push({ pathname: '/recipe/share', params: { draft: JSON.stringify(recipe) } }) },
      {
        title: '복제해서 새로 만들기',
        onPress: () => router.push({ pathname: '/recipe/[id]', params: { id: 'new', draft: JSON.stringify({ ...recipe, name: freeName(recipe.name) }) } }),
      },
      ...(dirty && !isNew ? [{ title: '변경 취소 (저장된 값으로)', onPress: () => setRecipe(saved) }] : []),
      ...(!isBuiltin && !isNew
        ? [
            {
              title: '삭제',
              destructive: true,
              onPress: () =>
                Alert.alert(`"${recipe.name}" 삭제?`, '폰에서만 지워져요.', [
                  { text: '취소', style: 'cancel' },
                  {
                    text: '삭제',
                    style: 'destructive',
                    onPress: async () => {
                      leaving.current = true;
                      await deleteRecipe(stored!.id);
                      toast('삭제됨', 'info');
                      router.back();
                    },
                  },
                ]),
            },
          ]
        : []),
    ]);
  }

  const liveHeight = Math.min(220, Math.round((width * 9) / 16));
  const previewLabel =
    !connected ? '카메라 연결 안 됨 — 미리보기 없음' : !previewOn ? '미리보기 꺼짐 (카메라는 원래 룩)' : preview.status === 'sending' ? '카메라에 보내는 중…' : preview.status === 'error' ? '미리보기 실패' : '실시간 미리보기 — 카메라에 반영 중';

  return (
    <View style={styles.screen}>
      {connected ? (
        <View style={{ height: liveHeight }}>
          <LiveView url={client.liveViewUrl()} rotation={0} style={{ width, height: liveHeight }} />
          <Pressable style={styles.previewBadge} onPress={() => setPreviewOn((v) => !v)} accessibilityRole="switch" accessibilityState={{ checked: previewOn }}>
            <Ionicons name={previewOn ? 'eye' : 'eye-off'} size={14} color={previewOn ? colors.accent : colors.textDim} />
            <Text style={styles.previewText}>{previewOn ? '미리보기' : '원래 룩'}</Text>
          </Pressable>
        </View>
      ) : null}
      <ScrollView style={styles.scroll} scrollEnabled={!dragging} contentContainerStyle={[styles.content, { paddingBottom: 110 + insets.bottom }]} keyboardShouldPersistTaps="handled">
        <View style={styles.titleRow}>
          <Swatch stops={swatchColors(recipe)} size={48} radius={12} />
          <View style={{ flex: 1 }}>
            <TextInput
              style={styles.name}
              value={recipe.name}
              onChangeText={(name) => set({ name: sanitizeName(name) })}
              autoCorrect={false}
              autoCapitalize="words"
              placeholder="Recipe name"
              placeholderTextColor={colors.textDim}
              accessibilityLabel="레시피 이름"
            />
            <Text style={styles.nameHelp}>{NAME_PATTERN.test(recipe.name) ? `${recipe.name.length}/24 · 영문·숫자 (카메라 글꼴)` : '이름을 입력하세요'}</Text>
          </View>
        </View>
        <Text style={styles.status}>{previewLabel}</Text>

        <Card title="크리에이티브 스타일">
          <TileGrid
            values={STYLES}
            value={recipe.style}
            onChange={(style) => set({ style })}
            label={(s) => STYLE_LABELS[s]}
            swatch={(s) => swatchColors({ ...recipe, style: s })}
          />
          <ValueStrip label="채도" value={recipe.saturation} min={-3} max={3} onChange={(saturation) => set({ saturation })} disabled={recipe.style === 'mono' || recipe.style === 'sepia'} />
          <ValueStrip label="대비" value={recipe.contrast} min={-3} max={3} onChange={(contrast) => set({ contrast })} />
          <ValueStrip label="선명도" value={recipe.sharpness} min={-3} max={3} onChange={(sharpness) => set({ sharpness })} />
          <Text style={styles.hint}>칸을 길게 누르면 0으로</Text>
        </Card>

        <Card title="화이트 밸런스">
          <Segmented values={['auto', 'kelvin', 'keep'] as WbMode[]} value={wbMode} label={(m) => WB_LABEL[m]} onChange={(m) => set({ whiteBalance: m === 'kelvin' ? 5500 : m })} />
          {wbMode === 'keep' ? <Text style={styles.hint}>적용할 때 카메라의 화이트 밸런스를 건드리지 않아요 (미세조정 포함).</Text> : null}
          {typeof recipe.whiteBalance === 'number' ? (
            <>
              <View style={styles.kelvinRow}>
                <View style={[styles.kelvinBar]}>
                  <View style={[styles.kelvinFill, { left: `${((recipe.whiteBalance - 2500) / 7400) * 100}%` }]} />
                </View>
                <Stepper value={recipe.whiteBalance / 100} min={25} max={99} format={(v) => `${v * 100}K`} onChange={(v) => set({ whiteBalance: v * 100 })} />
              </View>
              <Chips values={KELVIN_PRESETS} value={recipe.whiteBalance} onChange={(k) => set({ whiteBalance: k })} label={(k) => `${KELVIN_NAMES[k]} ${k}K`} />
            </>
          ) : null}
          {wbMode !== 'keep' ? <WbPad amberBlue={recipe.amberBlue} greenMagenta={recipe.greenMagenta} onChange={(amberBlue, greenMagenta) => set({ amberBlue, greenMagenta })} onActiveChange={setDragging} /> : null}
        </Card>

        <Card title="노출 · 다이내믹 레인지">
          <View style={styles.evRow}>
            <Text style={styles.label}>노출 보정</Text>
            <Stepper value={recipe.exposureThirds} min={-15} max={15} format={(v) => `${formatThirds(v)} EV`} onChange={(exposureThirds) => set({ exposureThirds })} />
          </View>
          <Chips values={EV_QUICK} value={recipe.exposureThirds} onChange={(exposureThirds) => set({ exposureThirds })} label={formatThirds} />
          <Text style={[styles.label, { marginTop: 8 }]}>DRO</Text>
          <Segmented values={DROS} value={recipe.dro} onChange={(dro) => set({ dro })} label={(d) => (d === 'off' ? '끔' : d === 'auto' ? '자동' : `${d}`)} />
        </Card>

        <Card title="픽처 이펙트">
          <TileGrid values={EFFECTS} value={recipe.effect} label={(e) => EFFECT_LABELS[e]} onChange={(effect) => set({ effect, effectOption: EFFECT_OPTIONS[effect]?.[0] ?? null })} />
          {EFFECT_OPTIONS[recipe.effect] ? (
            <Segmented values={EFFECT_OPTIONS[recipe.effect]!} value={recipe.effectOption ?? ''} onChange={(effectOption) => set({ effectOption })} label={(o) => OPTION_LABEL[o] ?? o} />
          ) : null}
          {recipe.effect !== 'off' ? <Text style={styles.warn}>⚠ 이펙트는 JPEG에서만 적용되고, 켜져 있는 동안 크리에이티브 스타일은 무시돼요.</Text> : null}
        </Card>
        {invalid ? <Text style={styles.warn}>{invalid}</Text> : null}
      </ScrollView>

      <View style={[styles.bar, { paddingBottom: insets.bottom + 10 }]}>
        <Pressable style={[styles.barButton, styles.barSecondary, (!dirty || !!invalid) && styles.disabled]} disabled={!dirty || !!invalid} onPress={save}>
          <Text style={styles.barSecondaryText}>{isBuiltin ? '내 레시피로 저장' : dirty ? '저장' : '저장됨'}</Text>
        </Pressable>
        <Pressable style={[styles.barButton, styles.barPrimary, (!connected || busy || !!invalid) && styles.disabled]} disabled={!connected || busy || !!invalid} onPress={apply}>
          <Ionicons name="color-wand" size={16} color="#000" />
          <Text style={styles.barPrimaryText}>{busy ? '적용 중…' : '카메라에 적용'}</Text>
        </Pressable>
      </View>
    </View>
  );
}

function Card({ title, children }: { title: string; children: React.ReactNode }) {
  return (
    <View style={styles.cardWrap}>
      <Text style={styles.cardTitle}>{title}</Text>
      <View style={styles.card}>{children}</View>
    </View>
  );
}

const styles = StyleSheet.create({
  screen: { flex: 1, backgroundColor: colors.background },
  scroll: { flex: 1 },
  content: { padding: 16, gap: 4 },
  previewBadge: {
    position: 'absolute',
    right: 10,
    bottom: 10,
    flexDirection: 'row',
    alignItems: 'center',
    gap: 5,
    paddingHorizontal: 10,
    height: 28,
    borderRadius: 14,
    backgroundColor: colors.overlay,
  },
  previewText: { color: colors.text, fontSize: 12, fontWeight: '600' },
  titleRow: { flexDirection: 'row', alignItems: 'center', gap: 12 },
  name: { color: colors.text, fontSize: 22, fontWeight: '700', height: 36, padding: 0 },
  nameHelp: { color: colors.textDim, fontSize: 11 },
  status: { color: colors.textDim, fontSize: 12, marginTop: 6 },
  cardWrap: { marginTop: 14, gap: 6 },
  cardTitle: { color: colors.textDim, fontSize: 13, fontWeight: '600' },
  card: { backgroundColor: colors.panel, borderRadius: 14, paddingHorizontal: 12, paddingVertical: 6 },
  hint: { color: colors.textDim, fontSize: 11, paddingBottom: 6 },
  warn: { color: '#ffb84d', fontSize: 12, paddingVertical: 6, lineHeight: 17 },
  label: { color: colors.text, fontSize: 15 },
  kelvinRow: { flexDirection: 'row', alignItems: 'center', gap: 12, paddingTop: 6 },
  kelvinBar: { flex: 1, height: 10, borderRadius: 5, backgroundColor: '#5b7bd5', overflow: 'visible', borderWidth: 0, experimental_backgroundImage: 'linear-gradient(90deg, #ff9a3c, #ffe3b0, #ffffff, #c6dcff, #7aa7ff)' },
  kelvinFill: { position: 'absolute', top: -4, width: 4, height: 18, marginLeft: -2, borderRadius: 2, backgroundColor: '#fff' },
  evRow: { flexDirection: 'row', alignItems: 'center', justifyContent: 'space-between', paddingTop: 8 },
  bar: {
    position: 'absolute',
    left: 0,
    right: 0,
    bottom: 0,
    flexDirection: 'row',
    gap: 10,
    paddingHorizontal: 16,
    paddingTop: 10,
    backgroundColor: 'rgba(15,15,16,0.96)',
    borderTopColor: colors.panelBorder,
    borderTopWidth: StyleSheet.hairlineWidth,
  },
  barButton: { flex: 1, height: 48, borderRadius: 12, alignItems: 'center', justifyContent: 'center', flexDirection: 'row', gap: 6 },
  barPrimary: { backgroundColor: colors.accent },
  barPrimaryText: { color: '#000', fontWeight: '700', fontSize: 16 },
  barSecondary: { backgroundColor: '#1d1d20', borderWidth: 1, borderColor: colors.panelBorder },
  barSecondaryText: { color: colors.text, fontWeight: '600', fontSize: 15 },
  disabled: { opacity: 0.4 },
});
