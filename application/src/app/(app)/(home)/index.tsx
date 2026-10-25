import { router } from 'expo-router';
import { ActivityIndicator, FlatList, StyleSheet, View } from 'react-native';
import { SafeAreaView } from 'react-native-safe-area-context';

import { HouseCard } from '@/components/house-card';
import { ScreenTransition } from '@/components/screen-transition';
import { ThemedText } from '@/components/themed-text';
import { ThemedView } from '@/components/themed-view';
import {
  BottomTabInset,
  MaxContentWidth,
  PageTitleSize,
  Spacing,
  TopTabInset,
} from '@/constants/theme';
import { useHouses } from '@/hooks/use-houses';
import { Button } from '@/components/button';

export default function HomeScreen() {
  //replaced the local demo filter with the houses this account can actually see.
  const { houses, loading, error, reload } = useHouses();
  const isSingle = houses.length === 1;

  return (
    <ThemedView style={styles.page}>
      <SafeAreaView style={styles.safeArea} edges={['top']}>
        <ScreenTransition>
          <FlatList
            key={isSingle ? 'single' : 'grid'}
            data={houses}
            refreshing={loading}
            onRefresh={reload}
            numColumns={isSingle ? 1 : 2}
            keyExtractor={(house) => house.id}
            renderItem={({ item }) => (
              <View style={isSingle ? styles.cellFull : styles.cellHalf}>
                <HouseCard
                  house={item}
                  large={isSingle}
                  onPress={(house) =>
                  router.push({
                  pathname: '/houses/[id]',
                  params: { id: house.id },
                  })
                  }
                  />
              </View>
            )}
            ListHeaderComponent={
              <ThemedText type="title" style={styles.header}>
                HOUSES
              </ThemedText>
            }
            ListEmptyComponent={
              loading ? <ActivityIndicator accessibilityLabel="Loading houses" /> : error ? (
                <View style={styles.empty}>
                  <ThemedText type="small" accessibilityRole="alert">{error}</ThemedText>
                  <Button title="Retry houses" onPress={reload} />
                </View>
              ) : <ThemedText type="small" themeColor="textSecondary" style={styles.empty}>
                No houses yet.
              </ThemedText>
            }
            contentContainerStyle={styles.listContent}
          />
        </ScreenTransition>
      </SafeAreaView>
    </ThemedView>
  );
}


const styles = StyleSheet.create({
  page: {
    flex: 1,
    width: '100%',
  },
  safeArea: {
    flex: 1,
    width: '100%',
    maxWidth: MaxContentWidth,
    alignSelf: 'center',
  },
  listContent: {
    paddingHorizontal: Spacing.two,
    paddingTop: TopTabInset,
    paddingBottom: BottomTabInset + Spacing.three,
  },
  header: {
    ...PageTitleSize,
    textAlign: 'center',
    marginTop: Spacing.one,
    marginBottom: Spacing.three,
  },
  cellHalf: {
    width: '50%',
    padding: Spacing.two,
  },
  cellFull: {
    width: '100%',
    padding: Spacing.two,
  },
  empty: {
    textAlign: 'center',
    paddingVertical: Spacing.five,
  },
});
