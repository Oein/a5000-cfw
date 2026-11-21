import * as Clipboard from 'expo-clipboard';
import { router, useLocalSearchParams, useNavigation } from 'expo-router';
import { useLayoutEffect, useMemo, useState } from 'react';
import { Alert, ScrollView, Share, StyleSheet, Text, TextInput } from 'react-native';
import { useSafeAreaInsets } from 'react-native-safe-area-context';

import { Button, Chips, formStyles, Row, Section, Stepper } from '@/components/Form';
import { useCamera } from '@/lib/CameraProvider';
import { colors } from '@/lib/theme';
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
import { toShareLink } from '@/features/recipes/shareCode';
import { deleteRecipe, freeName, getRecipe, saveRecipe, useRecipes } from '@/features/recipes/store';
import { writeRecipeFile } from '@/features/recipes/yaml';

const WB_MODES = ['auto', 'keep', 'kelvin'] as const;
const WB_LABEL = { auto: '자동', keep: '카메라 값 유지', kelvin: '색온도' };
const DROS: Dro[] = ['off', 'auto', 1, 2, 3, 4, 5];

/** Shows and edits one recipe. id "new" starts an empty one; built-ins are read-only (copy to edit). */
export default function RecipeScreen() {
  const { id, draft } = useLocalSearchParams<{ id: string; draft?: string }>();
  const navigation = useNavigation();
  const insets = useSafeAreaInsets();
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
    return stored ?? defaultRecipe(freeName('Untitled'));
    // eslint-disable-next-line react-hooks/exhaustive-deps
  }, [id, draft, stored?.id]);
  const [recipe, setRecipe] = useState<Recipe>(initial);
  const [message, setMessage] = useState<string | null>(null);
  const [busy, setBusy] = useState(false);
  const readOnly = stored?.builtin ?? false;
  const isNew = !stored;
  const dirty = isNew || !sameLook(recipe, initial) || recipe.name !== initial.name;
  const invalid = problem(recipe);

  useLayoutEffect(() => {
    navigation.setOptions({ title: isNew ? '새 레시피' : recipe.name });
  }, [navigation, isNew, recipe.name]);

  const set = (changes: Partial<Recipe>) => !readOnly && setRecipe((r) => ({ ...r, ...changes }));
  const wbMode = typeof recipe.whiteBalance === 'number' ? 'kelvin' : recipe.whiteBalance;

  const save = async (asNew = false) => {
    try {
      const target = asNew ? { ...recipe, name: freeName(recipe.name) } : recipe;
      const saved = await saveRecipe({ ...target, madeOn: target.madeOn ?? info?.model }, asNew || readOnly ? undefined : stored?.id);
      setMessage('저장했어요');
      if (asNew || isNew) router.replace({ pathname: '/recipe/[id]', params: { id: saved.id } });
    } catch (e) {
      setMessage(e instanceof Error ? e.message : String(e));
    }
  };

  const apply = async () => {
    setBusy(true);
    setMessage(null);
    try {
      const state = await client.state().catch(() => null);
      const warnings = applyWarnings(recipe, state);
      await applyRecipe(client, { ...recipe, id: dirty ? undefined : stored?.id });
      setMessage(['카메라에 적용했어요', ...warnings].join('\n• '));
    } catch (e) {
      setMessage(`적용 실패: ${e instanceof Error ? e.message : String(e)}`);
    } finally {
      setBusy(false);
    }
  };

  const share = async (kind: 'link' | 'yaml') => {
    try {
      const text = kind === 'link' ? toShareLink(recipe) : writeRecipeFile(recipe);
      await Clipboard.setStringAsync(text);
      await Share.share({ message: text });
    } catch (e) {
      setMessage(e instanceof Error ? e.message : String(e));
    }
  };

  const remove = () =>
    Alert.alert(`"${recipe.name}" 삭제?`, '폰에서만 지워져요.', [
      { text: '취소', style: 'cancel' },
      {
        text: '삭제',
        style: 'destructive',
        onPress: async () => {
          if (stored) await deleteRecipe(stored.id);
          router.back();
        },
      },
    ]);

  return (
    <ScrollView style={styles.screen} contentContainerStyle={[styles.content, { paddingBottom: 32 + insets.bottom }]}>
      {readOnly ? <Text style={formStyles.help}>기본 레시피는 바꿀 수 없어요. “복사해서 저장”으로 내 레시피를 만드세요.</Text> : null}

      <Section title="이름">
        <TextInput
          style={styles.name}
          value={recipe.name}
          editable={!readOnly}
          onChangeText={(name) => set({ name })}
          maxLength={24}
          autoCorrect={false}
          placeholder="Recipe name"
          placeholderTextColor={colors.textDim}
        />
        {!NAME_PATTERN.test(recipe.name) ? <Text style={styles.error}>영문·숫자와 공백 - . &apos; ( ) &amp; + 만, 24자까지 (카메라 폰트 제약)</Text> : null}
      </Section>

      <Section title="크리에이티브 스타일">
        <Chips values={STYLES} value={recipe.style} onChange={(style) => set({ style })} label={(s) => STYLE_LABELS[s]} />
        <Row label="채도">
          <Stepper value={recipe.saturation} min={-3} max={3} onChange={(saturation) => set({ saturation })} />
        </Row>
        <Row label="대비">
          <Stepper value={recipe.contrast} min={-3} max={3} onChange={(contrast) => set({ contrast })} />
        </Row>
        <Row label="선명도">
          <Stepper value={recipe.sharpness} min={-3} max={3} onChange={(sharpness) => set({ sharpness })} />
        </Row>
      </Section>

      <Section title="화이트 밸런스">
        <Chips
          values={WB_MODES}
          value={wbMode}
          label={(m) => WB_LABEL[m]}
          onChange={(m) => set({ whiteBalance: m === 'kelvin' ? 5500 : m })}
        />
        {typeof recipe.whiteBalance === 'number' ? (
          <Row label="색온도">
            <Stepper
              value={recipe.whiteBalance / 100}
              min={25}
              max={99}
              format={(v) => `${v * 100}K`}
              onChange={(v) => set({ whiteBalance: v * 100 })}
            />
          </Row>
        ) : null}
        <Row label="A-B 미세조정" detail="+ = 앰버">
          <Stepper value={recipe.amberBlue} min={-7} max={7} onChange={(amberBlue) => set({ amberBlue })} />
        </Row>
        <Row label="G-M 미세조정" detail="+ = 그린">
          <Stepper value={recipe.greenMagenta} min={-7} max={7} onChange={(greenMagenta) => set({ greenMagenta })} />
        </Row>
      </Section>

      <Section title="노출 · DRO">
        <Row label="노출 보정">
          <Stepper value={recipe.exposureThirds} min={-15} max={15} format={formatThirds} onChange={(exposureThirds) => set({ exposureThirds })} />
        </Row>
        <Chips values={DROS} value={recipe.dro} onChange={(dro) => set({ dro })} label={(d) => (d === 'off' ? 'DRO 끔' : d === 'auto' ? 'DRO 자동' : `Lv${d}`)} />
      </Section>

      <Section title="픽처 이펙트 (JPEG 전용)">
        <Chips
          values={EFFECTS}
          value={recipe.effect}
          label={(e) => EFFECT_LABELS[e]}
          onChange={(effect) => set({ effect, effectOption: EFFECT_OPTIONS[effect]?.[0] ?? null })}
        />
        {EFFECT_OPTIONS[recipe.effect] ? (
          <Chips values={EFFECT_OPTIONS[recipe.effect]!} value={recipe.effectOption ?? ''} onChange={(effectOption) => set({ effectOption })} />
        ) : null}
      </Section>

      {invalid ? <Text style={styles.error}>{invalid}</Text> : null}
      {message ? <Text style={styles.message}>{message}</Text> : null}

      <Text style={formStyles.help}>
        {connected ? '“카메라에 적용”은 저장하지 않아도 바로 카메라에 보내요.' : '카메라에 연결되면 적용할 수 있어요.'}
      </Text>
      <Section title="동작">
        <ScrollView horizontal={false} contentContainerStyle={[formStyles.buttons, styles.actions]}>
          <Button title={busy ? '적용 중…' : '카메라에 적용'} onPress={apply} disabled={!connected || busy || invalid !== null} />
          {!readOnly ? <Button title="저장" kind="secondary" onPress={() => save(false)} disabled={!dirty || invalid !== null} /> : null}
          <Button title="복사해서 저장" kind="secondary" onPress={() => save(true)} disabled={invalid !== null} />
          <Button title="공유 링크" kind="secondary" onPress={() => share('link')} disabled={invalid !== null} />
          <Button title="YAML 내보내기" kind="secondary" onPress={() => share('yaml')} disabled={invalid !== null} />
          {!readOnly && !isNew ? <Button title="삭제" kind="danger" onPress={remove} /> : null}
        </ScrollView>
      </Section>
    </ScrollView>
  );
}

const styles = StyleSheet.create({
  screen: { flex: 1, backgroundColor: colors.background },
  content: { padding: 16, gap: 6 },
  name: { color: colors.text, fontSize: 18, height: 46 },
  error: { color: colors.danger, fontSize: 13, paddingVertical: 4 },
  message: { color: colors.accent, fontSize: 14, marginTop: 8 },
  actions: { paddingVertical: 10 },
});
