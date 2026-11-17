// Help menu for the settings screen. A set of tappable rows that open external links (user guide,
// support) or show an inline "about" note. Links use `expo-web-browser` so they open in the system
// browser rather than navigating the app away.
import { Pressable, StyleSheet, View } from "react-native";
import Constants from "expo-constants";
import * as WebBrowser from "expo-web-browser";

import { ThemedText } from "@/components/themed-text";
import { ThemedView } from "@/components/themed-view";
import { Spacing } from "@/constants/theme";
import { useI18n } from "@/i18n/i18n";

// These point at the project's own docs. In a real deployment these would be public URLs; for now
// they're placeholders that the team can swap for hosted pages.
const GUIDE_URL =
  "https://github.com/ostium/ostium/blob/main/docs/user-guide.md";
const FAQ_URL =
  "https://github.com/ostium/ostium/blob/main/docs/maintenance-guide.md";
const CONTACT_URL = "mailto:support@ostium.example";

type HelpItem = {
  key:
    | "settings.help.faq"
    | "settings.help.guide"
    | "settings.help.contact"
    | "settings.help.about";
  url?: string;
};

const ITEMS: HelpItem[] = [
  { key: "settings.help.faq", url: FAQ_URL },
  { key: "settings.help.guide", url: GUIDE_URL },
  { key: "settings.help.contact", url: CONTACT_URL },
  { key: "settings.help.about" },
];

export function HelpMenu() {
  const { t } = useI18n();

  async function open(url: string) {
    try {
      await WebBrowser.openBrowserAsync(url);
    } catch {
      // Some environments (e.g. tests) can't open a browser; fail silently.
    }
  }

  const version = Constants.expoConfig?.version ?? "1.0.0";

  return (
    <View style={styles.list}>
      {ITEMS.map((item) =>
        item.url ? (
          <Pressable
            key={item.key}
            accessibilityRole="button"
            onPress={() => void open(item.url)}
            style={({ pressed }) => [styles.row, pressed && styles.pressed]}
          >
            <ThemedView type="backgroundElement" style={styles.rowInner}>
              <ThemedText type="smallBold">{t(item.key)}</ThemedText>
              <ThemedText type="smallBold" themeColor="textSecondary">
                ›
              </ThemedText>
            </ThemedView>
          </Pressable>
        ) : (
          <View key={item.key} style={styles.row}>
            <ThemedView type="backgroundElement" style={styles.rowInner}>
              <ThemedText type="smallBold">{t(item.key)}</ThemedText>
              <ThemedText type="small" themeColor="textSecondary">
                v{version}
              </ThemedText>
            </ThemedView>
          </View>
        ),
      )}
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
  pressed: {
    opacity: 0.75,
  },
});
