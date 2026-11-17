// A small pill that shows the signed-in user's role (tenant / manager / admin) using the
// translated label. Kept as its own component so the settings screen stays readable.
import { ThemedText } from "@/components/themed-text";
import { ThemedView } from "@/components/themed-view";
import { Spacing } from "@/constants/theme";
import { useI18n } from "@/i18n/i18n";
import type { UserRole } from "@/services/api";

const ROLE_KEY: Record<
  UserRole,
  | "settings.profile.role.tenant"
  | "settings.profile.role.manager"
  | "settings.profile.role.admin"
> = {
  tenant: "settings.profile.role.tenant",
  manager: "settings.profile.role.manager",
  admin: "settings.profile.role.admin",
};

export function RoleBadge({ role }: { role: UserRole }) {
  const { t } = useI18n();
  return (
    <ThemedView type="backgroundSelected" style={styles.badge}>
      <ThemedText type="smallBold" themeColor="accent">
        {t(ROLE_KEY[role])}
      </ThemedText>
    </ThemedView>
  );
}

const styles = {
  badge: {
    alignSelf: "flex-start",
    borderRadius: Spacing.two,
    paddingHorizontal: Spacing.two,
    paddingVertical: Spacing.half,
  },
};
