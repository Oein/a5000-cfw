// Builds the Recipe Lab recipe table into the app (src/features/recipes/bundled/recipelab.json).
// Personal-use build: the generated file is gitignored (this repo is public; Recipe Lab is GPL-3.0),
// so it is regenerated on `npm install` / `npm run recipes:bundle`. Offline → keeps the last file,
// or writes an empty list so the app still builds.
import { existsSync, mkdirSync, writeFileSync } from 'node:fs';
import { dirname, join } from 'node:path';
import { fileURLToPath } from 'node:url';

const here = dirname(fileURLToPath(import.meta.url));
const out = join(here, '../src/features/recipes/bundled/recipelab.json');
const { parseRecipeLabTable, RECIPE_LAB_SOURCE } = await import('../src/features/recipes/recipeLab.ts');

mkdirSync(dirname(out), { recursive: true });
try {
  const res = await fetch(RECIPE_LAB_SOURCE);
  if (!res.ok) throw new Error(`HTTP ${res.status}`);
  const { recipes, skipped } = parseRecipeLabTable(await res.text());
  if (recipes.length < 10) throw new Error(`only ${recipes.length} recipes parsed`);
  writeFileSync(out, JSON.stringify(recipes, null, 1) + '\n');
  console.log(`recipes: bundled ${recipes.length}${skipped.length ? `, skipped ${skipped.length}` : ''} → ${out}`);
} catch (e) {
  if (!existsSync(out)) writeFileSync(out, '[]\n');
  console.warn(`recipes: could not refresh (${e.message}); ${existsSync(out) ? 'kept existing file' : 'wrote empty list'}`);
}
