import { Stack } from 'expo-router';

import { largeTitleOptions } from '@/lib/stackOptions';
import { colors } from '@/lib/theme';

export default function Layout() {
  return (
    <Stack screenOptions={largeTitleOptions}>
      <Stack.Screen name="index" options={{ title: '갤러리', headerLargeTitle: false, headerTransparent: false, headerStyle: { backgroundColor: colors.background } }} />
    </Stack>
  );
}
