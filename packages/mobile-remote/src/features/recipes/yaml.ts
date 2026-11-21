/**
 * Recipe files: Recipe Lab's flat YAML (`key: value` per line, `#` comments, plain or quoted
 * values, UTF-8, optional BOM). Reads format 1 and 2, writes format 2 with family
 * "sony-creative-style". Pure TypeScript.
 */

import { defaultRecipe, EFFECT_OPTIONS, formatThirds, parseThirds, problem, signed, type Dro, type Effect, type Recipe, type Style } from './model.ts';

export const FORMAT = 2;
export const FAMILY = 'sony-creative-style';
/** Files over this are not recipes. */
export const MAX_BYTES = 8 * 1024;

export class RecipeFileError extends Error {}

function unquote(v: string) {
  const t = v.trim();
  if (t.length >= 2 && ((t[0] === '"' && t.endsWith('"')) || (t[0] === "'" && t.endsWith("'")))) {
    return t[0] === '"' ? t.slice(1, -1).replace(/\\"/g, '"').replace(/\\\\/g, '\\') : t.slice(1, -1).replace(/''/g, "'");
  }
  return t;
}

/** Strips a trailing "# comment" outside quotes. */
function stripComment(line: string) {
  let quote: string | null = null;
  for (let i = 0; i < line.length; i++) {
    const c = line[i];
    if (quote) {
      if (c === quote) quote = null;
    } else if (c === '"' || c === "'") quote = c;
    else if (c === '#' && (i === 0 || /\s/.test(line[i - 1]))) return line.slice(0, i);
  }
  return line;
}

export function parseKeys(text: string): Map<string, string> {
  if (text.length > MAX_BYTES) throw new RecipeFileError('file over 8 KB');
  const keys = new Map<string, string>();
  const lines = text.replace(/^﻿/, '').split(/\r?\n/);
  lines.forEach((raw, i) => {
    const line = stripComment(raw).trimEnd();
    if (line.trim() === '' || line.trim() === '---') return;
    const m = /^([a-z][a-z-]*)\s*:\s*(.*)$/.exec(line);
    if (!m || /^\s/.test(line)) throw new RecipeFileError(`line ${i + 1}: not "key: value"`);
    keys.set(m[1], unquote(m[2]));
  });
  return keys;
}

function int(keys: Map<string, string>, key: string, fallback: number) {
  const v = keys.get(key);
  if (v === undefined || v === '') return fallback;
  const n = Number(v.replace(/^\+/, ''));
  if (!Number.isInteger(n)) throw new RecipeFileError(`${key}: "${v}" is not a whole number`);
  return n;
}

/** Reads a recipe file; throws RecipeFileError with the reason when it can't be used. */
export function parseRecipeFile(text: string, fileName = 'recipe'): Recipe {
  const keys = parseKeys(text);
  const format = int(keys, 'format', 1);
  if (format > FORMAT) throw new RecipeFileError('made by a newer Recipe Lab (format ' + format + ')');
  if (format < 1) throw new RecipeFileError(`format ${format}`);
  if (format >= 2) {
    const family = keys.get('family');
    if (!family) throw new RecipeFileError('format 2 file without family');
    if (family !== FAMILY) throw new RecipeFileError(`made for other cameras (${family})`);
  }

  const r = defaultRecipe(keys.get('name') ?? fileName.replace(/\.[^.]*$/, ''));
  r.style = (keys.get('style') || 'standard') as Style;
  r.saturation = int(keys, 'saturation', 0);
  r.contrast = int(keys, 'contrast', 0);
  r.sharpness = int(keys, 'sharpness', 0);
  r.effect = (keys.get('effect') || 'off') as Effect;
  const options = EFFECT_OPTIONS[r.effect];
  r.effectOption = options ? (keys.get('effect-option') || options[0]) : null;

  const wb = (keys.get('white-balance') || 'auto').toLowerCase();
  if (wb === 'auto' || wb === 'keep') r.whiteBalance = wb;
  else {
    const m = /^(\d{4})k$/.exec(wb);
    if (!m) throw new RecipeFileError(`white-balance: "${wb}"`);
    r.whiteBalance = Number(m[1]);
  }
  r.amberBlue = int(keys, 'amber-blue', 0);
  r.greenMagenta = int(keys, 'green-magenta', 0);

  const ev = keys.get('exposure');
  r.exposureThirds = ev === undefined || ev === '' ? 0 : parseThirds(ev);
  if (Number.isNaN(r.exposureThirds)) throw new RecipeFileError(`exposure: "${ev}" is not in thirds`);

  const dro = (keys.get('dro') || 'auto').toLowerCase();
  r.dro = dro === 'off' || dro === 'auto' ? dro : (Number(dro) as Dro);
  const madeOn = keys.get('made-on');
  if (madeOn) r.madeOn = madeOn;

  const why = problem(r);
  if (why) throw new RecipeFileError(why);
  return r;
}

/** Writes format 2, one line per key with the allowed values as comments. */
export function writeRecipeFile(r: Recipe): string {
  const why = problem(r);
  if (why) throw new RecipeFileError(why);
  const lines = [
    `format: ${FORMAT}`,
    `family: ${FAMILY}`,
    `name: "${r.name.replace(/\\/g, '\\\\').replace(/"/g, '\\"')}"`,
    ...(r.madeOn ? [`made-on: "${r.madeOn}"`] : []),
    `style: ${r.style}              # standard vivid neutral portrait landscape mono clear deep light sunset night red-leaves sepia`,
    `saturation: ${signed(r.saturation)}              # -3 .. +3`,
    `contrast: ${signed(r.contrast)}                # -3 .. +3`,
    `sharpness: ${signed(r.sharpness)}               # -3 .. +3`,
    `effect: ${r.effect}`,
    ...(r.effectOption ? [`effect-option: ${r.effectOption}`] : []),
    `white-balance: ${typeof r.whiteBalance === 'number' ? `${r.whiteBalance}K` : r.whiteBalance}         # auto, keep, or 2500K .. 9900K`,
    `amber-blue: ${signed(r.amberBlue)}              # -7 .. +7, amber positive`,
    `green-magenta: ${signed(r.greenMagenta)}           # -7 .. +7, green positive`,
    `exposure: ${formatThirds(r.exposureThirds)}                # -5.0 .. +5.0 in thirds`,
    `dro: ${r.dro}                    # off, auto, 1 .. 5`,
  ];
  return lines.join('\n') + '\n';
}

/** Recipe Lab's card-safe file name: first 8 letters/digits, upper case, + .YML. */
export function fileNameFor(name: string) {
  const base = name.replace(/[^A-Za-z0-9]/g, '').toUpperCase().slice(0, 8) || 'RECIPE';
  return `${base}.YML`;
}
