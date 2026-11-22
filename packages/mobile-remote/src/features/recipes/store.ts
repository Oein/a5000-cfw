import AsyncStorage from '@react-native-async-storage/async-storage';
import { useEffect, useState } from 'react';

import { BUILTIN_RECIPES } from './builtin';
import { pushRecent } from './groups';
import { problem, type Recipe } from './model';
import type { PackRecipe } from './recipeLab';
// Built into the app by `npm run recipes:bundle` (runs on npm install; gitignored)
import BUNDLED from './bundled/recipelab.json';

/**
 * The phone's recipe library: built-ins (ours + the film looks built into the app, grouped by brand; fixed, editing
 * one saves a copy) + the user's own (AsyncStorage). Names are unique ignoring case, like on the camera.
 */

const KEY = 'recipes.v1';
const APPLIED_KEY = 'recipes.applied';
const FAVORITES_KEY = 'recipes.favorites';
const RECENT_KEY = 'recipes.recent';
const VIEW_KEY = 'recipes.view';

/** builtin = read-only (editing saves a copy). brand/title only on the brand-grouped film looks. */
export type StoredRecipe = Recipe & { id: string; builtin: boolean; favorite?: boolean; pack?: 'recipelab'; brand?: string; title?: string };

/** The recipe alone (drops the library's bookkeeping fields). */
export function plainRecipe(r: Recipe & Partial<StoredRecipe>): Recipe {
  const { id: _i, builtin: _b, favorite: _f, pack: _p, brand: _br, title: _t, ...rest } = r;
  return rest;
}

let custom: StoredRecipe[] | null = null;
let applied: string | null = null;
let favorites = new Set<string>();
let recent: string[] = [];
/** Browsing state: groups folded in the list, and groups hidden from the quick switch / list top. */
let view: { collapsed: string[]; hidden: string[] } = { collapsed: [], hidden: [] };
const listeners = new Set<() => void>();
const notify = () => listeners.forEach((l) => l());

const builtins: StoredRecipe[] = BUILTIN_RECIPES.map((r, i) => ({ ...r, id: `builtin-${i}`, builtin: true }));

/** Film looks by brand. Ids come from the title so favourites survive a table update; names avoid ours. */
const films: StoredRecipe[] = (() => {
  const taken = new Set(builtins.map((r) => r.name.toLowerCase()));
  const list: StoredRecipe[] = [];
  for (const p of BUNDLED as PackRecipe[]) {
    let name = p.recipe.name;
    for (let n = 2; taken.has(name.toLowerCase()); n++) name = `${p.recipe.name.replace(/ \d+$/, '').slice(0, 21)} ${n}`;
    taken.add(name.toLowerCase());
    let id = `rl-${p.title.toLowerCase().replace(/[^a-z0-9]+/g, '-').replace(/^-|-$/g, '')}`;
    while (list.some((r) => r.id === id)) id += '-2';
    list.push({ ...p.recipe, name, id, builtin: true, pack: 'recipelab', brand: p.brand, title: p.title });
  }
  return list;
})();

async function load() {
  if (custom) return custom;
  try {
    const [raw, last, favs, recentRaw, viewRaw] = await Promise.all([
      AsyncStorage.getItem(KEY),
      AsyncStorage.getItem(APPLIED_KEY),
      AsyncStorage.getItem(FAVORITES_KEY),
      AsyncStorage.getItem(RECENT_KEY),
      AsyncStorage.getItem(VIEW_KEY),
    ]);
    recent = recentRaw ? JSON.parse(recentRaw) : last ? [last] : [];
    // First run: brands start folded so 70+ film looks don't bury the user's own recipes
    view = viewRaw ? { collapsed: [], hidden: [], ...JSON.parse(viewRaw) } : { collapsed: [...new Set(films.map((r) => `brand:${r.brand}`))], hidden: [] };
    rebuildViewSets();
    custom = raw ? (JSON.parse(raw) as StoredRecipe[]) : [];
    AsyncStorage.removeItem('recipes.pack.recipelab.v1').catch(() => {}); // from the download-based version
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
  return [...builtins, ...films, ...(custom ?? []).sort((a, b) => a.name.localeCompare(b.name))].map((r) => ({ ...r, favorite: favorites.has(r.id) }));
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

export async function markApplied(id: string) {
  applied = id;
  recent = pushRecent(recent, id);
  notify();
  await Promise.all([AsyncStorage.setItem(APPLIED_KEY, id), AsyncStorage.setItem(RECENT_KEY, JSON.stringify(recent))]);
}

/** Favourite or unfavourite several at once. */
export async function setFavorites(ids: string[], on: boolean) {
  await load();
  for (const id of ids) {
    if (on) favorites.add(id);
    else favorites.delete(id);
  }
  notify();
  await AsyncStorage.setItem(FAVORITES_KEY, JSON.stringify([...favorites]));
}

/** Deletes the user's own recipes among ids (built-ins are skipped); returns how many. */
export async function deleteRecipes(ids: string[]) {
  const list = await load();
  const drop = new Set(ids.filter((id) => list.some((r) => r.id === id)));
  custom = list.filter((r) => !drop.has(r.id));
  for (const id of drop) favorites.delete(id);
  recent = recent.filter((id) => !drop.has(id));
  await Promise.all([AsyncStorage.setItem(FAVORITES_KEY, JSON.stringify([...favorites])), AsyncStorage.setItem(RECENT_KEY, JSON.stringify(recent))]);
  await persist();
  return drop.size;
}

/** Stable Set views of `view` for React (rebuilt only when it changes). */
let viewSets = { collapsed: new Set<string>(), hidden: new Set<string>() };
const rebuildViewSets = () => (viewSets = { collapsed: new Set(view.collapsed), hidden: new Set(view.hidden) });

async function saveView(next: typeof view) {
  view = next;
  rebuildViewSets();
  notify();
  await AsyncStorage.setItem(VIEW_KEY, JSON.stringify(view));
}
const toggled = (list: string[], key: string, on: boolean) => (on ? [...new Set([...list, key])] : list.filter((k) => k !== key));

export async function setGroupCollapsed(key: string, on: boolean) {
  await load();
  await saveView({ ...view, collapsed: toggled(view.collapsed, key, on) });
}
export async function setAllCollapsed(keys: string[], on: boolean) {
  await load();
  await saveView({ ...view, collapsed: on ? [...new Set([...view.collapsed, ...keys])] : view.collapsed.filter((k) => !keys.includes(k)) });
}
/** A hidden group drops out of the quick switch and sinks to the bottom of the list (search still finds it). */
export async function setGroupHidden(key: string, on: boolean) {
  await load();
  await saveView({ ...view, hidden: toggled(view.hidden, key, on) });
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
  return {
    recipes: allRecipes(),
    appliedId: applied,
    recentIds: recent,
    collapsed: viewSets.collapsed,
    hidden: viewSets.hidden,
    loaded: custom !== null,
  };
}
