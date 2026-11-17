import { NativeTabs } from "expo-router/native-tabs";
import { Colors } from "@/constants/theme";
import { useThemePreference } from "@/hooks/theme-preference";
import { useI18n } from "@/i18n/i18n";

export default function AppTabs() {
  const { colorScheme } = useThemePreference();
  const { t } = useI18n();
  const colors = Colors[colorScheme];

  return (
    <NativeTabs
      backgroundColor={colors.background}
      indicatorColor={colors.backgroundElement}
      labelStyle={{ fontFamily: "Doto" }}
    >
      <NativeTabs.Trigger name="(home)">
        <NativeTabs.Trigger.Label selectedStyle={{ color: colors.text }}>
          {t("tab.home")}
        </NativeTabs.Trigger.Label>
        <NativeTabs.Trigger.Icon
          src={require("@/assets/images/tabIcons/home.png")}
          renderingMode="template"
        />
      </NativeTabs.Trigger>

      <NativeTabs.Trigger name="qr-code">
        <NativeTabs.Trigger.Label selectedStyle={{ color: colors.text }}>
          {t("tab.qr")}
        </NativeTabs.Trigger.Label>
        <NativeTabs.Trigger.Icon
          sf={{ default: "qrcode", selected: "qrcode" }}
        />
      </NativeTabs.Trigger>

      <NativeTabs.Trigger name="settings">
        <NativeTabs.Trigger.Label selectedStyle={{ color: colors.text }}>
          {t("tab.settings")}
        </NativeTabs.Trigger.Label>
        <NativeTabs.Trigger.Icon
          sf={{ default: "gearshape", selected: "gearshape.fill" }}
        />
      </NativeTabs.Trigger>
    </NativeTabs>
  );
}
