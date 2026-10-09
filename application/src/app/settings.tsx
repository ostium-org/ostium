import { Pressable, StyleSheet, View } from 'react-native';
import { SafeAreaView } from 'react-native-safe-area-context';

import { ScreenTransition } from '@/components/screen-transition';
import { ThemedText } from '@/components/themed-text';
import { ThemedView } from '@/components/themed-view';
import { Colors, PageTitleSize, Spacing } from '@/constants/theme';
import { useThemePreference } from '@/hooks/theme-preference';

export default function SettingsScreen() {
  const { colorScheme, setColorScheme } = useThemePreference();

  return (
    <ThemedView style={styles.page}>
      <SafeAreaView style={styles.safeArea} edges={['top']}>
        <ScreenTransition>
          <ThemedView style={styles.container}>
            <ThemedText type="title" style={styles.header}>
              SETTINGS
            </ThemedText>
            <ThemedView type="backgroundElement" style={styles.settingCard}>
              <View style={styles.sectionHeading}>
                <ThemedText type="subtitle">Theme</ThemedText>
                <ThemedText type="small" themeColor="textSecondary">
                  Choose your preferred appearance.
                </ThemedText>
              </View>
              <View style={styles.themeOptions}>
                {(['light', 'dark'] as const).map((scheme) => {
                  const selected = colorScheme === scheme;
                  return (
                    <Pressable
                      key={scheme}
                      accessibilityRole="button"
                      accessibilityState={{ selected }}
                      onPress={() => setColorScheme(scheme)}
                      style={({ pressed }) => [styles.themeOption, pressed && styles.pressed]}>
                      <ThemedView
                        type={selected ? 'backgroundSelected' : 'background'}
                        style={styles.themeOptionInner}>
                        <View
                          style={[styles.themePreview, { backgroundColor: Colors[scheme].background }]}>
                          <View style={[styles.previewLine, { backgroundColor: Colors[scheme].text }]} />
                          <View
                            style={[
                              styles.previewLineShort,
                              { backgroundColor: Colors[scheme].textSecondary },
                            ]}
                          />
                        </View>
                        <ThemedText type="smallBold" themeColor={selected ? 'text' : 'textSecondary'}>
                          {scheme === 'light' ? 'LIGHT' : 'DARK'}
                        </ThemedText>
                      </ThemedView>
                    </Pressable>
                  );
                })}
              </View>
            </ThemedView>
          </ThemedView>
        </ScreenTransition>
      </SafeAreaView>
    </ThemedView>
  );
}

const styles = StyleSheet.create({
  page: {
    flex: 1,
    width: '100%',
  },
  safeArea: {
    flex: 1,
    width: '100%',
  },
  container: {
    flex: 1,
    padding: Spacing.four,
    gap: Spacing.five,
    width: '100%',
    maxWidth: 640,
    alignSelf: 'center',
  },
  header: {
    marginTop: Spacing.one,
    ...PageTitleSize,
    textAlign: 'center',
  },
  settingCard: {
    padding: Spacing.three,
    borderRadius: Spacing.three,
    gap: Spacing.three,
  },
  sectionHeading: {
    alignItems: 'center',
    gap: Spacing.one,
    paddingBottom: Spacing.one,
  },
  themeOptions: {
    flexDirection: 'row',
    gap: Spacing.two,
  },
  themeOption: {
    flex: 1,
    borderRadius: Spacing.two,
    overflow: 'hidden',
  },
  themeOptionInner: {
    alignItems: 'center',
    gap: Spacing.two,
    padding: Spacing.two,
    borderRadius: Spacing.two,
  },
  themePreview: {
    width: '100%',
    height: 64,
    borderRadius: Spacing.one,
    justifyContent: 'center',
    paddingHorizontal: Spacing.three,
    gap: Spacing.two,
  },
  previewLine: {
    width: '70%',
    height: 4,
    borderRadius: 2,
  },
  previewLineShort: {
    width: '42%',
    height: 4,
    borderRadius: 2,
  },
  pressed: {
    opacity: 0.75,
  },
});
