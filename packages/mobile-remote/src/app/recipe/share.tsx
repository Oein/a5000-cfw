import * as Clipboard from 'expo-clipboard';
import { Image } from 'expo-image';
import { useLocalSearchParams } from 'expo-router';
import makeQr from 'qrcode-generator';
import { useMemo } from 'react';
import { ScrollView, Share, StyleSheet, Text, View } from 'react-native';

import { Button, formStyles } from '@/components/Form';
import { Swatch } from '@/components/recipe/Controls';
import { colors } from '@/lib/theme';
import { toast, toastError } from '@/lib/toast';
import { problem, type Recipe } from '@/features/recipes/model';
import { toShareLink } from '@/features/recipes/shareCode';
import { recipeSummary } from '@/features/recipes/summary';
import { swatchColors } from '@/features/recipes/swatch';
import { fileNameFor, writeRecipeFile } from '@/features/recipes/yaml';

/**
 * Share a recipe: a QR code of its recipelab.io link (Recipe Lab on a camera reads it with
 * "Read Recipe QR Code"; any phone camera opens the link), the link itself, or the .YML file text.
 */
export default function ShareScreen() {
  const { draft } = useLocalSearchParams<{ draft: string }>();
  const recipe = useMemo(() => {
    try {
      const r = JSON.parse(draft ?? '') as Recipe;
      return problem(r) ? null : r;
    } catch {
      return null;
    }
  }, [draft]);
  const shared = useMemo(() => {
    if (!recipe) return { link: null, qrUri: null, error: '공유할 레시피를 읽지 못했어요.' };
    try {
      const link = toShareLink(recipe);
      const qr = makeQr(0, 'M');
      qr.addData(link);
      qr.make();
      return { link, qrUri: qr.createDataURL(8, 2), error: null };
    } catch (e) {
      return { link: null, qrUri: null, error: e instanceof Error ? e.message : String(e) };
    }
  }, [recipe]);

  const copy = async (text: string, what: string) => {
    await Clipboard.setStringAsync(text);
    toast(`${what} 복사됨`);
  };

  if (!recipe) {
    return (
      <View style={[styles.screen, styles.content]}>
        <Text style={styles.error}>{shared.error}</Text>
      </View>
    );
  }

  return (
    <ScrollView style={styles.screen} contentContainerStyle={styles.content}>
      <View style={styles.head}>
        <Swatch stops={swatchColors(recipe)} size={44} radius={11} />
        <View style={{ flex: 1 }}>
          <Text style={styles.name}>{recipe.name}</Text>
          <Text style={styles.summary}>{recipeSummary(recipe)}</Text>
        </View>
      </View>
      {shared.qrUri ? (
        <View style={styles.qrWrap}>
          <Image source={{ uri: shared.qrUri }} style={styles.qr} contentFit="contain" accessibilityLabel="레시피 QR 코드" />
          <Text style={formStyles.help}>Recipe Lab 카메라 앱: MENU 길게 › Read Recipe QR Code 로 이 코드를 찍으면 추가돼요.</Text>
        </View>
      ) : (
        <Text style={styles.error}>{shared.error}</Text>
      )}
      {shared.link ? (
        <>
          <Text style={styles.link} selectable numberOfLines={2}>
            {shared.link}
          </Text>
          <View style={formStyles.buttons}>
            <Button title="링크 복사" onPress={() => copy(shared.link!, '링크')} />
            <Button title="공유…" kind="secondary" onPress={() => Share.share({ message: `${recipe.name}\n${shared.link}` }).catch((e) => toastError('공유 실패', e))} />
          </View>
        </>
      ) : null}
      <Text style={styles.section}>파일 ({fileNameFor(recipe.name)})</Text>
      <Text style={formStyles.help}>카메라 SD 카드의 RECIPES 폴더에 넣거나, 다른 앱/사람에게 보낼 때.</Text>
      <View style={formStyles.buttons}>
        <Button title="YAML 복사" kind="secondary" onPress={() => copy(writeRecipeFile(recipe), 'YAML')} />
        <Button title="YAML 공유…" kind="secondary" onPress={() => Share.share({ message: writeRecipeFile(recipe) }).catch((e) => toastError('공유 실패', e))} />
      </View>
    </ScrollView>
  );
}

const styles = StyleSheet.create({
  screen: { flex: 1, backgroundColor: colors.background },
  content: { padding: 20, gap: 12, paddingBottom: 40 },
  head: { flexDirection: 'row', gap: 12, alignItems: 'center' },
  name: { color: colors.text, fontSize: 20, fontWeight: '700' },
  summary: { color: colors.textDim, fontSize: 12, marginTop: 2 },
  qrWrap: { alignItems: 'center', gap: 10, marginTop: 8 },
  qr: { width: 240, height: 240, backgroundColor: '#fff', borderRadius: 12 },
  link: { color: colors.accent, fontSize: 12, fontFamily: 'Menlo', textAlign: 'center' },
  section: { color: colors.textDim, fontSize: 13, fontWeight: '600', marginTop: 12 },
  error: { color: colors.danger, textAlign: 'center' },
});
