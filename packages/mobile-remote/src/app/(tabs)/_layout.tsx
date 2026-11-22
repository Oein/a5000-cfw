import { usePathname } from 'expo-router';
import { NativeTabs } from 'expo-router/unstable-native-tabs';
import { useWindowDimensions } from 'react-native';

import { useTabBarHidden } from '@/lib/tabBar';
import { colors } from '@/lib/theme';

/**
 * Native tab bar (UITabBarController: Liquid Glass on iOS 26+, minimises while scrolling). Each tab
 * but the shooting screen is a stack with a large title. Sideways the shooting screen goes full
 * screen, like a camera app; screens in select mode hide the bar for their own toolbar.
 */
export default function TabLayout() {
  const { width, height } = useWindowDimensions();
  const pathname = usePathname();
  const screenHides = useTabBarHidden();
  const shooting = pathname === '/';
  return (
    <NativeTabs tintColor={colors.accent} minimizeBehavior="onScrollDown" hidden={screenHides || (shooting && width > height)}>
      <NativeTabs.Trigger name="index">
        <NativeTabs.Trigger.Icon sf={{ default: 'camera', selected: 'camera.fill' }} md="photo_camera" />
        <NativeTabs.Trigger.Label>촬영</NativeTabs.Trigger.Label>
      </NativeTabs.Trigger>
      <NativeTabs.Trigger name="gallery">
        <NativeTabs.Trigger.Icon sf={{ default: 'photo.on.rectangle', selected: 'photo.fill.on.rectangle.fill' }} md="photo_library" />
        <NativeTabs.Trigger.Label>갤러리</NativeTabs.Trigger.Label>
      </NativeTabs.Trigger>
      <NativeTabs.Trigger name="recipes">
        <NativeTabs.Trigger.Icon sf={{ default: 'camera.filters', selected: 'camera.filters' }} md="palette" />
        <NativeTabs.Trigger.Label>레시피</NativeTabs.Trigger.Label>
      </NativeTabs.Trigger>
      <NativeTabs.Trigger name="library">
        <NativeTabs.Trigger.Icon sf={{ default: 'square.stack', selected: 'square.stack.fill' }} md="collections" />
        <NativeTabs.Trigger.Label>보관함</NativeTabs.Trigger.Label>
      </NativeTabs.Trigger>
      <NativeTabs.Trigger name="settings">
        <NativeTabs.Trigger.Icon sf={{ default: 'gearshape', selected: 'gearshape.fill' }} md="settings" />
        <NativeTabs.Trigger.Label>설정</NativeTabs.Trigger.Label>
      </NativeTabs.Trigger>
    </NativeTabs>
  );
}
