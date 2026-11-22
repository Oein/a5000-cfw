import { router } from 'expo-router';
import { Alert } from 'react-native';

import type { CameraClient } from '@/lib/camera';
import { toast, toastError } from '@/lib/toast';

import { applyWarnings } from './apply';
import { applyRecipe } from './applyToCamera';
import { recipeFromCamera } from './fromCamera';
import type { Recipe } from './model';
import { deleteRecipe, freeName, plainRecipe, type StoredRecipe } from './store';

/** Shared recipe actions for the list, the quick picker on the shooting screen, etc. */

export async function applyWithFeedback(client: CameraClient, recipe: Recipe & { id?: string }) {
  try {
    const state = await client.state().catch(() => null);
    const warnings = applyWarnings(recipe, state);
    await applyRecipe(client, recipe);
    toast(warnings.length ? `"${recipe.name}" 적용 — ${warnings[0]}` : `"${recipe.name}" 적용됨`, warnings.length ? 'info' : 'ok');
    return true;
  } catch (e) {
    toastError('적용 실패', e);
    return false;
  }
}

export function openEditor(recipe: StoredRecipe) {
  router.push({ pathname: '/recipe/[id]', params: { id: recipe.id } });
}

export function openDuplicate(recipe: Recipe) {
  const clean = plainRecipe(recipe);
  router.push({ pathname: '/recipe/[id]', params: { id: 'new', draft: JSON.stringify({ ...clean, name: freeName(clean.name) }) } });
}

export function openShare(recipe: Recipe) {
  router.push({ pathname: '/recipe/share', params: { draft: JSON.stringify(plainRecipe(recipe)) } });
}

/** "Bottle" what the camera is set to now into a new recipe (opens the editor). */
export async function newFromCamera(client: CameraClient) {
  try {
    const state = await client.state();
    const { recipe, unknown } = recipeFromCamera(state, freeName('Camera Look'));
    if (unknown.length) toast(`카메라에서 못 읽은 값: ${unknown.join(', ')} — 기본값으로 채웠어요`, 'info');
    router.push({ pathname: '/recipe/[id]', params: { id: 'new', draft: JSON.stringify(recipe) } });
  } catch (e) {
    toastError('카메라 설정을 못 읽었어요', e);
  }
}

export function confirmDelete(recipe: StoredRecipe) {
  Alert.alert(`"${recipe.name}" 삭제?`, '폰에서만 지워져요.', [
    { text: '취소', style: 'cancel' },
    {
      text: '삭제',
      style: 'destructive',
      onPress: async () => {
        await deleteRecipe(recipe.id);
        toast('삭제됨', 'info');
      },
    },
  ]);
}
