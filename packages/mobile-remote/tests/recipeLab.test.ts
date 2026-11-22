import assert from 'node:assert/strict';
import { test } from 'node:test';

import { cameraName, parseRecipeLabTable } from '../src/features/recipes/recipeLab.ts';

// A synthetic table in the same Java syntax (made-up names and values — not Recipe Lab's table).
const SRC = `
public class Recipes {
    Recipe(int group, String name, int style, int sat, int con, int sharp, int wbMode, int kelvin, int ab, int gm) { }
    public static final int STD = 1, VIVID = 2, MONO = 6, SEPIA = 14;
    private static final int AUTO = 1, K = 14;
    public static final int DRO_OFF = 0, DRO_AUTO = 6;
    public static final String[] GROUPS = { "Alpha", "Beta" };
    private static final int AA = 0, BB = 1;
    public static final Recipe[] ALL = {
        // ---- Alpha
        new Recipe(AA, "Plain",                       STD,   0,  0,  0, AUTO, 0,    0,  0),
        new Recipe(AA, "Warm (golden hour) / test",   VIVID, 2, -1,  1, K,    5600, 3, -2,  0, -2, DRO_OFF),   // comment (with parens)
        // new Recipe(AA, "Commented out", STD, 0, 0, 0, AUTO, 0, 0, 0),
        new Recipe(BB, "Toy Warm",                    STD,   0,  0,  0, 0,    0,    0,  0,  1,  1, 3, 2),
        new Recipe(BB, "Mono HC",                     MONO,  0,  3,  1, AUTO, 0,    0,  0,  7,  0, 6),
        new Recipe(BB, "Bad Sat",                     STD,   5,  0,  0, AUTO, 0,    0,  0),
        new Recipe(BB, "Unknown Style",               13,    0,  0,  0, AUTO, 0,    0,  0),
    };
}`;

test('parses rows with constants, optional args and comments', () => {
  const { recipes, skipped } = parseRecipeLabTable(SRC);
  assert.deepEqual(recipes.map((r) => r.title), ['Plain', 'Warm (golden hour) / test', 'Toy Warm', 'Mono HC']);
  assert.deepEqual(skipped.map((s) => s.title), ['Bad Sat', 'Unknown Style']);
  const [plain, warm, toy, mono] = recipes;
  assert.equal(plain.brand, 'Alpha');
  assert.deepEqual(plain.recipe, { name: 'Plain', style: 'standard', saturation: 0, contrast: 0, sharpness: 0, effect: 'off', effectOption: null, whiteBalance: 'auto', amberBlue: 0, greenMagenta: 0, exposureThirds: 0, dro: 'auto' });
  assert.equal(warm.recipe.style, 'vivid');
  assert.equal(warm.recipe.whiteBalance, 5600);
  assert.equal(warm.recipe.amberBlue, 3);
  assert.equal(warm.recipe.greenMagenta, -2);
  assert.equal(warm.recipe.exposureThirds, -2);
  assert.equal(warm.recipe.dro, 'off');
  assert.equal(toy.brand, 'Beta');
  assert.equal(toy.recipe.whiteBalance, 'keep');
  assert.equal(toy.recipe.effect, 'toy-camera');
  assert.equal(toy.recipe.effectOption, 'warm');
  assert.equal(toy.recipe.dro, 3);
  assert.equal(mono.recipe.effect, 'rough-mono');
  assert.equal(mono.recipe.effectOption, null);
});

test('camera names fit the camera rules', () => {
  assert.equal(cameraName('Polaroid / Instax'), 'Polaroid - Instax');
  assert.equal(cameraName('Kodak Vision 200T (Asteroid City)'), 'Kodak Vision 200T');
  assert.equal(cameraName('Short (kept)'), 'Short (kept)');
  assert.ok(cameraName('A very very long recipe name without parens').length <= 24);
});
