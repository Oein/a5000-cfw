/**
 * Camera state ↔ recipe values. Pure (runs under `node --test`).
 *  - recipeFromCamera: "bottle" the look the camera has now (set it up in the camera's own menus,
 *    then save it as a recipe) — the inverse of apply.recipeChanges.
 *  - lookSnapshot: what to restore when a live preview is abandoned.
 */

import type { CameraChanges, CameraState } from '../../lib/camera.ts';

import { defaultRecipe, EFFECT_OPTIONS, EFFECTS, STYLES, type Dro, type Effect, type Recipe, type Style } from './model.ts';

export function recipeFromCamera(state: CameraState, name: string): { recipe: Recipe; unknown: string[] } {
  const r = defaultRecipe(name);
  const unknown: string[] = [];
  const look = state.look;
  if (look) {
    if ((STYLES as readonly string[]).includes(look.creativeStyle)) r.style = look.creativeStyle as Style;
    else unknown.push(`스타일 "${look.creativeStyle}"`);
    r.saturation = look.saturation;
    r.contrast = look.contrast;
    r.sharpness = look.sharpness;
    if ((EFFECTS as readonly string[]).includes(look.pictureEffect)) {
      r.effect = look.pictureEffect as Effect;
      const options = EFFECT_OPTIONS[r.effect];
      const option = look.pictureEffectOption === 'posterization-mono' ? 'posterization-bw' : look.pictureEffectOption;
      r.effectOption = options ? (option && options.includes(option) ? option : options[0]) : null;
    } else unknown.push(`이펙트 "${look.pictureEffect}"`);
    const dro = look.dro === 'off' || look.dro === 'auto' ? look.dro : Number(look.dro);
    r.dro = (typeof dro === 'number' && (dro < 1 || dro > 5) ? 'auto' : dro) as Dro;
  } else unknown.push('룩(스타일 등) — 카메라가 알려주지 않음');

  if (state.whiteBalance === 'auto') r.whiteBalance = 'auto';
  else if (state.whiteBalance === 'color-temp' && state.colorTemperature) r.whiteBalance = Math.round(state.colorTemperature / 100) * 100;
  else r.whiteBalance = 'keep'; // a preset (daylight, shade…) the recipe format can't name
  r.amberBlue = -(state.wbAB ?? 0) || 0;
  r.greenMagenta = -(state.wbGM ?? 0) || 0;
  r.exposureThirds = Math.max(-15, Math.min(15, Math.round(state.ev * state.evStep * 3)));
  return { recipe: r, unknown };
}

/** The camera's current look as changes that put it back. */
export function lookSnapshot(state: CameraState): CameraChanges {
  const c: CameraChanges = {
    whiteBalance: state.whiteBalance,
    ev: state.ev,
    wbAB: state.wbAB ?? 0,
    wbGM: state.wbGM ?? 0,
  };
  if (state.colorTemperature) c.colorTemperature = state.colorTemperature;
  if (state.look) {
    c.creativeStyle = state.look.creativeStyle;
    c.saturation = state.look.saturation;
    c.contrast = state.look.contrast;
    c.sharpness = state.look.sharpness;
    c.pictureEffect = state.look.pictureEffect;
    if (state.look.pictureEffectOption) c.pictureEffectOption = state.look.pictureEffectOption;
    c.dro = state.look.dro;
  }
  return c;
}
