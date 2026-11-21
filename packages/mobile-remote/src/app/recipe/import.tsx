import Ionicons from '@expo/vector-icons/Ionicons';
import * as Clipboard from 'expo-clipboard';
import * as DocumentPicker from 'expo-document-picker';
import { File } from 'expo-file-system';
import { router, useLocalSearchParams } from 'expo-router';
import { useEffect, useState } from 'react';
import { Pressable, ScrollView, StyleSheet, Text, TextInput, View } from 'react-native';

import { formStyles } from '@/components/Form';
import { Swatch } from '@/components/recipe/Controls';
import { colors } from '@/lib/theme';
import { toast } from '@/lib/toast';
import type { Recipe } from '@/features/recipes/model';
import { fromShareCode } from '@/features/recipes/shareCode';
import { freeName, nameTaken, saveRecipe } from '@/features/recipes/store';
import { recipeSummary } from '@/features/recipes/summary';
import { swatchColors } from '@/features/recipes/swatch';
import { parseRecipeFile } from '@/features/recipes/yaml';

type Parsed = { key: string; source: string } & ({ ok: Recipe } | { error: string });

const looksLikeShare = (t: string) => /recipelab\.io\/r#|^[A-Za-z0-9_-]{18,}$/.test(t.trim()) && !t.includes('\n');

/** Splits pasted text into items: every share link in it, or one YAML file. */
function parseAny(text: string, source: string): Parsed[] {
  const links = text.match(/https?:\/\/recipelab\.io\/r#[A-Za-z0-9_-]+/g);
  const parts = links ?? (looksLikeShare(text) ? [text.trim()] : null);
  if (parts) {
    return parts.map((p, i) => {
      try {
        return { key: `${source}-${i}`, source, ok: fromShareCode(p) };
      } catch (e) {
        return { key: `${source}-${i}`, source, error: e instanceof Error ? e.message : String(e) };
      }
    });
  }
  if (!/^\s*[a-z-]+\s*:/m.test(text)) {
    return [{ key: source, source, error: '레시피 링크(recipelab.io/r#…)나 레시피 YAML이 아니에요' }];
  }
  try {
    return [{ key: source, source, ok: parseRecipeFile(text.trim(), source) }];
  } catch (e) {
    return [{ key: source, source, error: e instanceof Error ? e.message : String(e) }];
  }
}

/**
 * Imports recipes: Recipe Lab share links (several at once from pasted text), its .YML files
 * (several from Files), or YAML text. Opens with whatever a deep link carried
 * (a5kremote://recipe/import?code=<share code> — the code alone: a "#" in the query is cut off),
 * and offers the clipboard when it holds text.
 */
export default function ImportScreen() {
  const { code } = useLocalSearchParams<{ code?: string }>();
  const [text, setText] = useState('');
  const [results, setResults] = useState<Parsed[]>(() => (code ? parseAny(code, '링크') : []));
  const [skip, setSkip] = useState<Set<string>>(new Set());
  // Another deep link while this screen is open: show its recipe (state adjusted during render)
  const [seenCode, setSeenCode] = useState(code);
  if (code !== seenCode) {
    setSeenCode(code);
    setResults(code ? parseAny(code, '링크') : []);
    setSkip(new Set());
  }
  const [clipboardHasText, setClipboardHasText] = useState(false);

  useEffect(() => {
    // hasStringAsync doesn't read the clipboard, so iOS shows no paste prompt for it
    Clipboard.hasStringAsync().then(setClipboardHasText).catch(() => {});
  }, []);

  const show = (list: Parsed[]) => {
    setResults(list);
    setSkip(new Set());
  };

  const pickFiles = async () => {
    const picked = await DocumentPicker.getDocumentAsync({ multiple: true, copyToCacheDirectory: true, type: '*/*' });
    if (picked.canceled) return;
    const list: Parsed[] = [];
    for (const asset of picked.assets) {
      try {
        list.push(...parseAny(await new File(asset.uri).text(), asset.name));
      } catch (e) {
        list.push({ key: asset.name, source: asset.name, error: String(e) });
      }
    }
    show(list);
  };

  const paste = async () => {
    const t = await Clipboard.getStringAsync();
    setText(t);
    show(t.trim() ? parseAny(t, '클립보드') : []);
  };

  const good = results.filter((r): r is Parsed & { ok: Recipe } => 'ok' in r && !skip.has(r.key));

  const importAll = async () => {
    let added = 0;
    for (const r of good) {
      try {
        await saveRecipe({ ...r.ok, name: freeName(r.ok.name) });
        added++;
      } catch (e) {
        toast(`${r.ok.name}: ${e instanceof Error ? e.message : String(e)}`, 'error');
      }
    }
    if (added) {
      toast(`${added}개 레시피를 추가했어요`);
      router.back();
    }
  };

  return (
    <View style={styles.screen}>
      <ScrollView contentContainerStyle={styles.content} keyboardShouldPersistTaps="handled">
        <View style={styles.sources}>
          <Source icon="clipboard-outline" title="클립보드" detail={clipboardHasText ? '복사한 링크·텍스트' : '비어 있음'} onPress={paste} highlight={clipboardHasText} />
          <Source icon="document-text-outline" title="파일" detail=".YML (여러 개)" onPress={pickFiles} />
        </View>
        <Text style={styles.section}>또는 붙여넣기</Text>
        <TextInput
          style={styles.input}
          multiline
          value={text}
          onChangeText={(t) => {
            setText(t);
            show(t.trim() ? parseAny(t, '입력') : []);
          }}
          placeholder={'https://recipelab.io/r#… (여러 개 가능)\n또는 레시피 YAML'}
          placeholderTextColor={colors.textDim}
          autoCapitalize="none"
          autoCorrect={false}
        />
        {results.length > 0 ? <Text style={styles.section}>미리보기 · 탭해서 제외</Text> : <Text style={formStyles.help}>Recipe Lab 공유 링크(recipelab.io/r#…)와 레시피 파일을 읽어요. 범위를 벗어난 값은 고치지 않고 건너뛰며 이유를 알려줘요.</Text>}
        {results.map((r) =>
          'ok' in r ? (
            <Pressable
              key={r.key}
              style={[styles.card, skip.has(r.key) && styles.skipped]}
              onPress={() =>
                setSkip((s) => {
                  const n = new Set(s);
                  if (n.has(r.key)) n.delete(r.key);
                  else n.add(r.key);
                  return n;
                })
              }>
              <Swatch stops={swatchColors(r.ok)} size={40} radius={10} />
              <View style={{ flex: 1 }}>
                <Text style={styles.cardName}>
                  {r.ok.name}
                  {nameTaken(r.ok.name) ? <Text style={styles.rename}>{`  → ${freeName(r.ok.name)}`}</Text> : null}
                </Text>
                <Text style={styles.cardSummary} numberOfLines={2}>
                  {recipeSummary(r.ok)}
                </Text>
              </View>
              <Ionicons name={skip.has(r.key) ? 'ellipse-outline' : 'checkmark-circle'} size={22} color={skip.has(r.key) ? colors.textDim : colors.ok} />
            </Pressable>
          ) : (
            <View key={r.key} style={[styles.card, styles.bad]}>
              <Ionicons name="alert-circle" size={22} color={colors.danger} />
              <View style={{ flex: 1 }}>
                <Text style={styles.cardName}>{r.source}</Text>
                <Text style={[styles.cardSummary, { color: colors.danger }]}>{r.error}</Text>
              </View>
            </View>
          ),
        )}
      </ScrollView>
      <View style={styles.bar}>
        <Pressable style={[styles.importButton, good.length === 0 && { opacity: 0.4 }]} disabled={good.length === 0} onPress={importAll}>
          <Text style={styles.importText}>{good.length ? `${good.length}개 가져오기` : '가져올 레시피 없음'}</Text>
        </Pressable>
      </View>
    </View>
  );
}

function Source({ icon, title, detail, onPress, highlight }: { icon: keyof typeof Ionicons.glyphMap; title: string; detail: string; onPress: () => void; highlight?: boolean }) {
  return (
    <Pressable style={[styles.source, highlight && { borderColor: colors.accent }]} onPress={onPress}>
      <Ionicons name={icon} size={24} color={highlight ? colors.accent : colors.text} />
      <Text style={styles.sourceTitle}>{title}</Text>
      <Text style={styles.sourceDetail}>{detail}</Text>
    </Pressable>
  );
}

const styles = StyleSheet.create({
  screen: { flex: 1, backgroundColor: colors.background },
  content: { padding: 16, gap: 10, paddingBottom: 100 },
  sources: { flexDirection: 'row', gap: 10 },
  source: { flex: 1, backgroundColor: colors.panel, borderRadius: 14, padding: 14, gap: 4, borderWidth: 1.5, borderColor: 'transparent' },
  sourceTitle: { color: colors.text, fontSize: 15, fontWeight: '700', marginTop: 4 },
  sourceDetail: { color: colors.textDim, fontSize: 12 },
  section: { color: colors.textDim, fontSize: 13, fontWeight: '600', marginTop: 8 },
  input: { color: colors.text, minHeight: 90, fontSize: 13, fontFamily: 'Menlo', padding: 12, borderRadius: 12, backgroundColor: colors.panel, textAlignVertical: 'top' },
  card: { flexDirection: 'row', alignItems: 'center', gap: 12, backgroundColor: colors.panel, borderRadius: 12, padding: 10 },
  skipped: { opacity: 0.45 },
  bad: { borderWidth: 1, borderColor: 'rgba(255,77,77,0.4)' },
  cardName: { color: colors.text, fontSize: 15, fontWeight: '600' },
  rename: { color: colors.textDim, fontSize: 12, fontWeight: '400' },
  cardSummary: { color: colors.textDim, fontSize: 12, marginTop: 2 },
  bar: { position: 'absolute', left: 0, right: 0, bottom: 0, padding: 16, paddingBottom: 28, backgroundColor: 'rgba(0,0,0,0.9)' },
  importButton: { height: 50, borderRadius: 12, backgroundColor: colors.accent, alignItems: 'center', justifyContent: 'center' },
  importText: { color: '#000', fontWeight: '700', fontSize: 16 },
});
