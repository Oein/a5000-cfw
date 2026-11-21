import { EFFECT_LABELS, formatThirds, signed, STYLE_LABELS, type Recipe } from './model';

/** One line describing a recipe: "비비드 · 채도+2 · 5600K · EV+0.7". */
export function recipeSummary(r: Recipe) {
  const parts: string[] = [STYLE_LABELS[r.style]];
  if (r.saturation) parts.push(`채도${signed(r.saturation)}`);
  if (r.contrast) parts.push(`대비${signed(r.contrast)}`);
  if (r.sharpness) parts.push(`선명도${signed(r.sharpness)}`);
  if (r.effect !== 'off') parts.push(EFFECT_LABELS[r.effect]);
  if (typeof r.whiteBalance === 'number') parts.push(`${r.whiteBalance}K`);
  else if (r.whiteBalance === 'keep') parts.push('WB 유지');
  if (r.amberBlue || r.greenMagenta) parts.push(`A${signed(r.amberBlue)}/G${signed(r.greenMagenta)}`);
  if (r.exposureThirds) parts.push(`EV${formatThirds(r.exposureThirds)}`);
  if (r.dro !== 'auto') parts.push(`DRO ${r.dro === 'off' ? '끔' : `Lv${r.dro}`}`);
  return parts.join(' · ');
}
