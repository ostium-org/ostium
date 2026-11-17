// SETTINGS SCREEN (route "/settings")
//
// The overhauled settings page. Sections, top to bottom:
//   1. Profile      — display name, email, and role badge
//   2. Language     — pick the app language (i18n)
//   3. Theme        — light / dark
//   4. Recent       — recent activity log (sign-in, password change, QR issuance, …)
//   5. Help         — FAQ, user guide, contact, about
//   6. Account      — signed-in email, change password, sign out
//
// Everything is translated through the i18n provider, so switching language re-renders the whole
// page. Change password is an inline expandable form (no modal dependency).
import { useState } from "react";
import { Pressable, ScrollView, StyleSheet, View } from "react-native";
import { SafeAreaView } from "react-native-safe-area-context";

import { ChangePassword } from "@/components/change-password";
import { HelpMenu } from "@/components/help-menu";
import { LanguageSelector } from "@/components/language-selector";
import { RecentActions } from "@/components/recent-actions";
import { RoleBadge } from "@/components/role-badge";
import { ScreenTransition } from "@/components/screen-transition";
import { Button } from "@/components/button";
import { ThemedText } from "@/components/themed-text";
import { ThemedView } from "@/components/themed-view";
import { Colors, PageTitleSize, Spacing } from "@/constants/theme";
import { useAuth } from "@/hooks/auth";
import { useThemePreference } from "@/hooks/theme-preference";
import { useI18n } from "@/i18n/i18n";

export default function SettingsScreen() {
  const { colorScheme, setColorScheme } = useThemePreference();
  const { user, signOut } = useAuth();
  const { t, recordAction } = useI18n();

  const [showChangePassword, setShowChangePassword] = useState(false);
  const [passwordChanged, setPasswordChanged] = useState(false);

  function handleThemeChange(scheme: "light" | "dark") {
    if (scheme === colorScheme) return;
    setColorScheme(scheme);
    recordAction("action.themeChanged");
  }

  function handleSignOut() {
    recordAction("action.signOut");
    void signOut();
  }

  function handlePasswordDone() {
    setPasswordChanged(true);
    setShowChangePassword(false);
    setTimeout(() => setPasswordChanged(false), 4000);
  }

  return (
    <ThemedView style={styles.page}>
      <SafeAreaView style={styles.safeArea} edges={["top"]}>
        <ScreenTransition>
          <ScrollView
            contentContainerStyle={styles.container}
            showsVerticalScrollIndicator={false}
            keyboardShouldPersistTaps="handled"
          >
            <ThemedText type="title" style={styles.header}>
              {t("settings.title").toUpperCase()}
            </ThemedText>

            {/* 1. Profile */}
            <ThemedView type="backgroundElement" style={styles.card}>
              <View style={styles.sectionHeading}>
                <ThemedText type="subtitle">
                  {t("settings.profile.title")}
                </ThemedText>
              </View>
              <View style={styles.profileRow}>
                <View style={styles.profileInfo}>
                  <ThemedText type="smallBold">
                    {user?.name ?? user?.email}
                  </ThemedText>
                  <ThemedText type="small" themeColor="textSecondary">
                    {user?.email}
                  </ThemedText>
                </View>
                {user ? <RoleBadge role={user.role} /> : null}
              </View>
            </ThemedView>

            {/* 2. Language */}
            <ThemedView type="backgroundElement" style={styles.card}>
              <View style={styles.sectionHeading}>
                <ThemedText type="subtitle">
                  {t("settings.language.title")}
                </ThemedText>
                <ThemedText type="small" themeColor="textSecondary">
                  {t("settings.language.subtitle")}
                </ThemedText>
              </View>
              <LanguageSelector />
            </ThemedView>

            {/* 3. Theme */}
            <ThemedView type="backgroundElement" style={styles.card}>
              <View style={styles.sectionHeading}>
                <ThemedText type="subtitle">
                  {t("settings.theme.title")}
                </ThemedText>
                <ThemedText type="small" themeColor="textSecondary">
                  {t("settings.theme.subtitle")}
                </ThemedText>
              </View>
              <View style={styles.themeOptions}>
                {(["light", "dark"] as const).map((scheme) => {
                  const selected = colorScheme === scheme;
                  return (
                    <Pressable
                      key={scheme}
                      accessibilityRole="button"
                      accessibilityState={{ selected }}
                      onPress={() => handleThemeChange(scheme)}
                      style={({ pressed }) => [
                        styles.themeOption,
                        pressed && styles.pressed,
                      ]}
                    >
                      <ThemedView
                        type={selected ? "backgroundSelected" : "background"}
                        style={styles.themeOptionInner}
                      >
                        <View
                          style={[
                            styles.themePreview,
                            { backgroundColor: Colors[scheme].background },
                          ]}
                        >
                          <View
                            style={[
                              styles.previewLine,
                              { backgroundColor: Colors[scheme].text },
                            ]}
                          />
                          <View
                            style={[
                              styles.previewLineShort,
                              { backgroundColor: Colors[scheme].textSecondary },
                            ]}
                          />
                        </View>
                        <ThemedText
                          type="smallBold"
                          themeColor={selected ? "text" : "textSecondary"}
                        >
                          {t(
                            scheme === "light"
                              ? "settings.theme.light"
                              : "settings.theme.dark",
                          )}
                        </ThemedText>
                      </ThemedView>
                    </Pressable>
                  );
                })}
              </View>
            </ThemedView>

            {/* 4. Recent activity */}
            <ThemedView type="backgroundElement" style={styles.card}>
              <View style={styles.sectionHeading}>
                <ThemedText type="subtitle">
                  {t("settings.recent.title")}
                </ThemedText>
              </View>
              <RecentActions />
            </ThemedView>

            {/* 5. Help */}
            <ThemedView type="backgroundElement" style={styles.card}>
              <View style={styles.sectionHeading}>
                <ThemedText type="subtitle">
                  {t("settings.help.title")}
                </ThemedText>
              </View>
              <HelpMenu />
            </ThemedView>

            {/* 6. Account */}
            <ThemedView type="backgroundElement" style={styles.card}>
              <View style={styles.sectionHeading}>
                <ThemedText type="subtitle">
                  {t("settings.account.title")}
                </ThemedText>
                <ThemedText type="small" themeColor="textSecondary">
                  {t("settings.account.signedInAs", {
                    email: user?.email ?? "",
                  })}
                </ThemedText>
              </View>

              {passwordChanged ? (
                <ThemedText type="small" themeColor="success" role="status">
                  {t("password.success")}
                </ThemedText>
              ) : null}

              <Pressable
                accessibilityRole="button"
                accessibilityState={{ expanded: showChangePassword }}
                onPress={() => setShowChangePassword((v) => !v)}
                style={({ pressed }) => [
                  styles.accountRow,
                  pressed && styles.pressed,
                ]}
              >
                <ThemedView
                  type="backgroundElement"
                  style={styles.accountRowInner}
                >
                  <ThemedText type="smallBold">
                    {t("settings.account.changePassword")}
                  </ThemedText>
                  <ThemedText type="smallBold" themeColor="textSecondary">
                    {showChangePassword ? "▴" : "▾"}
                  </ThemedText>
                </ThemedView>
              </Pressable>

              {showChangePassword ? (
                <ChangePassword onDone={handlePasswordDone} />
              ) : null}

              <Button
                title={t("settings.account.signOut").toUpperCase()}
                variant="secondary"
                onPress={handleSignOut}
              />
            </ThemedView>
          </ScrollView>
        </ScreenTransition>
      </SafeAreaView>
    </ThemedView>
  );
}

const styles = StyleSheet.create({
  page: {
    flex: 1,
    width: "100%",
  },
  safeArea: {
    flex: 1,
    width: "100%",
  },
  container: {
    padding: Spacing.four,
    gap: Spacing.five,
    width: "100%",
    maxWidth: 640,
    alignSelf: "center",
    paddingBottom: Spacing.six,
  },
  header: {
    marginTop: Spacing.one,
    ...PageTitleSize,
    textAlign: "center",
  },
  card: {
    padding: Spacing.three,
    borderRadius: Spacing.three,
    gap: Spacing.three,
  },
  sectionHeading: {
    gap: Spacing.half,
    paddingBottom: Spacing.one,
  },
  profileRow: {
    flexDirection: "row",
    alignItems: "center",
    justifyContent: "space-between",
    gap: Spacing.two,
  },
  profileInfo: {
    flex: 1,
    gap: 0,
  },
  themeOptions: {
    flexDirection: "row",
    gap: Spacing.two,
  },
  themeOption: {
    flex: 1,
    borderRadius: Spacing.two,
    overflow: "hidden",
  },
  themeOptionInner: {
    alignItems: "center",
    gap: Spacing.two,
    padding: Spacing.two,
    borderRadius: Spacing.two,
  },
  themePreview: {
    width: "100%",
    height: 64,
    borderRadius: Spacing.one,
    justifyContent: "center",
    paddingHorizontal: Spacing.three,
    gap: Spacing.two,
  },
  previewLine: {
    width: "70%",
    height: 4,
    borderRadius: 2,
  },
  previewLineShort: {
    width: "42%",
    height: 4,
    borderRadius: 2,
  },
  accountRow: {
    borderRadius: Spacing.two,
    overflow: "hidden",
  },
  accountRowInner: {
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
