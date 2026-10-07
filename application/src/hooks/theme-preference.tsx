import { createContext, useContext, useState, type ReactNode } from 'react';

export type ColorScheme = 'light' | 'dark';

const ThemePreferenceContext = createContext<{
  colorScheme: ColorScheme;
  setColorScheme: (scheme: ColorScheme) => void;
} | null>(null);

export function ThemePreferenceProvider({ children }: { children: ReactNode }) {
  const [colorScheme, setColorScheme] = useState<ColorScheme>('dark');

  return (
    <ThemePreferenceContext.Provider value={{ colorScheme, setColorScheme }}>
      {children}
    </ThemePreferenceContext.Provider>
  );
}

export function useThemePreference() {
  const preference = useContext(ThemePreferenceContext);
  if (!preference) {
    throw new Error('useThemePreference must be used inside ThemePreferenceProvider');
  }
  return preference;
}
