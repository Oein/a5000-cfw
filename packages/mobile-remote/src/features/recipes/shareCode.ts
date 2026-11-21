/**
 * Recipe share codes: `https://recipelab.io/r#<code>`, the format Recipe Lab puts in QR codes.
 * base64url without padding of
 *   [version=1, family=0, style, sat+3, con+3, sharp+3, wb(0 auto,1 keep,25..99 = K/100),
 *    ab+7, gm+7, thirds+15, dro(0 off,1..5,6 auto), effect, effect-option, ...name ASCII]
 * Implemented from Recipe Lab's published spec. Pure TypeScript.
 */

import { EFFECT_OPTIONS, EFFECTS, problem, STYLES, type Dro, type Recipe } from './model.ts';

const B64 = 'ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789-_';
export const SHARE_PREFIX = 'https://recipelab.io/r#';

export class ShareCodeError extends Error {}

function encode(bytes: number[]) {
  let out = '';
  for (let i = 0; i < bytes.length; i += 3) {
    const n = (bytes[i] << 16) | ((bytes[i + 1] ?? 0) << 8) | (bytes[i + 2] ?? 0);
    const chars = i + 2 < bytes.length ? 4 : i + 1 < bytes.length ? 3 : 2;
    for (let c = 0; c < chars; c++) out += B64[(n >> (18 - 6 * c)) & 63];
  }
  return out;
}

function decode(code: string): number[] {
  if (code.length % 4 === 1) throw new ShareCodeError('bad length');
  const bytes: number[] = [];
  let bits = 0;
  let value = 0;
  for (const ch of code) {
    const v = B64.indexOf(ch);
    if (v < 0) throw new ShareCodeError(`bad character "${ch}"`);
    value = (value << 6) | v;
    bits += 6;
    if (bits >= 8) {
      bits -= 8;
      bytes.push((value >> bits) & 0xff);
      value &= (1 << bits) - 1;
    }
  }
  if (value !== 0) throw new ShareCodeError('stray bits after the last byte');
  return bytes;
}

export function toShareCode(r: Recipe): string {
  const why = problem(r);
  if (why) throw new ShareCodeError(why);
  const wb = r.whiteBalance === 'auto' ? 0 : r.whiteBalance === 'keep' ? 1 : r.whiteBalance / 100;
  const dro = r.dro === 'off' ? 0 : r.dro === 'auto' ? 6 : r.dro;
  const options = EFFECT_OPTIONS[r.effect];
  const bytes = [
    1,
    0,
    STYLES.indexOf(r.style),
    r.saturation + 3,
    r.contrast + 3,
    r.sharpness + 3,
    wb,
    r.amberBlue + 7,
    r.greenMagenta + 7,
    r.exposureThirds + 15,
    dro,
    EFFECTS.indexOf(r.effect),
    options ? options.indexOf(r.effectOption ?? '') : 0,
    ...Array.from(r.name, (c) => c.charCodeAt(0)),
  ];
  return encode(bytes);
}

export function toShareLink(r: Recipe) {
  return SHARE_PREFIX + toShareCode(r);
}

/** Reads a code or a whole link (anything after "#"). */
export function fromShareCode(input: string): Recipe {
  const code = input.trim().includes('#') ? input.trim().slice(input.trim().indexOf('#') + 1) : input.trim();
  const b = decode(code);
  if (b.length < 14) throw new ShareCodeError('too short');
  if (b[0] !== 1) throw new ShareCodeError(`version ${b[0]} not supported`);
  if (b[1] !== 0) throw new ShareCodeError('made for other cameras');
  const style = STYLES[b[2]];
  const effect = EFFECTS[b[11]];
  if (!style) throw new ShareCodeError(`style ${b[2]}`);
  if (!effect) throw new ShareCodeError(`effect ${b[11]}`);
  const options = EFFECT_OPTIONS[effect];
  let effectOption: string | null = null;
  if (options) {
    effectOption = options[b[12]];
    if (!effectOption) throw new ShareCodeError(`effect option ${b[12]}`);
  } else if (b[12] !== 0) throw new ShareCodeError('option on an effect without options');
  const wbByte = b[6];
  const whiteBalance = wbByte === 0 ? 'auto' : wbByte === 1 ? 'keep' : wbByte >= 25 && wbByte <= 99 ? wbByte * 100 : NaN;
  if (Number.isNaN(whiteBalance)) throw new ShareCodeError(`white balance ${wbByte}`);
  const dro: Dro = b[10] === 0 ? 'off' : b[10] === 6 ? 'auto' : (b[10] as Dro);
  const recipe: Recipe = {
    name: String.fromCharCode(...b.slice(13)),
    style,
    saturation: b[3] - 3,
    contrast: b[4] - 3,
    sharpness: b[5] - 3,
    effect,
    effectOption,
    whiteBalance: whiteBalance as Recipe['whiteBalance'],
    amberBlue: b[7] - 7,
    greenMagenta: b[8] - 7,
    exposureThirds: b[9] - 15,
    dro,
  };
  const why = problem(recipe);
  if (why) throw new ShareCodeError(why);
  return recipe;
}
