import { defaultRecipe, type Recipe } from './model';

/**
 * Starter recipes: the camera's own creative styles at default settings, plus a few looks of our
 * own. Deliberately small — the big library is imported (YAML files / share links), not bundled.
 */
const make = (name: string, changes: Partial<Recipe>): Recipe => ({ ...defaultRecipe(name), ...changes });

export const BUILTIN_RECIPES: Recipe[] = [
  make('Standard', {}),
  make('Vivid', { style: 'vivid' }),
  make('Neutral', { style: 'neutral' }),
  make('Portrait', { style: 'portrait' }),
  make('Landscape', { style: 'landscape' }),
  make('Mono', { style: 'mono' }),
  make('Warm Evening', { style: 'standard', saturation: 1, whiteBalance: 6500, amberBlue: 2, exposureThirds: -1 }),
  make('Cool Morning', { style: 'neutral', contrast: 1, whiteBalance: 4800, amberBlue: -1, greenMagenta: -1 }),
  make('Faded', { style: 'light', saturation: -1, contrast: -2, dro: 3 }),
  make('Punchy Mono', { style: 'mono', contrast: 3, sharpness: 1, dro: 'off' }),
];
