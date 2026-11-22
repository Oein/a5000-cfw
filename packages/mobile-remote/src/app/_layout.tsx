// Background tasks and the camera session start at bundle load, before (or without) any screen:
// iOS may relaunch the app in the background for location
import '@/features/sync/backgroundTask';
import { initSession, useSession } from '@/features/session/session';

import { Stack } from 'expo-router/stack';
import { StatusBar } from 'expo-status-bar';

import { CameraProvider, useCamera } from '@/lib/CameraProvider';
import { colors } from '@/lib/theme';
import { ToastHost } from '@/lib/toast';
import { useForegroundSync } from '@/features/sync/syncEngine';

initSession();

/** Keeps the phone's library in step with the camera while the app is open. */
function ForegroundSync() {
  const { client, connected } = useCamera();
  const session = useSession();
  useForegroundSync(client, connected, session.link === 'linked');
  return null;
}

export default function RootLayout() {
  return (
    <CameraProvider>
      <ForegroundSync />
      <StatusBar style="light" />
      <Stack
        screenOptions={{
          headerStyle: { backgroundColor: colors.background },
          headerShadowVisible: false,
          headerTintColor: colors.accent,
          headerTitleStyle: { color: colors.text },
          contentStyle: { backgroundColor: colors.background },
          headerBackButtonDisplayMode: 'minimal',
        }}>
        <Stack.Screen name="(tabs)" options={{ headerShown: false }} />
        <Stack.Screen name="photo/[id]" options={{ title: '사진' }} />
        <Stack.Screen name="movie/[id]" options={{ title: '동영상' }} />
        <Stack.Screen name="recipe/[id]" options={{ title: '레시피' }} />
        <Stack.Screen name="recipe/import" options={{ title: '레시피 가져오기', presentation: 'modal' }} />
        <Stack.Screen name="recipe/share" options={{ title: '레시피 공유', presentation: 'modal' }} />
        <Stack.Screen name="library/[key]" options={{ title: '보관함' }} />
      </Stack>
      <ToastHost />
    </CameraProvider>
  );
}
