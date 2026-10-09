import { StyleSheet } from 'react-native';
import { SafeAreaView } from 'react-native-safe-area-context';

import { ThemedText } from '@/components/themed-text';
import { ThemedView } from '@/components/themed-view';
import { ScreenTransition } from '@/components/screen-transition';
import { Spacing } from '@/constants/theme';

export default function QRCodeScreen() {
  return (
    <ThemedView style={styles.page}>
      <SafeAreaView style={styles.safeArea} edges={['top']}>
        <ScreenTransition>
          <ThemedView style={styles.container}>
            <ThemedText type="title">QR CODE</ThemedText>
            <ThemedText type="small" themeColor="textSecondary" style={styles.caption}>
              SCAN OR SHARE YOUR CODE
            </ThemedText>
          </ThemedView>
        </ScreenTransition>
      </SafeAreaView>
    </ThemedView>
  );
}

const styles = StyleSheet.create({
  safeArea: {
    flex: 1,
    width: '100%',
  },
  page: {
    flex: 1,
    width: '100%',
  },
  container: {
    flex: 1,
    alignItems: 'center',
    justifyContent: 'flex-start',
    padding: Spacing.four,
    gap: Spacing.three,
    paddingTop: Spacing.four,
  },
  caption: {
    letterSpacing: 1,
  },
});
