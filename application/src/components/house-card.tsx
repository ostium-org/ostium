import { SymbolView } from 'expo-symbols';
import { Pressable, StyleSheet, View } from 'react-native';

import { ThemedText } from '@/components/themed-text';
import { ThemedView } from '@/components/themed-view';
import { Spacing } from '@/constants/theme';
import type { House } from '@/data/houses';
import { useTheme } from '@/hooks/use-theme';

type HouseCardProps = {
  house: House;
  /** Renders the big single-house variant instead of the compact grid variant. */
  large?: boolean;
  onPress?: (house: House) => void;
};

export function HouseCard({ house, large = false, onPress }: HouseCardProps) {
  const theme = useTheme();

  return (
    <Pressable
      accessibilityRole="button"
      accessibilityLabel={`${house.name}, ${house.address}`}
      onPress={() => onPress?.(house)}
      style={({ pressed }) => pressed && styles.pressed}>
      <ThemedView type="backgroundElement" style={styles.card}>
        <ThemedView
          type="backgroundSelected"
          style={[styles.media, large ? styles.mediaLarge : styles.mediaSmall]}>
          <SymbolView
            name={{ ios: 'house.fill', android: 'home', web: 'home' }}
            size={large ? 72 : 40}
            tintColor={theme.accent}
          />
        </ThemedView>

        <View style={[styles.body, large && styles.bodyLarge]}>
          <ThemedText type={large ? 'subtitle' : 'smallBold'} numberOfLines={1}>
            {house.name}
          </ThemedText>
          <ThemedText type="small" themeColor="textSecondary" numberOfLines={2}>
            {house.address}
          </ThemedText>
        </View>
      </ThemedView>
    </Pressable>
  );
}

const styles = StyleSheet.create({
  card: {
    borderRadius: Spacing.three,
    overflow: 'hidden',
  },
  media: {
    alignItems: 'center',
    justifyContent: 'center',
  },
  mediaSmall: {
    aspectRatio: 1,
  },
  mediaLarge: {
    aspectRatio: 16 / 9,
  },
  body: {
    padding: Spacing.three,
    gap: Spacing.half,
  },
  bodyLarge: {
    padding: Spacing.four,
    gap: Spacing.one,
  },
  pressed: {
    opacity: 0.75,
  },
});