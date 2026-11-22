import Ionicons from '@expo/vector-icons/Ionicons';
import { Pressable, StyleSheet, Text, View } from 'react-native';

import { Swatch } from '@/components/recipe/Controls';
import { colors } from '@/lib/theme';
import { displayName, type Listed, type Section } from '@/features/recipes/groups';
import { EFFECT_LABELS, formatThirds, STYLE_LABELS, type Recipe } from '@/features/recipes/model';
import { swatchColors } from '@/features/recipes/swatch';

/** Rows and section headers shared by the recipe tab and the quick-switch sheet. */

/** Up to three short tags. */
export function recipeTags(r: Recipe) {
  const t = [STYLE_LABELS[r.style]];
  if (r.effect !== 'off') t.push(EFFECT_LABELS[r.effect]);
  else if (typeof r.whiteBalance === 'number') t.push(`${r.whiteBalance}K`);
  if (r.exposureThirds) t.push(`${formatThirds(r.exposureThirds)}EV`);
  else if (r.saturation) t.push(`채도${r.saturation > 0 ? '+' : ''}${r.saturation}`);
  return t.slice(0, 3);
}

const SECTION_ICON: Record<string, keyof typeof Ionicons.glyphMap> = {
  recent: 'time-outline',
  fav: 'star',
  mine: 'person-outline',
  builtin: 'cube-outline',
  search: 'search',
};

/**
 * A group header: tap folds/unfolds it. Folded, it previews a few of its looks so a brand is
 * recognisable without opening it.
 */
export function SectionHeader({
  section,
  preview,
  onToggle,
  onMenu,
  right,
  plain,
}: {
  section: Section;
  /** no background (in a glass sheet); default is opaque for sticky headers */
  plain?: boolean;
  /** recipes to preview while folded */
  preview?: Listed[];
  onToggle?: () => void;
  onMenu?: () => void;
  right?: React.ReactNode;
}) {
  const icon = SECTION_ICON[section.key] ?? 'film-outline';
  return (
    <Pressable
      style={({ pressed }) => [styles.header, plain && { backgroundColor: 'transparent' }, pressed && onToggle && { opacity: 0.7 }]}
      onPress={onToggle}
      disabled={!onToggle}
      accessibilityRole="button"
      accessibilityState={{ expanded: !section.collapsed }}
      accessibilityLabel={`${section.title} ${section.count}개${section.collapsed ? ', 접힘' : ''}`}>
      <Ionicons name={icon} size={15} color={section.key === 'fav' ? colors.accent : colors.textDim} />
      <Text style={[styles.headerTitle, section.hidden && { color: colors.textDim }]} numberOfLines={1}>
        {section.title}
      </Text>
      <Text style={styles.headerCount}>{section.count}</Text>
      {section.hidden ? <Ionicons name="eye-off-outline" size={14} color={colors.textDim} /> : null}
      <View style={{ flex: 1 }} />
      {section.collapsed && preview?.length ? (
        <View style={styles.previewRow}>
          {preview.slice(0, 5).map((r) => (
            <Swatch key={r.id} stops={swatchColors(r)} size={16} radius={4} />
          ))}
        </View>
      ) : null}
      {right}
      {onMenu ? (
        <Pressable hitSlop={10} onPress={onMenu} accessibilityLabel={`${section.title} 메뉴`} style={styles.headerMenu}>
          <Ionicons name="ellipsis-horizontal" size={17} color={colors.textDim} />
        </Pressable>
      ) : null}
      {onToggle ? <Ionicons name={section.collapsed ? 'chevron-forward' : 'chevron-down'} size={16} color={colors.textDim} /> : null}
    </Pressable>
  );
}

export function RecipeRow({
  recipe,
  applied,
  compact,
  selecting,
  selected,
  showGroup,
  onPress,
  onLongPress,
  right,
}: {
  recipe: Listed;
  applied?: boolean;
  compact?: boolean;
  selecting?: boolean;
  selected?: boolean;
  /** brand tag (useful in mixed lists: search, favourites, recent) */
  showGroup?: boolean;
  onPress: () => void;
  onLongPress?: () => void;
  right?: React.ReactNode;
}) {
  const tags = recipeTags(recipe);
  if (showGroup && recipe.brand) tags.unshift(recipe.brand);
  return (
    <Pressable
      style={({ pressed }) => [styles.row, compact && styles.rowCompact, applied && styles.rowApplied, selected && styles.rowSelected, pressed && { opacity: 0.7 }]}
      onPress={onPress}
      onLongPress={onLongPress}
      accessibilityState={selecting ? { checked: !!selected } : undefined}
      accessibilityHint={onLongPress && !selecting ? '길게 누르면 메뉴' : undefined}>
      {selecting ? <Ionicons name={selected ? 'checkmark-circle' : 'ellipse-outline'} size={22} color={selected ? colors.accent : colors.textDim} /> : null}
      <Swatch stops={swatchColors(recipe)} size={compact ? 34 : 40} radius={compact ? 8 : 10} />
      <View style={styles.rowText}>
        <View style={styles.nameRow}>
          <Text style={[styles.name, compact && { fontSize: 15 }]} numberOfLines={1}>
            {displayName(recipe)}
          </Text>
          {recipe.favorite ? <Ionicons name="star" size={12} color={colors.accent} /> : null}
        </View>
        <Text style={styles.tags} numberOfLines={1}>
          {tags.slice(0, 4).join(' · ')}
        </Text>
      </View>
      {right}
    </Pressable>
  );
}

const styles = StyleSheet.create({
  header: { flexDirection: 'row', alignItems: 'center', gap: 6, minHeight: 40, paddingHorizontal: 6, backgroundColor: colors.background },
  headerTitle: { color: colors.text, fontSize: 14, fontWeight: '700', flexShrink: 1 },
  headerCount: { color: colors.textDim, fontSize: 12, fontVariant: ['tabular-nums'] },
  headerMenu: { paddingHorizontal: 4 },
  previewRow: { flexDirection: 'row', gap: 3, marginRight: 4 },
  row: { flexDirection: 'row', alignItems: 'center', gap: 10, backgroundColor: colors.panel, borderRadius: 12, borderCurve: 'continuous', paddingVertical: 8, paddingHorizontal: 10, marginBottom: 4, borderWidth: 1.5, borderColor: 'transparent' },
  rowCompact: { backgroundColor: 'transparent', paddingVertical: 6, marginBottom: 2 },
  rowApplied: { borderColor: colors.ok },
  rowSelected: { borderColor: colors.accent },
  rowText: { flex: 1, gap: 3 },
  nameRow: { flexDirection: 'row', alignItems: 'center', gap: 5 },
  name: { color: colors.text, fontSize: 16, fontWeight: '600', flexShrink: 1 },
  tags: { color: colors.textDim, fontSize: 12 },
});
