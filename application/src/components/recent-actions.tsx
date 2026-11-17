// Recent activity list for the settings screen. Reads the action log from the i18n provider
// (which owns persistence) and renders each entry with a translated label and a relative time.
import { Pressable, StyleSheet, View } from "react-native";

import { ThemedText } from "@/components/themed-text";
import { ThemedView } from "@/components/themed-view";
import { Spacing } from "@/constants/theme";
import { useI18n, type RecentAction } from "@/i18n/i18n";

function relativeTime(at: number, now: number): string {
  const seconds = Math.max(0, Math.floor((now - at) / 1000));
  if (seconds < 60) return `${seconds}s`;
  const minutes = Math.floor(seconds / 60);
  if (minutes < 60) return `${minutes}m`;
  const hours = Math.floor(minutes / 60);
  if (hours < 24) return `${hours}h`;
  const days = Math.floor(hours / 24);
  return `${days}d`;
}

function ActionRow({ action, now }: { action: RecentAction; now: number }) {
  const { t } = useI18n();
  return (
    <View style={styles.row}>
      <View style={styles.rowText}>
        <ThemedText type="smallBold">{t(action.labelKey)}</ThemedText>
        {action.detail ? (
          <ThemedText type="small" themeColor="textSecondary">
            {action.detail}
          </ThemedText>
        ) : null}
      </View>
      <ThemedText type="small" themeColor="textSecondary">
        {relativeTime(action.at, now)}
      </ThemedText>
    </View>
  );
}

export function RecentActions() {
  const { t, recentActions, clearActions } = useI18n();
  const now = Date.now();

  return (
    <View style={styles.container}>
      {recentActions.length === 0 ? (
        <ThemedText
          type="small"
          themeColor="textSecondary"
          style={styles.empty}
        >
          {t("settings.recent.empty")}
        </ThemedText>
      ) : (
        <>
          <View style={styles.list}>
            {recentActions.map((action) => (
              <ActionRow key={action.id} action={action} now={now} />
            ))}
          </View>
          <Pressable
            accessibilityRole="button"
            accessibilityLabel={t("settings.recent.clear")}
            onPress={clearActions}
            hitSlop={Spacing.two}
            style={({ pressed }) => [styles.clear, pressed && styles.pressed]}
          >
            <ThemedText type="smallBold" themeColor="danger">
              {t("settings.recent.clear")}
            </ThemedText>
          </Pressable>
        </>
      )}
    </View>
  );
}

const styles = StyleSheet.create({
  container: {
    gap: Spacing.two,
  },
  list: {
    gap: Spacing.two,
  },
  row: {
    flexDirection: "row",
    alignItems: "center",
    justifyContent: "space-between",
    gap: Spacing.two,
  },
  rowText: {
    flex: 1,
    gap: 0,
  },
  empty: {
    paddingVertical: Spacing.one,
  },
  clear: {
    alignSelf: "flex-end",
    paddingVertical: Spacing.half,
    paddingHorizontal: Spacing.two,
  },
  pressed: {
    opacity: 0.75,
  },
});
