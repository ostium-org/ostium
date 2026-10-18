import { NativeTabs } from 'expo-router/unstable-native-tabs';
import { Colors } from '@/constants/theme';
import { useThemePreference } from '@/hooks/theme-preference';

export default function AppTabs() {
  const { colorScheme } = useThemePreference();
  const colors = Colors[colorScheme];

  return (
    <NativeTabs
      backgroundColor={colors.background}
      indicatorColor={colors.backgroundElement}
      labelStyle={{ fontFamily: 'Doto' }}>
      <NativeTabs.Trigger name="index">
        <NativeTabs.Trigger.Label selectedStyle={{ color: colors.text }}>Home</NativeTabs.Trigger.Label>
        <NativeTabs.Trigger.Icon
          src={require('@/assets/images/tabIcons/home.png')}
          renderingMode="template"
        />
      </NativeTabs.Trigger>

      <NativeTabs.Trigger name="qr-code">
        <NativeTabs.Trigger.Label selectedStyle={{ color: colors.text }}>QR Code</NativeTabs.Trigger.Label>
        <NativeTabs.Trigger.Icon sf={{ default: 'qrcode', selected: 'qrcode' }} />
      </NativeTabs.Trigger>

      <NativeTabs.Trigger name="settings">
        <NativeTabs.Trigger.Label selectedStyle={{ color: colors.text }}>Settings</NativeTabs.Trigger.Label>
        <NativeTabs.Trigger.Icon sf={{ default: 'gearshape', selected: 'gearshape.fill' }} />
      </NativeTabs.Trigger>
    </NativeTabs>
  );
}
