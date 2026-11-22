import { EFFECT_OPTIONS, EFFECTS, NAME_PATTERN, problem, STYLES, type Dro, type Recipe, type Style, type WhiteBalance } from './model.ts';

/**
 * Reads Recipe Lab's built-in recipe table (its `Recipes.java`) into our recipes.
 *
 * Recipe Lab (github.com/voxivoid/recipe-lab-sony-pmca) is GPL-3.0, so its table is NOT bundled in
 * this app or repo: the phone downloads the file from GitHub when the user asks and this parser reads
 * it on the phone. Only the file's syntax is assumed — `static final int NAME = n` constants, the
 * `GROUPS = { "…", … }` brand list and `new Recipe(group, "name", style, sat, con, sharp, wbMode,
 * kelvin, ab, gm[, pe, ev, dro[, sub]])` rows — and the meaning of the stored enums its comments
 * document (style enum, wbMode 0 keep / 1 auto / 14 kelvin, pe = index in the effect list, dro 0 off,
 * 1..5, 6 auto, ab/gm amber/green positive like our model).
 *
 * Pure TypeScript (runs under `node --test`).
 */

export const RECIPE_LAB_SOURCE = 'https://raw.githubusercontent.com/voxivoid/recipe-lab-sony-pmca/main/src/com/voxivoid/recipelab/Recipes.java';

export type PackRecipe = { brand: string; title: string; recipe: Recipe };
export type PackParse = { recipes: PackRecipe[]; skipped: { title: string; reason: string }[] };

/** Stored creative-style enum → style (the camera's store values; 13 is unidentified). */
const STYLE_ENUM: Record<number, Style> = {
  1: 'standard',
  2: 'vivid',
  3: 'neutral',
  4: 'portrait',
  5: 'landscape',
  6: 'mono',
  7: 'clear',
  8: 'deep',
  9: 'light',
  10: 'sunset',
  11: 'night',
  12: 'red-leaves',
  14: 'sepia',
};
const DRO_AUTO = 6;

/** Fits a display title into the camera's name rules (≤24 of A–Z a–z 0–9 space - . ' ( ) & +). */
export function cameraName(title: string) {
  const clean = (s: string) =>
    s
      .replace(/\s*\/\s*/g, ' - ')
      .replace(/[^A-Za-z0-9 \-.'()&+]/g, '')
      .replace(/\s+/g, ' ')
      .trim();
  let name = clean(title);
  if (name.length > 24) name = clean(name.replace(/\s*\([^)]*\)/g, ''));
  if (name.length > 24) name = name.slice(0, 24).trim();
  return NAME_PATTERN.test(name) ? name : 'Recipe';
}

type Token = { kind: 'str'; value: string } | { kind: 'word'; value: string };

/** The arguments of one `new Recipe( … )`, starting just after the "(" — strings may hold parentheses. */
function readArgs(src: string, at: number): { args: Token[]; end: number } | null {
  const args: Token[] = [];
  let i = at;
  let word = '';
  const flush = () => {
    if (word.trim()) args.push({ kind: 'word', value: word.trim() });
    word = '';
  };
  while (i < src.length) {
    const c = src[i];
    if (c === '"') {
      let j = i + 1;
      let s = '';
      while (j < src.length && src[j] !== '"') {
        if (src[j] === '\\') j++;
        s += src[j];
        j++;
      }
      args.push({ kind: 'str', value: s });
      i = j + 1;
      continue;
    }
    if (c === ',') flush();
    else if (c === ')') {
      flush();
      return { args, end: i + 1 };
    } else if (c === '(' || c === '\n' || c === ';') return null;
    else word += c;
    i++;
  }
  return null;
}

/** `static final int A = 1, B = 2;` constants anywhere in the file. */
function intConstants(src: string) {
  const out = new Map<string, number>();
  for (const decl of src.matchAll(/static\s+final\s+int\s+([^;]+);/g)) {
    for (const part of decl[1].split(',')) {
      const m = part.match(/^\s*([A-Z_][A-Z0-9_]*)\s*=\s*(-?\d+)\s*$/);
      if (m) out.set(m[1], Number(m[2]));
    }
  }
  return out;
}

export function parseRecipeLabTable(src: string): PackParse {
  // Drop comments so a commented-out row isn't read (strings never hold "//" in this table)
  const code = src.replace(/\/\*[\s\S]*?\*\//g, '').replace(/\/\/[^\n]*/g, '');
  const consts = intConstants(code);
  const groupsMatch = code.match(/GROUPS\s*=\s*\{([^}]*)\}/);
  const groups = groupsMatch ? [...groupsMatch[1].matchAll(/"([^"]*)"/g)].map((m) => m[1]) : [];
  const num = (t: Token | undefined): number => {
    if (!t || t.kind !== 'word') return NaN;
    if (/^-?\d+$/.test(t.value)) return Number(t.value);
    const neg = t.value.startsWith('-');
    const v = consts.get(t.value.replace(/^-\s*/, ''));
    return v === undefined ? NaN : neg ? -v : v;
  };

  const recipes: PackRecipe[] = [];
  const skipped: PackParse['skipped'] = [];
  for (const m of code.matchAll(/new\s+Recipe\s*\(/g)) {
    const read = readArgs(code, m.index + m[0].length);
    if (!read) continue;
    const a = read.args;
    // The constructor definitions themselves ("Recipe(int group, …)") never match "new Recipe("
    if (a.length < 10 || a[1]?.kind !== 'str') continue;
    const title = a[1].value;
    const [group, style, sat, con, sharp, wbMode, kelvin, ab, gm] = [0, 2, 3, 4, 5, 6, 7, 8, 9].map((k) => num(a[k]));
    const pe = a.length > 10 ? num(a[10]) : 0;
    const ev = a.length > 11 ? num(a[11]) : 0;
    const dro = a.length > 12 ? num(a[12]) : DRO_AUTO;
    const sub = a.length > 13 ? num(a[13]) : 0;
    const why = (reason: string) => skipped.push({ title, reason });

    if ([group, style, sat, con, sharp, wbMode, kelvin, ab, gm, pe, ev, dro, sub].some(Number.isNaN)) {
      why('읽을 수 없는 값');
      continue;
    }
    const s = STYLE_ENUM[style];
    if (!s || !STYLES.includes(s)) {
      why(`알 수 없는 스타일 ${style}`);
      continue;
    }
    const effect = EFFECTS[pe];
    if (!effect) {
      why(`알 수 없는 이펙트 ${pe}`);
      continue;
    }
    const options = EFFECT_OPTIONS[effect];
    const whiteBalance: WhiteBalance | null = wbMode === 0 ? 'keep' : wbMode === 1 ? 'auto' : wbMode === 14 ? kelvin : null;
    if (whiteBalance === null) {
      why(`알 수 없는 WB 모드 ${wbMode}`);
      continue;
    }
    const recipe: Recipe = {
      name: cameraName(title),
      style: s,
      saturation: sat,
      contrast: con,
      sharpness: sharp,
      effect,
      effectOption: options ? (options[sub] ?? null) : null,
      whiteBalance,
      amberBlue: ab,
      greenMagenta: gm,
      exposureThirds: ev,
      dro: (dro === 0 ? 'off' : dro === DRO_AUTO ? 'auto' : dro) as Dro,
    };
    const bad = problem(recipe);
    if (bad) {
      why(bad);
      continue;
    }
    recipes.push({ brand: groups[group] ?? `#${group}`, title, recipe });
  }
  return { recipes, skipped };
}
