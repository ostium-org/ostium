import { useState } from 'react';
import { ActivityIndicator, Pressable, ScrollView, StyleSheet, View } from 'react-native';
import { Image } from 'expo-image';
import QRCode from 'react-native-qrcode-svg';
import { SafeAreaView } from 'react-native-safe-area-context';

import { Button } from '@/components/button';
import { ScreenTransition } from '@/components/screen-transition';
import { ThemedText } from '@/components/themed-text';
import { ThemedView } from '@/components/themed-view';
import { BottomTabInset, MaxContentWidth, Spacing, TopTabInset } from '@/constants/theme';
import { useHouses } from '@/hooks/use-houses';
import { useRotatingQRCode } from '@/hooks/use-rotating-qr-code';
import { useTheme } from '@/hooks/use-theme';

// Set to false when the live QR endpoint is ready. Placeholder mode makes no requests.
const USE_QR_PLACEHOLDER = true;

export default function QRCodeScreen() {
  const { houses, loading: housesLoading, error: housesError, reload: reloadHouses } = useHouses();
  const theme = useTheme();
  const [selectedHouseId, setSelectedHouseId] = useState(houses[0]?.id);
  const [expanded, setExpanded] = useState(false);
  const [cardWidth, setCardWidth] = useState(0);
  const selectedHouse = houses.find((item) => item.id === selectedHouseId);
  //fall back to an available house if the previous selection is no longer in the list.
  const house = selectedHouse ?? houses[0];
  const { code, seconds, error, loading, refresh } = useRotatingQRCode(
    USE_QR_PLACEHOLDER ? undefined : house?.id,
  );

  return (
    <ThemedView style={styles.page}>
      <SafeAreaView style={styles.safeArea} edges={['top']}>
        <ScreenTransition>
          <ScrollView contentContainerStyle={styles.container}>
            <ThemedText type="title" style={styles.centered}>QR CODE</ThemedText>
            {housesLoading ? <ActivityIndicator accessibilityLabel="Loading houses" /> : housesError ? (
              <>
                <ThemedText accessibilityRole="alert">{housesError}</ThemedText>
                <Button title="Retry houses" onPress={reloadHouses} />
              </>
            ) : house ? (
              <>
                <View style={styles.selector}>
                  <ThemedText type="smallBold">SELECT HOUSE</ThemedText>
                  <Pressable
                    accessibilityRole="button"
                    accessibilityLabel={`Select house. Current house: ${house.name}`}
                    accessibilityState={{ expanded }}
                    onPress={() => setExpanded(!expanded)}
                    onAccessibilityEscape={() => setExpanded(false)}
                    style={[styles.selectButton, { backgroundColor: theme.backgroundElement }]}>
                    <View style={styles.houseLabel}>
                      <ThemedText type="smallBold">{house.name}</ThemedText>
                      <ThemedText type="small" themeColor="textSecondary">{house.address}</ThemedText>
                    </View>
                    <ThemedText type="smallBold">{expanded ? 'Close ▴' : 'Change ▾'}</ThemedText>
                  </Pressable>
                  {expanded && (
                    <ThemedView type="backgroundElement" style={styles.options}>
                      {houses.map((item) => (
                        // Render a pressable option for each house.
                        <Pressable
                          key={item.id}
                          accessibilityRole="button"
                          accessibilityState={{ selected: item.id === house.id }}
                          accessibilityLabel={`${item.name}, ${item.address}`}
                          onPress={() => {
                            setSelectedHouseId(item.id);
                            setExpanded(false);
                          }}
                          style={[
                            styles.option,
                            item.id === house.id && { backgroundColor: theme.backgroundSelected },
                          ]}>
                          <View style={styles.houseLabel}>
                            <ThemedText type="smallBold">{item.name}</ThemedText>
                            <ThemedText type="small" themeColor="textSecondary">{item.address}</ThemedText>
                          </View>
                          {item.id === house.id && <ThemedText>✓</ThemedText>}
                        </Pressable>
                      ))}
                    </ThemedView>
                  )}
                </View>
                <ThemedView
                  type="backgroundElement"
                  style={styles.card}
                  onLayout={(event) => setCardWidth(event.nativeEvent.layout.width)}>
                  <ThemedText type="subtitle" style={styles.centered}>{house.name}</ThemedText>
                  <View
                    accessible={!USE_QR_PLACEHOLDER && Boolean(code)}
                    accessibilityLabel={!USE_QR_PLACEHOLDER && code ? `${code.isDemo ? 'Demo QR code' : 'QR code'} for ${house.name}` : undefined}
                    style={styles.qrBackground}>
                    {USE_QR_PLACEHOLDER ? <Image
                      source={require('../../../assets/images/qr-placeholder.png')}
                      style={styles.qrImage}
                      contentFit="contain"
                      accessible
                      accessibilityLabel="Placeholder QR code"
                    /> : error ? (
                      <ThemedText accessibilityRole="alert" style={styles.error}>{error}</ThemedText>
                    ) : code && cardWidth > 0 ? (
                      <QRCode
                        value={code.payload}
                        size={Math.max(80, Math.min(260, cardWidth - 96))}
                        color="#000000"
                        backgroundColor="#FFFFFF"
                        quietZone={20}
                        ecl="M"
                      />
                    ) : <ActivityIndicator color="#000000" />}
                  </View>
                  {!USE_QR_PLACEHOLDER && code && !error && (
                    <ThemedText type="smallBold" style={styles.countdown}>
                      Refreshes in {Math.floor(seconds / 60).toString().padStart(2, '0')}:{(seconds % 60).toString().padStart(2, '0')}
                    </ThemedText>
                  )}
                  {(USE_QR_PLACEHOLDER || code) && (
                    <ThemedText type="small" themeColor="textSecondary" style={styles.centered}>
                      {USE_QR_PLACEHOLDER ? 'Placeholder QR code' : code?.isDemo ? 'Demo code — door unlocking is not connected.' : 'Hold this code in front of the door camera.'}
                    </ThemedText>
                  )}
                </ThemedView>
                {!USE_QR_PLACEHOLDER && (
                  <Button
                    title={error ? 'Retry QR code' : 'Refresh QR code'}
                    onPress={refresh}
                    loading={loading}
                  />
                )}
              </>
            ) : (
              <ThemedText themeColor="textSecondary" style={styles.centered}>
                No houses available.
              </ThemedText>
            )}
          </ScrollView>
        </ScreenTransition>
      </SafeAreaView>
    </ThemedView>
  );
}

const styles = StyleSheet.create({
  page: { flex: 1, width: '100%' },
  safeArea: { flex: 1, width: '100%', maxWidth: MaxContentWidth, alignSelf: 'center' },
  container: {
    paddingHorizontal: Spacing.four,
    paddingTop: TopTabInset + Spacing.four,
    paddingBottom: BottomTabInset + Spacing.four,
    gap: Spacing.four,
    width: '100%',
    maxWidth: 480,
    alignSelf: 'center',
  },
  centered: { textAlign: 'center' },
  selector: { gap: Spacing.two },
  selectButton: {
    flexDirection: 'row',
    alignItems: 'center',
    gap: Spacing.three,
    padding: Spacing.three,
    borderRadius: Spacing.three,
    minHeight: 64,
  },
  houseLabel: { flex: 1, gap: Spacing.one },
  options: { borderRadius: Spacing.three, overflow: 'hidden' },
  option: {
    flexDirection: 'row',
    alignItems: 'center',
    gap: Spacing.two,
    padding: Spacing.three,
  },
  card: {
    alignItems: 'center',
    padding: Spacing.three,
    gap: Spacing.three,
    borderRadius: Spacing.four,
  },
  qrBackground: {
    width: '100%',
    maxWidth: 300,
    backgroundColor: '#FFFFFF',
    borderRadius: Spacing.two,
    overflow: 'hidden',
    minHeight: 80,
    minWidth: 80,
    alignItems: 'center',
    justifyContent: 'center',
  },
  qrImage: { width: '100%', aspectRatio: 1 },
  countdown: { fontVariant: ['tabular-nums'] },
  error: { color: '#000000', textAlign: 'center', padding: Spacing.three },
});
