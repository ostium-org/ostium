// Language selector for the settings screen. Renders one row per supported locale; the active
// locale is highlighted with a check. Selecting a row updates the app-wide locale immediately.
import { Pressable, StyleSheet, View } from "react-native";

import { ThemedText } from "@/components/themed-text";
import { ThemedView } from "@/components/themed-view";
import { Spacing } from "@/constants/theme";
import { useI18n } from "@/i18n/i18n";
import { SUPPORTED_LOCALES, type Locale } from "@/i18n/translations";

export function LanguageSelector() {
  const { locale, setLocale, recordAction } = useI18n();

  function select(next: Locale) {
    if (next === locale) return;
    setLocale(next);
    recordAction("action.languageChanged");
  }

  return (
    <View style={styles.list}>
      {SUPPORTED_LOCALES.map((entry) => {
        const selected = entry.code === locale;
        return (
          <Pressable
            key={entry.code}
            accessibilityRole="button"
            accessibilityState={{ selected }}
            onPress={() => select(entry.code)}
            style={({ pressed }) => [styles.row, pressed && styles.pressed]}
          >
            <ThemedView
              type={selected ? "backgroundSelected" : "backgroundElement"}
              style={styles.rowInner}
            >
              <View style={styles.labels}>
                <ThemedText
                  type="smallBold"
                  themeColor={selected ? "text" : "textSecondary"}
                >
                  {entry.nativeLabel}
                </ThemedText>
                <ThemedText type="small" themeColor="textSecondary">
                  {entry.label}
                </ThemedText>
              </View>
              {selected ? (
                <ThemedText type="smallBold" themeColor="accent">
                  ✓
                </ThemedText>
              ) : null}
            </ThemedView>
          </Pressable>
        );
      })}
    </View>
  );
}

const styles = StyleSheet.create({
  list: {
    gap: Spacing.one,
  },
  row: {
    borderRadius: Spacing.two,
    overflow: "hidden",
  },
  rowInner: {
    flexDirection: "row",
    alignItems: "center",
    justifyContent: "space-between",
    paddingVertical: Spacing.two,
    paddingHorizontal: Spacing.three,
    borderRadius: Spacing.two,
  },
  labels: {
    gap: 0,
  },
  pressed: {
    opacity: 0.75,
  },
});
