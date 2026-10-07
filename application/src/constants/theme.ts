/**
 * Below are the colors that are used in the app. The colors are defined in the light and dark mode.
 * There are many other ways to style your app. For example, [Nativewind](https://www.nativewind.dev/), [Tamagui](https://tamagui.dev/), [unistyles](https://reactnativeunistyles.vercel.app), etc.
 */

import '@/global.css';

import { Platform } from 'react-native';

export const Colors = {
  light: {
    text: '#4C4F69',
    background: '#EFF1F5',
    backgroundElement: '#E6E9EF',
    backgroundSelected: '#CCD0DA',
    textSecondary: '#5C5F77',
    accent: '#8839EF',
  },
  dark: {
    text: '#CDD6F4',
    background: '#1E1E2E',
    backgroundElement: '#181825',
    backgroundSelected: '#313244',
    textSecondary: '#BAC2DE',
    accent: '#CBA6F7',
  },
} as const;

export type ThemeColor = keyof typeof Colors.light & keyof typeof Colors.dark;

export const Fonts =
  Platform.select({
    web: {
      display: 'var(--font-display)',
      body: 'var(--font-body)',
    },
    default: {
      display: 'Doto',
      body: 'Manrope',
    },
  }) ?? { display: 'Doto', body: 'Manrope' };

export const Spacing = {
  half: 2,
  one: 4,
  two: 8,
  three: 16,
  four: 24,
  five: 32,
  six: 64,
} as const;

export const PageTitleSize = {
  fontSize: 30,
  lineHeight: 36,
} as const;

export const BottomTabInset = Platform.select({ ios: 50, android: 80 }) ?? 0;
export const MaxContentWidth = 800;
