import { Stack } from 'expo-router';

import { largeTitleOptions } from '@/lib/stackOptions';

export default function Layout() {
  return (
    <Stack screenOptions={largeTitleOptions}>
      <Stack.Screen name="index" options={{ title: '설정' }} />
    </Stack>
  );
}
