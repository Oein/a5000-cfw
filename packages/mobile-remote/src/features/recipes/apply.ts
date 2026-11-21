/**
 * Recipe → camera changes. Pure TypeScript.
 *
 * Sign conventions differ: recipe files put amber and green positive (Recipe Lab), the camera
 * link uses wbAB amber(-)..blue(+) and wbGM green(-)..magenta(+) (ILCE Remote), so both flip.
 * The picture effect's B&W posterization is "posterization-bw" in files but
 * "posterization-mono" in the camera's runtime list.
 */

import type { CameraChanges, CameraState } from '@/lib/camera';

import type { Recipe } from './model.ts';

export function cameraOption(effectOption: string | null) {
  return effectOption === 'posterization-bw' ? 'posterization-mono' : effectOption;
}

export function recipeChanges(r: Recipe, evStep = 1 / 3): CameraChanges {
  const changes: CameraChanges = {
    creativeStyle: r.style,
    saturation: r.saturation,
    contrast: r.contrast,
    sharpness: r.sharpness,
    pictureEffect: r.effect,
    dro: String(r.dro),
    ev: Math.round(r.exposureThirds / 3 / evStep),
  };
  const option = cameraOption(r.effectOption);
  if (option) changes.pictureEffectOption = option;
  if (r.whiteBalance !== 'keep') {
    changes.whiteBalance = r.whiteBalance === 'auto' ? 'auto' : 'color-temp';
    if (typeof r.whiteBalance === 'number') changes.colorTemperature = r.whiteBalance;
    changes.wbAB = -r.amberBlue || 0;
    changes.wbGM = -r.greenMagenta || 0;
  }
  return changes;
}

/** Things to tell the user before applying (e.g. effects need JPEG). */
export function applyWarnings(r: Recipe, state: CameraState | null): string[] {
  const out: string[] = [];
  if (r.effect !== 'off') out.push('픽처 이펙트는 JPEG에서만 적용돼요 (RAW/RAW+JPEG이면 카메라가 무시)');
  if (state?.pictureEffectValues && r.effect !== 'off' && !state.pictureEffectValues.includes(r.effect)) {
    out.push(`이 카메라 모드에서 "${r.effect}" 효과를 쓸 수 없어요`);
  }
  if (state?.creativeStyleValues && !state.creativeStyleValues.includes(r.style)) {
    out.push(`이 카메라는 "${r.style}" 스타일이 없어요`);
  }
  if (state && state.mode === 'auto') out.push('인텔리전트 자동 모드에서는 크리에이티브 스타일이 적용되지 않아요');
  return out;
}
