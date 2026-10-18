import { useState } from 'react';
import { ActivityIndicator, Pressable, ScrollView, StyleSheet, View } from 'react-native';
import QRCode from 'react-native-qrcode-svg';
import { SafeAreaView } from 'react-native-safe-area-context';

import { ScreenTransition } from '@/components/screen-transition';
import { ThemedText } from '@/components/themed-text';
import { ThemedView } from '@/components/themed-view';
import { BottomTabInset, MaxContentWidth, Spacing, TopTabInset } from '@/constants/theme';
import { HOUSES } from '@/data/houses';
import { useRotatingQRCode } from '@/hooks/use-rotating-qr-code';
import { useTheme } from '@/hooks/use-theme';

export default function QRCodeScreen() {
  const theme = useTheme();
  const [selectedHouseId, setSelectedHouseId] = useState(HOUSES[0]?.id);
  const [expanded, setExpanded] = useState(false);
  const [cardWidth, setCardWidth] = useState(0);
  const house = HOUSES.find((item) => item.id === selectedHouseId);
  const { code, seconds, error } = useRotatingQRCode(house?.id);

  return (
    <ThemedView style={styles.page}>
      <SafeAreaView style={styles.safeArea} edges={['top']}>
        <ScreenTransition>
          <ScrollView contentContainerStyle={styles.container}>
            <ThemedText type="title" style={styles.centered}>QR CODE</ThemedText>
            {house ? (
              <>
                <View style={styles.selector}>
                  <ThemedText type="smallBold">HOUSE</ThemedText>
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
                    <ThemedText>{expanded ? '▴' : '▾'}</ThemedText>
                  </Pressable>
                  {expanded && (
                    <ThemedView type="backgroundElement" style={styles.options}>
                      {HOUSES.map((item) => (
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
                    accessible
                    accessibilityLabel={`Demo QR code for ${house.name}`}
                    style={styles.qrBackground}>
                    {error ? (
                      <ThemedText style={styles.error}>Unable to generate code. Retrying…</ThemedText>
                    ) : code && cardWidth > 0 ? (
                      <QRCode
                        value={JSON.stringify(code)}
                        size={Math.max(80, Math.min(260, cardWidth - 96))}
                        color="#000000"
                        backgroundColor="#FFFFFF"
                        quietZone={20}
                        ecl="M"
                      />
                    ) : <ActivityIndicator color="#000000" />}
                  </View>
                  {code && !error && (
                    <ThemedText type="smallBold" style={styles.countdown}>
                      Refreshes in {Math.floor(seconds / 60).toString().padStart(2, '0')}:{(seconds % 60).toString().padStart(2, '0')}
                    </ThemedText>
                  )}
                  <ThemedText type="small" themeColor="textSecondary" style={styles.centered}>
                    Hold this code in front of the door camera.
                  </ThemedText>
                </ThemedView>
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
    backgroundColor: '#FFFFFF',
    borderRadius: Spacing.two,
    overflow: 'hidden',
    minHeight: 80,
    minWidth: 80,
    alignItems: 'center',
    justifyContent: 'center',
  },
  countdown: { fontVariant: ['tabular-nums'] },
  error: { color: '#000000', textAlign: 'center', padding: Spacing.three },
});
