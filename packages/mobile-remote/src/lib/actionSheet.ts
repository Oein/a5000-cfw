import { ActionSheetIOS, Alert, Platform } from 'react-native';

export type SheetAction = { title: string; onPress: () => void; destructive?: boolean };

/** Native action sheet on iOS, an Alert with buttons elsewhere. */
export function showActions(title: string | undefined, actions: SheetAction[]) {
  if (Platform.OS === 'ios') {
    const destructive = actions.findIndex((a) => a.destructive);
    ActionSheetIOS.showActionSheetWithOptions(
      {
        title,
        options: [...actions.map((a) => a.title), '취소'],
        cancelButtonIndex: actions.length,
        destructiveButtonIndex: destructive >= 0 ? destructive : undefined,
        userInterfaceStyle: 'dark',
      },
      (i) => {
        if (i < actions.length) actions[i].onPress();
      },
    );
    return;
  }
  Alert.alert(title ?? '', undefined, [
    ...actions.map((a) => ({ text: a.title, onPress: a.onPress, style: a.destructive ? ('destructive' as const) : ('default' as const) })),
    { text: '취소', style: 'cancel' as const },
  ]);
}
