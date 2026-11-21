/**
 * A recipe's "look" as colours, so recipes can be told apart at a glance in lists and pickers.
 * Not a colour-science render — a hint: warm/cool from white balance + fine-tune, saturation,
 * contrast, mono/sepia styles, and brightness from exposure. Pure (runs under `node --test`).
 */

import type { Recipe } from './model.ts';

type RGB = [number, number, number];

const clamp = (v: number, lo = 0, hi = 255) => Math.max(lo, Math.min(hi, v));
const hex = ([r, g, b]: RGB) => `#${[r, g, b].map((v) => Math.round(clamp(v)).toString(16).padStart(2, '0')).join('')}`;

/** Approximate tint of a kelvin white balance on a neutral scene: low K renders cool, high K warm. */
function wbTint(r: Recipe): RGB {
  const k = typeof r.whiteBalance === 'number' ? r.whiteBalance : 5500;
  const t = (k - 5500) / 4400; // -0.68 .. +1
  const warm = t * 60;
  // amber + (Recipe Lab: amber positive), green + (green positive)
  const ab = r.amberBlue * 6;
  const gm = r.greenMagenta * 6;
  return [warm + ab - gm * 0.3, gm, -warm - ab - gm * 0.3];
}

const STYLE_BASE: Record<Recipe['style'], { sat: number; con: number; tint?: RGB }> = {
  standard: { sat: 0, con: 0 },
  vivid: { sat: 0.35, con: 0.15 },
  neutral: { sat: -0.2, con: -0.1 },
  portrait: { sat: -0.05, con: -0.1, tint: [12, 4, 0] },
  landscape: { sat: 0.25, con: 0.15, tint: [0, 8, 6] },
  mono: { sat: -1, con: 0.1 },
  clear: { sat: 0.1, con: 0.1, tint: [0, 2, 8] },
  deep: { sat: 0.15, con: 0.25 },
  light: { sat: -0.1, con: -0.25 },
  sunset: { sat: 0.3, con: 0.1, tint: [30, 6, -20] },
  night: { sat: -0.1, con: 0.2, tint: [-6, 0, 14] },
  'red-leaves': { sat: 0.3, con: 0.1, tint: [24, -4, -10] },
  sepia: { sat: -1, con: 0, tint: [40, 18, -20] },
};

/** Three stops (shadow, mid, highlight) of a sample scene rendered with the recipe. */
export function swatchColors(r: Recipe): [string, string, string] {
  const base = STYLE_BASE[r.style];
  const sat = Math.max(-1, base.sat + r.saturation * 0.15);
  const con = base.con + r.contrast * 0.1;
  const ev = r.exposureThirds / 3;
  const tint = wbTint(r);
  const scene: RGB[] = [
    [60, 90, 140], // shade/sky
    [200, 140, 90], // skin/warm mid
    [235, 225, 190], // highlight
  ];
  const mono = r.style === 'mono' || r.style === 'sepia' || r.effect === 'rough-mono' || r.effect === 'richtone-mono';
  return scene.map((c) => {
    let px = c.map((v, i) => v + tint[i] * (mono ? 0 : 1)) as RGB;
    const lum = 0.3 * px[0] + 0.59 * px[1] + 0.11 * px[2];
    px = px.map((v) => lum + (v - lum) * (mono ? 0 : 1 + sat)) as RGB;
    if (mono && base.tint) px = px.map((v, i) => v + base.tint![i]) as RGB;
    else if (base.tint) px = px.map((v, i) => v + base.tint![i]) as RGB;
    px = px.map((v) => 128 + (v - 128) * (1 + con)) as RGB;
    px = px.map((v) => v * Math.pow(2, ev * 0.5)) as RGB;
    return hex(px);
  }) as [string, string, string];
}
