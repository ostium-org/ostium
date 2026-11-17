// Recent activity list for the settings screen. Reads the action log from the i18n provider
// (which owns persistence) and renders each entry with a translated label and a relative time.
import { useMemo } from "react";
import { Pressable, StyleSheet, View } from "react-native";

import { ThemedText } from "@/components/themed-text";
import { Spacing } from "@/constants/theme";
import { useI18n, type RecentAction } from "@/i18n/i18n";

// Format a stored timestamp as a short, deterministic time-of-day label (e.g. "14:32").
// Derived purely from the stored `at` value so it is stable across re-renders (react-hooks/purity).
function formatTime(at: number): string {
  const date = new Date(at);
  const hours = date.getHours().toString().padStart(2, "0");
  const minutes = date.getMinutes().toString().padStart(2, "0");
  return `${hours}:${minutes}`;
}

function ActionRow({
  action,
  timeLabel: label,
}: {
  action: RecentAction;
  timeLabel: string;
}) {
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
        {label}
      </ThemedText>
    </View>
  );
}

export function RecentActions() {
  const { t, recentActions, clearActions } = useI18n();

  // Format each row's time label deterministically from its stored timestamp.
  const rows = useMemo(
    () =>
      recentActions.map((action) => ({
        ...action,
        timeLabel: formatTime(action.at),
      })),
    [recentActions],
  );

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
            {rows.map((row) => (
              <ActionRow key={row.id} action={row} timeLabel={row.timeLabel} />
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
