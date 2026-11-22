import { EFFECT_LABELS, STYLE_LABELS, type Recipe } from './model.ts';

/**
 * How the recipe library is grouped and searched, shared by the recipe tab and the quick-switch
 * sheet. Groups: the user's own, our built-ins, then one per Recipe Lab brand.
 *
 * Pure TypeScript (runs under `node --test`).
 */

export type Listed = Recipe & { id: string; builtin: boolean; favorite?: boolean; pack?: string; brand?: string; title?: string };

export const MINE = 'mine';
export const BUILTIN = 'builtin';

export function groupOf(r: Listed): string {
  if (!r.builtin) return MINE;
  return r.brand ? `brand:${r.brand}` : BUILTIN;
}

export function groupLabel(key: string) {
  if (key === MINE) return '내 레시피';
  if (key === BUILTIN) return '기본';
  return key.replace(/^brand:/, '');
}

export const displayName = (r: Listed) => r.title ?? r.name;

/** Group keys in library order: mine, built-in, then brands as they come. */
export function groupKeys(recipes: Listed[]) {
  const keys = [MINE, BUILTIN];
  for (const r of recipes) {
    const k = groupOf(r);
    if (!keys.includes(k)) keys.push(k);
  }
  return keys.filter((k) => recipes.some((r) => groupOf(r) === k));
}

/** Matches every word of the query against name, title, brand, style and effect (Korean labels too). */
export function matches(r: Listed, query: string) {
  const words = query.trim().toLowerCase().split(/\s+/).filter(Boolean);
  if (!words.length) return true;
  const hay = [r.name, r.title ?? '', r.brand ?? '', groupLabel(groupOf(r)), r.style, STYLE_LABELS[r.style], r.effect === 'off' ? '' : `${r.effect} ${EFFECT_LABELS[r.effect]}`, typeof r.whiteBalance === 'number' ? `${r.whiteBalance}k` : '']
    .join(' ')
    .toLowerCase();
  return words.every((w) => hay.includes(w));
}

/** data = the rows to render (empty when folded); items = all of the section's recipes. */
export type Section<T extends Listed = Listed> = { key: string; title: string; count: number; collapsed: boolean; hidden: boolean; data: T[]; items: T[] };

/**
 * Sections for browsing: "recent" and "favorites" shortcuts on top (their recipes also stay in their
 * own group), then every group. Collapsed sections keep their count but carry no rows; hidden groups
 * go last (or are dropped with dropHidden). A search flattens everything into one result section.
 */
export function buildSections<T extends Listed>(
  recipes: T[],
  opts: { query?: string; recentIds?: string[]; recentMax?: number; collapsed?: ReadonlySet<string>; hidden?: ReadonlySet<string>; dropHidden?: boolean },
): Section<T>[] {
  const collapsed = opts.collapsed ?? new Set<string>();
  const hidden = opts.hidden ?? new Set<string>();
  const query = opts.query?.trim() ?? '';
  if (query) {
    const found = recipes.filter((r) => matches(r, query));
    return [{ key: 'search', title: `검색 결과`, count: found.length, collapsed: false, hidden: false, data: found, items: found }];
  }
  const sections: Section<T>[] = [];
  const byId = new Map(recipes.map((r) => [r.id, r]));
  const recent = (opts.recentIds ?? []).map((id) => byId.get(id)).filter((r): r is T => !!r).slice(0, opts.recentMax ?? 5);
  const shortcut = (key: string, title: string, data: T[]) => {
    if (data.length) sections.push({ key, title, count: data.length, collapsed: collapsed.has(key), hidden: false, data: collapsed.has(key) ? [] : data, items: data });
  };
  shortcut('recent', '최근 사용', recent);
  shortcut('fav', '즐겨찾기', recipes.filter((r) => r.favorite));
  const groups = groupKeys(recipes);
  const ordered = [...groups.filter((k) => !hidden.has(k)), ...(opts.dropHidden ? [] : groups.filter((k) => hidden.has(k)))];
  for (const key of ordered) {
    const data = recipes.filter((r) => groupOf(r) === key);
    const isCollapsed = collapsed.has(key);
    sections.push({ key, title: groupLabel(key), count: data.length, collapsed: isCollapsed, hidden: hidden.has(key), data: isCollapsed ? [] : data, items: data });
  }
  return sections;
}

/** Recently applied ids, newest first, without repeats. */
export function pushRecent(list: string[], id: string, max = 10) {
  return [id, ...list.filter((x) => x !== id)].slice(0, max);
}
