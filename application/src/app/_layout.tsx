import { DarkTheme, DefaultTheme, ThemeProvider } from 'expo-router';
import { useFonts } from 'expo-font';
import * as SplashScreen from 'expo-splash-screen';

import { AnimatedSplashOverlay } from '@/components/animated-icon';
import AppTabs from '@/components/app-tabs';
import { ThemePreferenceProvider, useThemePreference } from '@/hooks/theme-preference';

SplashScreen.preventAutoHideAsync();

export default function TabLayout() {
  return (
    <ThemePreferenceProvider>
      <ThemedApp />
    </ThemePreferenceProvider>
  );
}

function ThemedApp() {
  const { colorScheme } = useThemePreference();
  const [fontsLoaded, fontError] = useFonts({
    Doto: require('@/assets/fonts/Doto-Variable.ttf'),
    Manrope: require('@/assets/fonts/Manrope-Variable.ttf'),
  });

  if (!fontsLoaded && !fontError) return null;

  return (
    <ThemeProvider value={colorScheme === 'dark' ? DarkTheme : DefaultTheme}>
      <AnimatedSplashOverlay />
      <AppTabs />
    </ThemeProvider>
  );
}
