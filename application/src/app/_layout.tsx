// ROOT LAYOUT
// The first thing Expo Router renders; everything else renders inside it.
// It sets up the app-wide providers (theme, sign-in state), loads fonts, and decides which screens
// a visitor is allowed to reach:
//   signed out -> only "login"
//   signed in  -> only the main app, the "(app)" group (home / QR code / settings tabs)

import { DarkTheme, DefaultTheme, Stack, ThemeProvider } from 'expo-router';
import { useFonts } from 'expo-font';
import * as SplashScreen from 'expo-splash-screen';

import { AnimatedSplashOverlay } from '@/components/animated-icon';
import { Colors } from '@/constants/theme';
import { AuthProvider, useAuth } from '@/hooks/auth';
import { ThemePreferenceProvider, useThemePreference } from '@/hooks/theme-preference';

// Keep the native splash screen up until the app is ready (AnimatedSplashOverlay hides it).
SplashScreen.preventAutoHideAsync();

export default function RootLayout() {
  // Order matters: ThemedApp reads both the theme and the sign-in state, so both providers wrap it.
  return (
    <ThemePreferenceProvider>
      <AuthProvider>
        <ThemedApp />
      </AuthProvider>
    </ThemePreferenceProvider>
  );
}

function ThemedApp() {
  const { colorScheme } = useThemePreference();
  // `user` is null when signed out. `isLoading` is true while we check device storage for a saved session.
  const { user, isLoading } = useAuth();

  const [fontsLoaded, fontError] = useFonts({
    Doto: require('@/assets/fonts/Doto-Variable.ttf'),
    Manrope: require('@/assets/fonts/Manrope-Variable.ttf'),
  });

  // Wait for fonts AND the saved-session check. Without the second wait, a signed-in user would see
  // the login screen flash by on every launch. (The native splash screen stays up meanwhile.)
  if ((!fontsLoaded && !fontError) || isLoading) return null;

  return (
    <ThemeProvider value={colorScheme === 'dark' ? DarkTheme : DefaultTheme}>
      <AnimatedSplashOverlay />
      <Stack
        screenOptions={{
          headerShown: false, // our screens draw their own titles
          contentStyle: { backgroundColor: Colors[colorScheme].background }, // no white flash between screens
        }}>
        {/* Route guards: a screen inside <Stack.Protected guard={false}> can't be reached at all
            (by tapping, typing a URL, or a deep link). When `user` changes, the router moves the
            person to whichever side is now allowed. This is a convenience for the UI, not security:
            the backend must still check the token on every request. */}
        <Stack.Protected guard={!!user}>
          <Stack.Screen name="(app)" />
        </Stack.Protected>
        <Stack.Protected guard={!user}>
          <Stack.Screen name="login" />
        </Stack.Protected>
      </Stack>
    </ThemeProvider>
  );
}