import * as Clipboard from 'expo-clipboard';
import * as DocumentPicker from 'expo-document-picker';
import { File } from 'expo-file-system';
import { router } from 'expo-router';
import { useState } from 'react';
import { ScrollView, StyleSheet, Text, TextInput } from 'react-native';

import { Button, formStyles, Section } from '@/components/Form';
import { colors } from '@/lib/theme';
import type { Recipe } from '@/features/recipes/model';
import { fromShareCode } from '@/features/recipes/shareCode';
import { freeName, saveRecipe } from '@/features/recipes/store';
import { recipeSummary } from '@/features/recipes/summary';
import { parseRecipeFile } from '@/features/recipes/yaml';

type Parsed = { ok: Recipe; source: string } | { error: string; source: string };

/** A share link / code, or a YAML recipe file's text. */
function parseAny(text: string, source: string): Parsed {
  const t = text.trim();
  try {
    if (/^https?:\/\/|^[A-Za-z0-9_-]{18,}$/.test(t) && !t.includes(':\n') && !t.includes('\n')) return { ok: fromShareCode(t), source };
    return { ok: parseRecipeFile(t, source), source };
  } catch (e) {
    return { error: e instanceof Error ? e.message : String(e), source };
  }
}

/**
 * Imports recipes: Recipe Lab share links (recipelab.io/r#…), its .YML files (picked from Files,
 * several at once), or text pasted from anywhere. Each is checked; a bad one says why.
 */
export default function ImportScreen() {
  const [text, setText] = useState('');
  const [results, setResults] = useState<Parsed[]>([]);
  const [message, setMessage] = useState<string | null>(null);

  const preview = (list: Parsed[]) => {
    setResults(list);
    setMessage(null);
  };

  const pickFiles = async () => {
    const picked = await DocumentPicker.getDocumentAsync({ multiple: true, copyToCacheDirectory: true, type: ['text/*', 'application/x-yaml', 'application/yaml', '*/*'] });
    if (picked.canceled) return;
    const list: Parsed[] = [];
    for (const asset of picked.assets) {
      try {
        list.push(parseAny(await new File(asset.uri).text(), asset.name));
      } catch (e) {
        list.push({ error: String(e), source: asset.name });
      }
    }
    preview(list);
  };

  const paste = async () => {
    const t = await Clipboard.getStringAsync();
    setText(t);
    preview(t.trim() ? [parseAny(t, '클립보드')] : []);
  };

  const importAll = async () => {
    let added = 0;
    const errors: string[] = [];
    for (const r of results) {
      if (!('ok' in r)) continue;
      try {
        await saveRecipe({ ...r.ok, name: freeName(r.ok.name) });
        added++;
      } catch (e) {
        errors.push(`${r.source}: ${e instanceof Error ? e.message : String(e)}`);
      }
    }
    setMessage([`${added}개 추가했어요`, ...errors].join('\n'));
    if (added > 0 && errors.length === 0) router.back();
  };

  const good = results.filter((r) => 'ok' in r).length;

  return (
    <ScrollView style={styles.screen} contentContainerStyle={styles.content}>
      <Text style={formStyles.help}>
        Recipe Lab 공유 링크(recipelab.io/r#…), 레시피 .YML 파일, 또는 YAML 텍스트를 가져올 수 있어요. 값이 범위를 벗어나면 고치지 않고 건너뛰며 이유를 알려줘요.
      </Text>
      <Section title="붙여넣기">
        <TextInput
          style={styles.input}
          multiline
          value={text}
          onChangeText={(t) => {
            setText(t);
            preview(t.trim() ? [parseAny(t, '입력')] : []);
          }}
          placeholder={'https://recipelab.io/r#…\n또는\nname: "Golden Hour"\nstyle: portrait\n…'}
          placeholderTextColor={colors.textDim}
          autoCapitalize="none"
          autoCorrect={false}
        />
      </Section>
      <Section title="불러오기">
        <ScrollView contentContainerStyle={[formStyles.buttons, styles.actions]}>
          <Button title="클립보드에서" kind="secondary" onPress={paste} />
          <Button title="파일 선택 (.YML)" kind="secondary" onPress={pickFiles} />
        </ScrollView>
      </Section>
      {results.length > 0 ? (
        <Section title={`미리보기 (${good}/${results.length})`}>
          {results.map((r, i) => (
            <Text key={i} style={[styles.result, !('ok' in r) && { color: colors.danger }]}>
              {'ok' in r ? `✓ ${r.ok.name} — ${recipeSummary(r.ok)}` : `✕ ${r.source}: ${r.error}`}
            </Text>
          ))}
        </Section>
      ) : null}
      {message ? <Text style={styles.message}>{message}</Text> : null}
      <Button title={`${good}개 가져오기`} onPress={importAll} disabled={good === 0} />
    </ScrollView>
  );
}

const styles = StyleSheet.create({
  screen: { flex: 1, backgroundColor: colors.background },
  content: { padding: 16, gap: 8, paddingBottom: 40 },
  input: { color: colors.text, minHeight: 120, fontSize: 14, fontFamily: 'Menlo', paddingVertical: 10, textAlignVertical: 'top' },
  actions: { paddingVertical: 10 },
  result: { color: colors.text, fontSize: 13, paddingVertical: 6 },
  message: { color: colors.accent, fontSize: 14 },
});
