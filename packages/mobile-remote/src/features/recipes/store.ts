import AsyncStorage from '@react-native-async-storage/async-storage';
import { useEffect, useState } from 'react';

import { BUILTIN_RECIPES } from './builtin';
import { problem, type Recipe } from './model';
import { parseRecipeLabTable, RECIPE_LAB_SOURCE } from './recipeLab';

/**
 * The phone's recipe library: built-ins (fixed; editing one saves a copy) + the user's own (AsyncStorage). Names are
 * unique ignoring case, like on the camera.
 */

const KEY = 'recipes.v1';
const APPLIED_KEY = 'recipes.applied';
const FAVORITES_KEY = 'recipes.favorites';
const PACK_KEY = 'recipes.pack.recipelab.v1';

/** builtin = read-only (ours or a downloaded pack; editing saves a copy). brand/title only on pack recipes. */
export type StoredRecipe = Recipe & { id: string; builtin: boolean; favorite?: boolean; pack?: 'recipelab'; brand?: string; title?: string };
export type PackInfo = { fetchedAt: number; count: number; skipped: number };

/** The recipe alone (drops the library's bookkeeping fields). */
export function plainRecipe(r: Recipe & Partial<StoredRecipe>): Recipe {
  const { id: _i, builtin: _b, favorite: _f, pack: _p, brand: _br, title: _t, ...rest } = r;
  return rest;
}

let custom: StoredRecipe[] | null = null;
let applied: string | null = null;
let favorites = new Set<string>();
let pack: { info: PackInfo; recipes: StoredRecipe[] } | null = null;
const listeners = new Set<() => void>();
const notify = () => listeners.forEach((l) => l());

const builtins: StoredRecipe[] = BUILTIN_RECIPES.map((r, i) => ({ ...r, id: `builtin-${i}`, builtin: true }));

async function load() {
  if (custom) return custom;
  try {
    const [raw, last, favs, packRaw] = await Promise.all([
      AsyncStorage.getItem(KEY),
      AsyncStorage.getItem(APPLIED_KEY),
      AsyncStorage.getItem(FAVORITES_KEY),
      AsyncStorage.getItem(PACK_KEY),
    ]);
    pack = packRaw ? JSON.parse(packRaw) : null;
    custom = raw ? (JSON.parse(raw) as StoredRecipe[]) : [];
    applied = last;
    favorites = new Set(favs ? (JSON.parse(favs) as string[]) : custom.filter((r) => r.favorite).map((r) => r.id));
  } catch {
    custom = [];
  }
  return custom;
}

async function persist() {
  await AsyncStorage.setItem(KEY, JSON.stringify(custom ?? []));
  notify();
}

export function allRecipes(): StoredRecipe[] {
  return [...builtins, ...(pack?.recipes ?? []), ...(custom ?? []).sort((a, b) => a.name.localeCompare(b.name))].map((r) => ({ ...r, favorite: favorites.has(r.id) }));
}

export function getRecipe(id: string) {
  return allRecipes().find((r) => r.id === id) ?? null;
}

export function nameTaken(name: string, exceptId?: string) {
  return allRecipes().some((r) => r.id !== exceptId && r.name.toLowerCase() === name.toLowerCase());
}

/** A free name: "Velvia" → "Velvia 2", "Velvia 3"… */
export function freeName(name: string) {
  if (!nameTaken(name)) return name;
  const base = name.replace(/ \d+$/, '').slice(0, 21);
  for (let n = 2; ; n++) if (!nameTaken(`${base} ${n}`)) return `${base} ${n}`;
}

/** Adds or updates a user recipe. Throws with the reason when invalid or the name is taken. */
export async function saveRecipe(recipe: Recipe, id?: string): Promise<StoredRecipe> {
  const list = await load();
  const why = problem(recipe);
  if (why) throw new Error(why);
  if (nameTaken(recipe.name, id)) throw new Error(`"${recipe.name}" 이름이 이미 있어요`);
  const stored: StoredRecipe = { ...recipe, id: id ?? `user-${Date.now().toString(36)}-${Math.random().toString(36).slice(2, 6)}`, builtin: false };
  const at = list.findIndex((r) => r.id === stored.id);
  if (at >= 0) list[at] = { ...list[at], ...stored };
  else list.push(stored);
  await persist();
  return stored;
}

export async function deleteRecipe(id: string) {
  const list = await load();
  custom = list.filter((r) => r.id !== id);
  if (favorites.delete(id)) await AsyncStorage.setItem(FAVORITES_KEY, JSON.stringify([...favorites]));
  await persist();
}

/** Favourites work for built-ins too; returns the new state. */
export async function toggleFavorite(id: string) {
  await load();
  const on = !favorites.has(id);
  if (on) favorites.add(id);
  else favorites.delete(id);
  notify();
  await AsyncStorage.setItem(FAVORITES_KEY, JSON.stringify([...favorites]));
  return on;
}

/** A stable id per pack recipe, so favourites and "applied" survive a re-download. */
const packId = (title: string) => `rl-${title.toLowerCase().replace(/[^a-z0-9]+/g, '-').replace(/^-|-$/g, '')}`;

export function packInfo() {
  return pack?.info ?? null;
}

/**
 * Downloads Recipe Lab's recipe table from its GitHub repo and keeps it on the phone. Not bundled:
 * that project is GPL-3.0 (see recipeLab.ts). Names already used by the user's recipes get a number.
 */
export async function downloadRecipeLab(): Promise<PackInfo> {
  await load();
  const res = await fetch(RECIPE_LAB_SOURCE, { cache: 'no-store' });
  if (!res.ok) throw new Error(`GitHub 응답 ${res.status}`);
  const { recipes, skipped } = parseRecipeLabTable(await res.text());
  if (recipes.length < 10) throw new Error('레시피 표를 읽지 못했어요 (원본 형식이 바뀌었을 수 있어요)');
  const taken = new Set([...builtins, ...(custom ?? [])].map((r) => r.name.toLowerCase()));
  const seen = new Set<string>();
  const list: StoredRecipe[] = [];
  for (const p of recipes) {
    let name = p.recipe.name;
    for (let n = 2; taken.has(name.toLowerCase()) || seen.has(name.toLowerCase()); n++) name = `${p.recipe.name.replace(/ \d+$/, '').slice(0, 21)} ${n}`;
    seen.add(name.toLowerCase());
    let id = packId(p.title);
    while (list.some((r) => r.id === id)) id += '-2';
    list.push({ ...p.recipe, name, id, builtin: true, pack: 'recipelab', brand: p.brand, title: p.title });
  }
  pack = { info: { fetchedAt: Date.now(), count: list.length, skipped: skipped.length }, recipes: list };
  await AsyncStorage.setItem(PACK_KEY, JSON.stringify(pack));
  notify();
  return pack.info;
}

export async function removeRecipeLab() {
  await load();
  pack = null;
  await AsyncStorage.removeItem(PACK_KEY);
  notify();
}

export async function markApplied(id: string) {
  applied = id;
  notify();
  await AsyncStorage.setItem(APPLIED_KEY, id);
}

export function useRecipes() {
  const [, setTick] = useState(0);
  useEffect(() => {
    const l = () => setTick((t) => t + 1);
    listeners.add(l);
    load().then(l);
    return () => {
      listeners.delete(l);
    };
  }, []);
  return { recipes: allRecipes(), appliedId: applied, loaded: custom !== null };
}
