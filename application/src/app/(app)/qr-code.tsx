import { useLocalSearchParams } from "expo-router";
import { useEffect, useRef, useState } from "react";
import {
  ActivityIndicator,
  Pressable,
  ScrollView,
  StyleSheet,
  View,
} from "react-native";
import QRCode from "react-native-qrcode-svg";
import { SafeAreaView } from "react-native-safe-area-context";

import { Button } from "@/components/button";
import { ScreenTransition } from "@/components/screen-transition";
import { ThemedText } from "@/components/themed-text";
import { ThemedView } from "@/components/themed-view";
import {
  BottomTabInset,
  MaxContentWidth,
  Spacing,
  TopTabInset,
} from "@/constants/theme";
import { useHouses } from "@/hooks/use-houses";
import { useRotatingQRCode } from "@/hooks/use-rotating-qr-code";
import { useTheme } from "@/hooks/use-theme";
import { useI18n } from "@/i18n/i18n";

export default function QRCodeScreen() {
  const { houseId: requestedHouseId } = useLocalSearchParams<{
    houseId?: string;
  }>();
  const {
    houses,
    loading: housesLoading,
    error: housesError,
    reload: reloadHouses,
  } = useHouses();
  const theme = useTheme();
  const { t, recordAction } = useI18n();
  const [selection, setSelection] = useState<{
    requestedId?: string;
    selectedId?: string;
  }>({});
  const [expanded, setExpanded] = useState(false);
  const [cardWidth, setCardWidth] = useState(0);
  const selectedHouseId =
    selection.requestedId === requestedHouseId
      ? (selection.selectedId ?? requestedHouseId ?? houses[0]?.id)
      : (requestedHouseId ?? houses[0]?.id);
  const selectedHouse = houses.find((item) => item.id === selectedHouseId);
  const house = requestedHouseId ? selectedHouse : (selectedHouse ?? houses[0]);
  //added this so the screen follows signed credential expiry and confirmed door scans.
  const {
    code,
    seconds,
    error,
    loading,
    refresh,
    confirmation,
    confirmationError,
    retryConfirmation,
    returnToCode,
  } = useRotatingQRCode(house?.id);

  // Record a "QR code issued" action once per house when a code first appears (not on every
  // rotation). The ref tracks which house we've already logged for this mount.
  const recordedHouseRef = useRef<string | null>(null);
  useEffect(() => {
    if (code && house && recordedHouseRef.current !== house.id) {
      recordedHouseRef.current = house.id;
      recordAction("action.qrIssued", house.name);
    }
  }, [code, house, recordAction]);

  return (
    <ThemedView style={styles.page}>
      <SafeAreaView style={styles.safeArea} edges={["top"]}>
        <ScreenTransition>
          {confirmation && house ? (
            <ScrollView
              contentContainerStyle={[styles.container, styles.success]}
            >
              <View
                style={[
                  styles.successBadge,
                  { backgroundColor: theme.successBackground },
                ]}
                accessible={false}
              >
                <ThemedText
                  style={[styles.checkmark, { color: theme.success }]}
                >
                  ✓
                </ThemedText>
              </View>
              <View
                accessibilityLiveRegion="polite"
                accessibilityRole="alert"
                style={styles.successCopy}
              >
                <ThemedText type="title" style={styles.centered}>
                  {t("qr.accessGranted")}
                </ThemedText>
                <ThemedText type="subtitle" style={styles.centered}>
                  {house.name}
                </ThemedText>
                <ThemedText themeColor="textSecondary" style={styles.centered}>
                  {house.address}
                </ThemedText>
                <ThemedText
                  type="small"
                  themeColor="textSecondary"
                  style={styles.centered}
                >
                  {t("qr.scanConfirmed", {
                    time: new Date(
                      confirmation.occurredAt,
                    ).toLocaleTimeString(),
                  })}
                </ThemedText>
              </View>
              <Button title={t("qr.backToCode")} onPress={returnToCode} />
            </ScrollView>
          ) : (
            <ScrollView contentContainerStyle={styles.container}>
              <ThemedText type="title" style={styles.centered}>
                {t("qr.title").toUpperCase()}
              </ThemedText>
              {housesLoading ? (
                <ActivityIndicator accessibilityLabel={t("qr.loadingHouses")} />
              ) : housesError ? (
                <>
                  <ThemedText accessibilityRole="alert">
                    {housesError}
                  </ThemedText>
                  <Button title={t("qr.retryHouses")} onPress={reloadHouses} />
                </>
              ) : house ? (
                <>
                  <View style={styles.selector}>
                    <ThemedText type="smallBold">
                      {t("qr.selectHouse").toUpperCase()}
                    </ThemedText>
                    <Pressable
                      accessibilityRole="button"
                      accessibilityLabel={`${t("qr.selectHouse")}: ${house.name}`}
                      accessibilityState={{ expanded }}
                      onPress={() => setExpanded(!expanded)}
                      onAccessibilityEscape={() => setExpanded(false)}
                      style={[
                        styles.selectButton,
                        { backgroundColor: theme.backgroundElement },
                      ]}
                    >
                      <View style={styles.houseLabel}>
                        <ThemedText type="smallBold">{house.name}</ThemedText>
                        <ThemedText type="small" themeColor="textSecondary">
                          {house.address}
                        </ThemedText>
                      </View>
                      <ThemedText type="smallBold">
                        {expanded
                          ? `${t("qr.close")} ▴`
                          : `${t("qr.change")} ▾`}
                      </ThemedText>
                    </Pressable>
                    {expanded && (
                      <ThemedView
                        type="backgroundElement"
                        style={styles.options}
                      >
                        {houses.map((item) => (
                          // Render a pressable option for each house.
                          <Pressable
                            key={item.id}
                            accessibilityRole="button"
                            accessibilityState={{
                              selected: item.id === house.id,
                            }}
                            accessibilityLabel={`${item.name}, ${item.address}`}
                            onPress={() => {
                              setSelection({
                                requestedId: requestedHouseId,
                                selectedId: item.id,
                              });
                              setExpanded(false);
                            }}
                            style={[
                              styles.option,
                              item.id === house.id && {
                                backgroundColor: theme.backgroundSelected,
                              },
                            ]}
                          >
                            <View style={styles.houseLabel}>
                              <ThemedText type="smallBold">
                                {item.name}
                              </ThemedText>
                              <ThemedText
                                type="small"
                                themeColor="textSecondary"
                              >
                                {item.address}
                              </ThemedText>
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
                    onLayout={(event) =>
                      setCardWidth(event.nativeEvent.layout.width)
                    }
                  >
                    <ThemedText type="subtitle" style={styles.centered}>
                      {house.name}
                    </ThemedText>
                    <View
                      accessible={Boolean(code)}
                      accessibilityLabel={
                        code ? `${t("qr.codeLabel")} ${house.name}` : undefined
                      }
                      style={styles.qrBackground}
                    >
                      {error ? (
                        <ThemedText
                          accessibilityRole="alert"
                          style={styles.error}
                        >
                          {error}
                        </ThemedText>
                      ) : code && cardWidth > 0 ? (
                        <QRCode
                          value={code.payload}
                          size={Math.max(80, Math.min(260, cardWidth - 96))}
                          color="#000000"
                          backgroundColor="#FFFFFF"
                          quietZone={20}
                          ecl="M"
                        />
                      ) : (
                        <ActivityIndicator color="#000000" />
                      )}
                    </View>
                    {code && !error && (
                      <ThemedText type="smallBold" style={styles.countdown}>
                        {t("qr.refreshesIn")}{" "}
                        {Math.floor(seconds / 60)
                          .toString()
                          .padStart(2, "0")}
                        :{(seconds % 60).toString().padStart(2, "0")}
                      </ThemedText>
                    )}
                    {code && (
                      <ThemedText
                        type="small"
                        themeColor="textSecondary"
                        style={styles.centered}
                      >
                        {code.isDemo ? t("qr.demoCode") : t("qr.holdCode")}
                      </ThemedText>
                    )}
                  </ThemedView>
                  <Button
                    title={error ? t("qr.retry") : t("qr.refresh")}
                    onPress={refresh}
                    loading={loading}
                  />
                  {confirmationError && (
                    <View style={styles.successCopy}>
                      <ThemedText
                        type="small"
                        accessibilityRole="alert"
                        style={styles.centered}
                      >
                        {confirmationError}
                      </ThemedText>
                      <Button
                        title={t("qr.retryConfirmation")}
                        variant="secondary"
                        onPress={retryConfirmation}
                      />
                    </View>
                  )}
                </>
              ) : (
                <ThemedText themeColor="textSecondary" style={styles.centered}>
                  {requestedHouseId
                    ? t("qr.houseUnavailable")
                    : t("qr.noHouses")}
                </ThemedText>
              )}
            </ScrollView>
          )}
        </ScreenTransition>
      </SafeAreaView>
    </ThemedView>
  );
}

const styles = StyleSheet.create({
  page: { flex: 1, width: "100%" },
  safeArea: {
    flex: 1,
    width: "100%",
    maxWidth: MaxContentWidth,
    alignSelf: "center",
  },
  container: {
    paddingHorizontal: Spacing.four,
    paddingTop: TopTabInset + Spacing.four,
    paddingBottom: BottomTabInset + Spacing.four,
    gap: Spacing.four,
    width: "100%",
    maxWidth: 480,
    alignSelf: "center",
  },
  centered: { textAlign: "center" },
  selector: { gap: Spacing.two },
  selectButton: {
    flexDirection: "row",
    alignItems: "center",
    gap: Spacing.three,
    padding: Spacing.three,
    borderRadius: Spacing.three,
    minHeight: 64,
  },
  houseLabel: { flex: 1, gap: Spacing.one },
  options: { borderRadius: Spacing.three, overflow: "hidden" },
  option: {
    flexDirection: "row",
    alignItems: "center",
    gap: Spacing.two,
    padding: Spacing.three,
  },
  card: {
    alignItems: "center",
    padding: Spacing.three,
    gap: Spacing.three,
    borderRadius: Spacing.four,
  },
  qrBackground: {
    width: "100%",
    maxWidth: 300,
    backgroundColor: "#FFFFFF",
    borderRadius: Spacing.two,
    overflow: "hidden",
    minHeight: 80,
    minWidth: 80,
    alignItems: "center",
    justifyContent: "center",
  },
  success: {
    flexGrow: 1,
    justifyContent: "center",
    paddingVertical: Spacing.six,
  },
  successCopy: { gap: Spacing.three },
  successBadge: {
    width: 112,
    height: 112,
    borderRadius: 56,
    alignSelf: "center",
    alignItems: "center",
    justifyContent: "center",
  },
  checkmark: { fontSize: 64, lineHeight: 80 },
  countdown: { fontVariant: ["tabular-nums"] },
  error: { color: "#000000", textAlign: "center", padding: Spacing.three },
});
