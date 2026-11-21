import assert from 'node:assert/strict';
import { test } from 'node:test';

import { recipeChanges } from '../src/features/recipes/apply.ts';
import { defaultRecipe, formatThirds, parseThirds } from '../src/features/recipes/model.ts';
import { fromShareCode, toShareCode } from '../src/features/recipes/shareCode.ts';
import { fileNameFor, parseRecipeFile, writeRecipeFile } from '../src/features/recipes/yaml.ts';

test('reads a format 1 file with comments', () => {
  const r = parseRecipeFile(`name: "Golden Hour"
style: portrait              # standard vivid neutral portrait …
saturation: -1               # -3 .. +3
white-balance: 5600K         # auto, keep, or 2500K .. 9900K
exposure: +0.7               # -5.0 .. +5.0 in thirds
`);
  assert.equal(r.name, 'Golden Hour');
  assert.equal(r.style, 'portrait');
  assert.equal(r.saturation, -1);
  assert.equal(r.whiteBalance, 5600);
  assert.equal(r.exposureThirds, 2);
  assert.equal(r.dro, 'auto');
});

test('format 2 needs our family; newer formats are refused', () => {
  assert.throws(() => parseRecipeFile('format: 2\nname: "A"\n'), /family/);
  assert.throws(() => parseRecipeFile('format: 2\nfamily: canon-picture-style\nname: "A"\n'), /other cameras/);
  assert.throws(() => parseRecipeFile('format: 3\nfamily: sony-creative-style\nname: "A"\n'), /newer/);
});

test('bad values skip the file with a reason, never clamp', () => {
  assert.throws(() => parseRecipeFile('name: "A"\nsaturation: 4\n'), /saturation/);
  assert.throws(() => parseRecipeFile('name: "A"\nexposure: 0.5\n'), /thirds/);
  assert.throws(() => parseRecipeFile('name: "A"\nwhite-balance: 5650K\n'), /white-balance/);
  assert.throws(() => parseRecipeFile('name: "A"\n  nested: 1\n'), /line 2/);
});

test('write → read round trip', () => {
  const r = { ...defaultRecipe('Velvia 2'), style: 'vivid' as const, saturation: 3, effect: 'toy-camera' as const, effectOption: 'warm', exposureThirds: -4, dro: 2 as const, whiteBalance: 'keep' as const };
  assert.deepEqual(parseRecipeFile(writeRecipeFile(r)), r);
  assert.equal(fileNameFor('Golden Hour'), 'GOLDENHO.YML');
});

test('thirds formatting', () => {
  assert.equal(formatThirds(2), '+0.7');
  assert.equal(formatThirds(-4), '-1.3');
  assert.equal(parseThirds('-1.3'), -4);
});

// Vectors from Recipe Lab's published recipe-share-vectors.tsv (interoperability check)
const VECTORS: [string, Partial<ReturnType<typeof defaultRecipe>>][] = [
  ['AQAAAgUDAAoIDgYAAENsYXNzaWMgQ2hyb21l', { name: 'Classic Chrome', style: 'standard', saturation: -1, contrast: 2, sharpness: 0, whiteBalance: 'auto', amberBlue: 3, greenMagenta: 1, exposureThirds: -1, dro: 'auto', effect: 'off', effectOption: null }],
  ['AQAMBgAGYw4AHgANAEtvZGFrIEdvbGQgMjAwICh3YXJtKSsmJw', { name: "Kodak Gold 200 (warm)+&'", style: 'sepia', saturation: 3, contrast: -3, sharpness: 3, whiteBalance: 9900, amberBlue: 7, greenMagenta: -7, exposureThirds: 15, dro: 'off', effect: 'watercolor' }],
  ['AQABAAYAGQAOAAEBBENvb2wtVG95', { name: 'Cool-Toy', style: 'vivid', whiteBalance: 2500, exposureThirds: -15, dro: 1, effect: 'toy-camera', effectOption: 'magenta' }],
  ['AQALBAQEAQcHEgUDAUtlZXAgV0IgMS4w', { name: 'Keep WB 1.0', style: 'red-leaves', whiteBalance: 'keep', exposureThirds: 3, dro: 5, effect: 'posterization', effectOption: 'posterization-bw' }],
];

for (const [code, expected] of VECTORS) {
  test(`share code ${expected.name}`, () => {
    const r = fromShareCode(`https://recipelab.io/r#${code}`);
    for (const [k, v] of Object.entries(expected)) assert.deepEqual((r as Record<string, unknown>)[k], v, k);
    assert.equal(toShareCode(r), code);
  });
}

test('share code rejects stray bits and other families', () => {
  assert.doesNotThrow(() => fromShareCode('AQAAAwMDAAcHDwYAAEE'));
  assert.throws(() => fromShareCode('AQAAAwMDAAcHDwYAAEF'), /stray/);
  assert.throws(() => fromShareCode('AQEAAgUDAAoIDgYAAEE'));
});

test('recipe → camera changes flips WB fine-tune signs', () => {
  const c = recipeChanges({ ...defaultRecipe(), whiteBalance: 5600, amberBlue: 3, greenMagenta: -2, exposureThirds: 2, effect: 'posterization', effectOption: 'posterization-bw' });
  assert.equal(c.whiteBalance, 'color-temp');
  assert.equal(c.colorTemperature, 5600);
  assert.equal(c.wbAB, -3);
  assert.equal(c.wbGM, 2);
  assert.equal(c.ev, 2);
  assert.equal(c.pictureEffectOption, 'posterization-mono');
  assert.equal(recipeChanges({ ...defaultRecipe(), whiteBalance: 'keep' }).whiteBalance, undefined);
});

import { lookSnapshot, recipeFromCamera } from '../src/features/recipes/fromCamera.ts';
import { swatchColors } from '../src/features/recipes/swatch.ts';
import type { CameraState } from '../src/lib/camera.ts';

/** A camera state after applying `r` (what the mock / a camera would report). */
function stateAfter(r: ReturnType<typeof defaultRecipe>): CameraState {
  const c = recipeChanges(r);
  return {
    mode: 'manual', sceneMode: 'manual-exposure', sceneModeValues: [], iso: 400, isoValues: [], shutter: [1, 125], shutterText: '1/125',
    aperture: 5.6, ev: c.ev ?? 0, evMin: -9, evMax: 9, evStep: 1 / 3, whiteBalance: c.whiteBalance ?? 'daylight', whiteBalanceValues: [],
    driveMode: 'single', driveModeValues: [], focusMode: 'auto', colorTemperature: c.colorTemperature ?? 5500, wbAB: c.wbAB ?? 0, wbGM: c.wbGM ?? 0,
    look: { creativeStyle: c.creativeStyle!, saturation: c.saturation!, contrast: c.contrast!, sharpness: c.sharpness!, pictureEffect: c.pictureEffect!, pictureEffectOption: c.pictureEffectOption, dro: c.dro! },
  };
}

test('apply → read back from the camera gives the same recipe', () => {
  const r = { ...defaultRecipe('Round Trip'), style: 'deep' as const, saturation: 2, contrast: -1, whiteBalance: 6300, amberBlue: 3, greenMagenta: -2, exposureThirds: -4, dro: 3 as const, effect: 'posterization' as const, effectOption: 'posterization-bw' };
  const { recipe, unknown } = recipeFromCamera(stateAfter(r), 'Round Trip');
  assert.deepEqual(unknown, []);
  assert.deepEqual(recipe, r);
});

test('a WB preset the format cannot name becomes "keep"; missing look is reported', () => {
  const s = stateAfter(defaultRecipe());
  s.whiteBalance = 'shade';
  delete s.look;
  const { recipe, unknown } = recipeFromCamera(s, 'X');
  assert.equal(recipe.whiteBalance, 'keep');
  assert.equal(unknown.length, 1);
});

test('snapshot restores what the camera had', () => {
  const s = stateAfter({ ...defaultRecipe(), style: 'vivid', whiteBalance: 4800 });
  const snap = lookSnapshot(s);
  assert.equal(snap.creativeStyle, 'vivid');
  assert.equal(snap.colorTemperature, 4800);
  assert.equal(snap.whiteBalance, 'color-temp');
});

test('swatches: mono is grey, warm WB is warmer than cool WB', () => {
  const [, mid] = swatchColors({ ...defaultRecipe(), style: 'mono' });
  assert.equal(mid.slice(1, 3), mid.slice(3, 5));
  const red = (hex: string) => parseInt(hex.slice(1, 3), 16) - parseInt(hex.slice(5, 7), 16);
  assert.ok(red(swatchColors({ ...defaultRecipe(), whiteBalance: 9000 })[1]) > red(swatchColors({ ...defaultRecipe(), whiteBalance: 3000 })[1]));
});
