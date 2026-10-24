import { router, useLocalSearchParams } from 'expo-router';
import { Pressable, ScrollView, StyleSheet } from 'react-native';
import { SafeAreaView } from 'react-native-safe-area-context';

import { ThemedText } from '@/components/themed-text';
import { ThemedView } from '@/components/themed-view';
import {
  BottomTabInset,
  MaxContentWidth,
  Spacing,
  TopTabInset,
} from '@/constants/theme';
import { HOUSES } from '@/data/houses';

export default function HouseDetailsScreen() {
  const { id } = useLocalSearchParams<{ id: string }>();
  const house = HOUSES.find((item) => item.id === id);

  function returnToHouses() {
    if (router.canGoBack()) {
      router.back();
    } else {
      router.replace('/');
    }
  }

  return (
    <ThemedView style={styles.page}>
      <SafeAreaView style={styles.safeArea} edges={['top']}>
        <ScrollView contentContainerStyle={styles.content}>
          <Pressable
            accessibilityRole="button"
            onPress={returnToHouses}
            style={({ pressed }) => [
              styles.backButton,
              pressed && styles.pressed,
            ]}>
            <ThemedText type="smallBold">Back to houses</ThemedText>
          </Pressable>

          {house ? (
            <>
              <ThemedText type="title">{house.name}</ThemedText>

              <ThemedView
                type="backgroundElement"
                style={styles.details}>
                <ThemedText type="smallBold">Address</ThemedText>
                <ThemedText>{house.address}</ThemedText>
              </ThemedView>
            </>
          ) : (
            <>
              <ThemedText type="title">House not found</ThemedText>
              <ThemedText>
                This house could not be found. Return to the house list
                and select another property.
              </ThemedText>
            </>
          )}
        </ScrollView>
      </SafeAreaView>
    </ThemedView>
  );
}

const styles = StyleSheet.create({
  page: {
    flex: 1,
  },
  safeArea: {
    flex: 1,
    width: '100%',
    maxWidth: MaxContentWidth,
    alignSelf: 'center',
  },
  content: {
    paddingHorizontal: Spacing.three,
    paddingTop: TopTabInset + Spacing.three,
    paddingBottom: BottomTabInset + Spacing.three,
    gap: Spacing.three,
  },
  backButton: {
    alignSelf: 'flex-start',
    paddingVertical: Spacing.two,
    paddingHorizontal: Spacing.two,
  },
  details: {
    borderRadius: Spacing.three,
    padding: Spacing.three,
    gap: Spacing.two,
  },
  pressed: {
    opacity: 0.75,
  },
});