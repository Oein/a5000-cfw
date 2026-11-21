/**
 * Picture recipes (feature 3): a named set of the settings that make a camera "look".
 *
 * The model and its value ranges follow Recipe Lab's recipe files (format 1/2, family
 * "sony-creative-style") so recipes move between that app, its share links and ours. This is an
 * independent implementation from its published file/link spec; no Recipe Lab code or recipe
 * table is included (that project is GPL-3.0).
 *
 * Pure TypeScript (runs under `node --test`).
 */

export const STYLES = [
  'standard',
  'vivid',
  'neutral',
  'portrait',
  'landscape',
  'mono',
  'clear',
  'deep',
  'light',
  'sunset',
  'night',
  'red-leaves',
  'sepia',
] as const;
export type Style = (typeof STYLES)[number];

export const EFFECTS = [
  'off',
  'toy-camera',
  'pop-color',
  'posterization',
  'retro-photo',
  'soft-high-key',
  'part-color',
  'rough-mono',
  'soft-focus',
  'hdr-art',
  'richtone-mono',
  'miniature',
  'illust',
  'watercolor',
] as const;
export type Effect = (typeof EFFECTS)[number];

export const EFFECT_OPTIONS: Partial<Record<Effect, readonly string[]>> = {
  'toy-camera': ['normal', 'cool', 'warm', 'green', 'magenta'],
  posterization: ['posterization-color', 'posterization-bw'],
  'soft-high-key': ['blue', 'pink', 'green'],
  'part-color': ['red', 'green', 'blue', 'yellow'],
};

/** "auto", "keep" (leave the camera's alone) or kelvin 2500..9900 in hundreds. */
export type WhiteBalance = 'auto' | 'keep' | number;
/** "off", "auto" or level 1..5. */
export type Dro = 'off' | 'auto' | 1 | 2 | 3 | 4 | 5;

export type Recipe = {
  name: string;
  style: Style;
  saturation: number;
  contrast: number;
  sharpness: number;
  effect: Effect;
  /** Only for effects with options; null otherwise. */
  effectOption: string | null;
  whiteBalance: WhiteBalance;
  /** -7..+7, amber positive (Recipe Lab's convention). */
  amberBlue: number;
  /** -7..+7, green positive. */
  greenMagenta: number;
  /** Exposure compensation in thirds of a stop, -15..+15. */
  exposureThirds: number;
  dro: Dro;
  /** Informational: the model it was made on. */
  madeOn?: string;
};

export const STYLE_LABELS: Record<Style, string> = {
  standard: '스탠다드',
  vivid: '비비드',
  neutral: '뉴트럴',
  portrait: '인물',
  landscape: '풍경',
  mono: '흑백',
  clear: '클리어',
  deep: '딥',
  light: '라이트',
  sunset: '일몰',
  night: '야경',
  'red-leaves': '단풍',
  sepia: '세피아',
};

export const EFFECT_LABELS: Record<Effect, string> = {
  off: '끔',
  'toy-camera': '토이 카메라',
  'pop-color': '팝 컬러',
  posterization: '포스터화',
  'retro-photo': '레트로 사진',
  'soft-high-key': '소프트 하이키',
  'part-color': '부분 컬러',
  'rough-mono': '고대비 흑백',
  'soft-focus': '소프트 포커스',
  'hdr-art': 'HDR 회화',
  'richtone-mono': '리치톤 흑백',
  miniature: '미니어처',
  illust: '일러스트',
  watercolor: '수채화',
};

export const NAME_PATTERN = /^[A-Za-z0-9 \-.'()&+]{1,24}$/;

export function defaultRecipe(name = 'Untitled'): Recipe {
  return {
    name,
    style: 'standard',
    saturation: 0,
    contrast: 0,
    sharpness: 0,
    effect: 'off',
    effectOption: null,
    whiteBalance: 'auto',
    amberBlue: 0,
    greenMagenta: 0,
    exposureThirds: 0,
    dro: 'auto',
  };
}

const intIn = (v: number, lo: number, hi: number) => Number.isInteger(v) && v >= lo && v <= hi;

/** Why a recipe can't be used, or null when it's valid. Values are checked, never clamped. */
export function problem(r: Recipe): string | null {
  if (!NAME_PATTERN.test(r.name)) return `name "${r.name}": 1–24 of A–Z a–z 0–9 and space - . ' ( ) & +`;
  if (!STYLES.includes(r.style)) return `style: unknown "${r.style}"`;
  for (const [k, v] of [
    ['saturation', r.saturation],
    ['contrast', r.contrast],
    ['sharpness', r.sharpness],
  ] as const) {
    if (!intIn(v, -3, 3)) return `${k}: ${v} is not -3..+3`;
  }
  if (!EFFECTS.includes(r.effect)) return `effect: unknown "${r.effect}"`;
  const options = EFFECT_OPTIONS[r.effect];
  if (options ? !options.includes(r.effectOption ?? '') : r.effectOption !== null) {
    return `effect-option: "${r.effectOption}" not valid for ${r.effect}`;
  }
  if (typeof r.whiteBalance === 'number' && !(intIn(r.whiteBalance, 2500, 9900) && r.whiteBalance % 100 === 0)) {
    return `white-balance: ${r.whiteBalance}K is not 2500K..9900K in hundreds`;
  }
  if (!intIn(r.amberBlue, -7, 7)) return `amber-blue: ${r.amberBlue} is not -7..+7`;
  if (!intIn(r.greenMagenta, -7, 7)) return `green-magenta: ${r.greenMagenta} is not -7..+7`;
  if (!intIn(r.exposureThirds, -15, 15)) return `exposure: ${r.exposureThirds}/3 is not -5.0..+5.0`;
  if (!(r.dro === 'off' || r.dro === 'auto' || intIn(r.dro as number, 1, 5))) return `dro: "${r.dro}"`;
  return null;
}

/** Thirds as the camera shows them: "+0.7", "-1.3", "0". */
export function formatThirds(thirds: number) {
  if (thirds === 0) return '0';
  const a = Math.abs(thirds);
  const frac = a % 3 === 0 ? '.0' : a % 3 === 1 ? '.3' : '.7';
  return `${thirds > 0 ? '+' : '-'}${Math.floor(a / 3)}${frac}`;
}

/** "+0.7" / "-1.3" / "0" / "+2" → thirds; NaN if not on a third. */
export function parseThirds(text: string) {
  const v = Number(text.trim().replace(/^\+/, ''));
  if (!Number.isFinite(v)) return NaN;
  const thirds = Math.round(v * 3);
  return Math.abs(v * 3 - thirds) < 0.11 ? thirds : NaN;
}

export function signed(n: number) {
  return n > 0 ? `+${n}` : String(n);
}

/** Two recipes set the same look (names aside). */
export function sameLook(a: Recipe, b: Recipe) {
  const { name: _a, madeOn: _ma, ...ra } = a;
  const { name: _b, madeOn: _mb, ...rb } = b;
  return JSON.stringify(ra) === JSON.stringify(rb);
}
