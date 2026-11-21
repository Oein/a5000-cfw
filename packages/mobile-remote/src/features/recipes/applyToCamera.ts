import type { CameraClient } from '@/lib/camera';

import { recipeChanges } from './apply';
import type { Recipe } from './model';
import { markApplied } from './store';

/** Sends a recipe's settings to the camera and remembers it as the applied one. */
export async function applyRecipe(client: CameraClient, recipe: Recipe & { id?: string }) {
  const state = await client.state().catch(() => null);
  const result = await client.apply(recipeChanges(recipe, state?.evStep ?? 1 / 3));
  if (recipe.id) await markApplied(recipe.id);
  return result;
}
