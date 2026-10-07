/**
 * Learn more about light and dark modes:
 * https://docs.expo.dev/guides/color-schemes/
 */

import { Colors } from '@/constants/theme';
import { useThemePreference } from '@/hooks/theme-preference';

export function useTheme() {
  const { colorScheme } = useThemePreference();
  return Colors[colorScheme];
}
