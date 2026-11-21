import AsyncStorage from '@react-native-async-storage/async-storage';
import { useEffect, useState } from 'react';

import { BUILTIN_RECIPES } from './builtin';
import { problem, type Recipe } from './model';

/**
 * The phone's recipe library: built-ins (read-only) + the user's own (AsyncStorage). Names are
 * unique ignoring case, like on the camera.
 */

const KEY = 'recipes.v1';
const APPLIED_KEY = 'recipes.applied';

export type StoredRecipe = Recipe & { id: string; builtin: boolean; favorite?: boolean };

let custom: StoredRecipe[] | null = null;
let applied: string | null = null;
const listeners = new Set<() => void>();
const notify = () => listeners.forEach((l) => l());

const builtins: StoredRecipe[] = BUILTIN_RECIPES.map((r, i) => ({ ...r, id: `builtin-${i}`, builtin: true }));

async function load() {
  if (custom) return custom;
  try {
    const [raw, last] = await Promise.all([AsyncStorage.getItem(KEY), AsyncStorage.getItem(APPLIED_KEY)]);
    custom = raw ? (JSON.parse(raw) as StoredRecipe[]) : [];
    applied = last;
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
  return [...builtins, ...(custom ?? []).sort((a, b) => a.name.localeCompare(b.name))];
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
  await persist();
}

export async function toggleFavorite(id: string) {
  const list = await load();
  const r = list.find((x) => x.id === id);
  if (r) {
    r.favorite = !r.favorite;
    await persist();
  }
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
