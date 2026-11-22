import type { ComponentProps } from 'react';
import type { Stack } from 'expo-router';

import { colors } from '@/lib/theme';

type Options = NonNullable<ComponentProps<typeof Stack>['screenOptions']>;

/** iOS-style headers: large title that collapses on scroll, transparent bar over the content. */
export const largeTitleOptions: Options = {
  headerLargeTitle: true,
  headerTransparent: true,
  headerLargeTitleShadowVisible: false,
  headerShadowVisible: false,
  headerTintColor: colors.accent,
  headerTitleStyle: { color: colors.text },
  headerLargeTitleStyle: { color: colors.text },
  contentStyle: { backgroundColor: colors.background },
};
