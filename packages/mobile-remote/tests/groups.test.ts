import assert from 'node:assert/strict';
import { test } from 'node:test';

import { buildSections, groupKeys, groupOf, matches, pushRecent, type Listed } from '../src/features/recipes/groups.ts';
import { defaultRecipe } from '../src/features/recipes/model.ts';

const r = (id: string, extra: Partial<Listed> = {}): Listed => ({ ...defaultRecipe(id), id, builtin: true, ...extra });
const LIB: Listed[] = [
  r('Standard'),
  r('Mono', { style: 'mono' }),
  r('Velvet', { brand: 'Film A', title: 'Velvet (slide)', style: 'vivid', favorite: true }),
  r('Grain', { brand: 'Film B', style: 'mono' }),
  r('Mine', { builtin: false }),
];

test('groups: mine, built-in, then brands in order', () => {
  assert.deepEqual(groupKeys(LIB), ['mine', 'builtin', 'brand:Film A', 'brand:Film B']);
  assert.equal(groupOf(LIB[2]), 'brand:Film A');
});

test('search matches every word over name, title, brand and Korean style labels', () => {
  assert.ok(matches(LIB[2], 'film a slide'));
  assert.ok(matches(LIB[3], '흑백'));
  assert.ok(!matches(LIB[3], 'velvet'));
});

test('sections: shortcuts on top, folded groups keep counts, hidden groups last or dropped', () => {
  const s = buildSections(LIB, { recentIds: ['Grain', 'gone', 'Mine'], collapsed: new Set(['brand:Film A']), hidden: new Set(['builtin']) });
  assert.deepEqual(s.map((x) => x.key), ['recent', 'fav', 'mine', 'brand:Film A', 'brand:Film B', 'builtin']);
  assert.deepEqual(s[0].data.map((x) => x.id), ['Grain', 'Mine']);
  const filmA = s.find((x) => x.key === 'brand:Film A')!;
  assert.equal(filmA.count, 1);
  assert.equal(filmA.data.length, 0);
  assert.equal(filmA.items.length, 1);
  const dropped = buildSections(LIB, { hidden: new Set(['builtin']), dropHidden: true });
  assert.ok(!dropped.some((x) => x.key === 'builtin'));
  const found = buildSections(LIB, { query: 'mono', hidden: new Set(['builtin']), dropHidden: true });
  assert.deepEqual(found[0].data.map((x) => x.id), ['Mono', 'Grain']);
});

test('recent list: newest first, no repeats, capped', () => {
  assert.deepEqual(pushRecent(['a', 'b', 'c'], 'b', 3), ['b', 'a', 'c']);
  assert.deepEqual(pushRecent(['a', 'b', 'c'], 'd', 3), ['d', 'a', 'b']);
});
