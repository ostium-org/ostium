import { router } from 'expo-router';
import { FlatList, StyleSheet, View } from 'react-native';
import { SafeAreaView } from 'react-native-safe-area-context';
import { Button } from '@/components/button';
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
import { useHouses } from '@/data/houses';
import { useHouseAccessRevision, userHasHouseAccess } from '@/data/house-access';
import { useAuth } from '@/hooks/auth';

export default function HomeScreen() {
  const { user } = useAuth();
  const allHouses = useHouses();
  useHouseAccessRevision();

  const houses = user?.role === 'manager'
    ? allHouses
    : allHouses.filter(
        (house) => user && userHasHouseAccess(house.id, user.id, user.email),
      );

  const isSingle = houses.length === 1;

  return (
    <ThemedView style={styles.page}>
      <SafeAreaView style={styles.safeArea} edges={['top']}>
        <ScreenTransition>
          <FlatList
            key={isSingle ? 'single' : 'grid'}
            data={houses}
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
              <View>
                <ThemedText type="title" style={styles.header}>
                  HOUSES
                </ThemedText>

                {user?.role === 'manager' && (
                  <Button
                    title="Add house"
                    onPress={() => router.push('/houses/new')}
                  />
                )}
              </View>
            }
            ListEmptyComponent={
              <ThemedText
                type="small"
                themeColor="textSecondary"
                style={styles.empty}>
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
