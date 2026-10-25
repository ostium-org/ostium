import { router, useLocalSearchParams } from 'expo-router';
import { useState } from 'react';
import { ActivityIndicator, Pressable, ScrollView, StyleSheet, TextInput, View } from 'react-native';
import { SafeAreaView } from 'react-native-safe-area-context';

import { Button } from '@/components/button';
import { ThemedText } from '@/components/themed-text';
import { ThemedView } from '@/components/themed-view';
import { useAuth } from '@/hooks/auth';
import { useTheme } from '@/hooks/use-theme';
import {
  getHouseMembers,
  grantHouseAccess,
  revokeHouseAccess,
} from '@/data/house-access';
import {
  BottomTabInset,
  MaxContentWidth,
  Spacing,
  TopTabInset,
} from '@/constants/theme';
import { useHouses } from '@/hooks/use-houses';
import { isMockApiEnabled } from '@/services/api';
import { canManageHouses } from '@/services/houses';

export default function HouseDetailsScreen() {
  const { id } = useLocalSearchParams<{ id: string }>();
  const { houses, loading, error, reload } = useHouses();
  const house = houses.find((item) => item.id === id);
  const demo = isMockApiEnabled();
  const { user } = useAuth();
  const theme = useTheme();
  const members = demo && house ? getHouseMembers(house.id) : [];
  const canManage = canManageHouses(user?.role);
  const [email, setEmail] = useState('');
  const [message, setMessage] = useState('');

  function returnToHouses() {
    if (router.canGoBack()) {
      router.back();
    } else {
      router.replace('/');
    }
  }

  function addMember() {
    //kept this demo-only because this form does not save access changes to Supabase yet.
    if (!demo || !canManage) return;
    const normalizedEmail = email.trim().toLowerCase();
    if (!/^\S+@\S+\.\S+$/.test(normalizedEmail)) {
      setMessage('Enter a valid email address.');
      return;
    }
    const name = normalizedEmail.split('@')[0].replace(/[._-]+/g, ' ');
    const added = house && grantHouseAccess(house.id, {
      id: normalizedEmail === 'tenant@example.com' ? 'dev-tenant' : normalizedEmail,
      name,
      email: normalizedEmail,
      role: 'Tenant',
    });
    if (!added) {
      setMessage('This user already has access.');
      return;
    }

    setEmail('');
    setMessage(`${normalizedEmail} added to this house.`);
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

          {loading ? <ActivityIndicator accessibilityLabel="Loading house" /> : error ? (
            <>
              <ThemedText accessibilityRole="alert">{error}</ThemedText>
              <Button title="Retry houses" onPress={reload} />
            </>
          ) : house ? (
            <>
              <View style={styles.heading}>
                <ThemedText type="smallBold" themeColor="textSecondary">
                  {canManage ? 'PROPERTY MANAGER' : 'HOUSE DETAILS'}
                </ThemedText>
                <ThemedText type="title">{house.name}</ThemedText>
                <ThemedText themeColor="textSecondary">{house.address}</ThemedText>
              </View>

              <View style={styles.section}>
                <ThemedText type="subtitle">Property status</ThemedText>
                <View style={styles.statusGrid}>
                  <ThemedView type="backgroundElement" style={styles.statusItem}>
                    <ThemedText type="small" themeColor="textSecondary">DOOR LOCK</ThemedText>
                    <ThemedText type="smallBold">{demo ? 'Locked' : 'Status unavailable'}</ThemedText>
                    <ThemedText type="small" themeColor="textSecondary">{demo ? 'Device offline (demo)' : 'Live status not connected'}</ThemedText>
                  </ThemedView>
                  <ThemedView type="backgroundElement" style={styles.statusItem}>
                    <ThemedText type="small" themeColor="textSecondary">TEMPERATURE</ThemedText>
                    <ThemedText style={styles.metric}>21.5 C</ThemedText>
                    <ThemedText type="small" themeColor="textSecondary">Sample reading</ThemedText>
                  </ThemedView>
                  {demo && canManage && (
                    <ThemedView type="backgroundElement" style={styles.statusItem}>
                      <ThemedText type="small" themeColor="textSecondary">ACCESS</ThemedText>
                      <ThemedText style={styles.metric}>{members.length}</ThemedText>
                      <ThemedText type="small" themeColor="textSecondary">Users assigned</ThemedText>
                    </ThemedView>
                  )}
                </View>
              </View>

              {demo && canManage && <View style={styles.section}>
                <View style={styles.sectionHeading}>
                  <ThemedText type="subtitle">House access</ThemedText>
                  <ThemedText type="small" themeColor="textSecondary">{members.length} users</ThemedText>
                </View>
                {members.map((member) => (
                  <ThemedView key={member.id} type="backgroundElement" style={styles.memberRow}>
                    <View style={styles.memberInfo}>
                      <ThemedText type="smallBold" style={styles.memberName}>{member.name}</ThemedText>
                      <ThemedText type="small" themeColor="textSecondary">{member.email}</ThemedText>
                    </View>
                    <ThemedText type="small" themeColor="textSecondary">{member.role}</ThemedText>
                    <Pressable
                      accessibilityRole="button"
                      accessibilityLabel={`Remove ${member.email} from ${house.name}`}
                      onPress={() => {
                        revokeHouseAccess(house.id, member.id);
                        setMessage(`${member.email} removed from this house.`);
                      }}
                      style={({ pressed }) => [styles.removeButton, pressed && styles.pressed]}>
                      <ThemedText type="smallBold" style={{ color: theme.danger }}>Remove</ThemedText>
                    </Pressable>
                  </ThemedView>
                ))}
                {members.length === 0 && (
                  <ThemedText type="small" themeColor="textSecondary">No users currently have access.</ThemedText>
                )}

                <ThemedView type="backgroundElement" style={styles.addForm}>
                    <ThemedText type="smallBold">Grant house access</ThemedText>
                    <View style={styles.addRow}>
                      <TextInput
                        accessibilityLabel="User email address"
                        autoCapitalize="none"
                        autoComplete="email"
                        keyboardType="email-address"
                        onChangeText={setEmail}
                        onSubmitEditing={addMember}
                        placeholder="name@example.com"
                        placeholderTextColor={theme.textSecondary}
                        returnKeyType="done"
                        style={[styles.input, { color: theme.text, borderColor: theme.backgroundSelected }]}
                        value={email}
                      />
                      <Pressable
                        accessibilityRole="button"
                        onPress={addMember}
                        style={({ pressed }) => [styles.addButton, { backgroundColor: theme.accent }, pressed && styles.pressed]}>
                        <ThemedText type="smallBold" style={styles.addButtonText}>Add user</ThemedText>
                      </Pressable>
                    </View>
                    {message !== '' && <ThemedText type="small" accessibilityRole="alert">{message}</ThemedText>}
                </ThemedView>
              </View>}
              {!demo && canManage && (
                <ThemedText type="small" themeColor="textSecondary">
                  House access changes are not available in the app yet.
                </ThemedText>
              )}
            </>
          ) : (
            <>
              <ThemedText type="title">House not found</ThemedText>
              <ThemedText>
                This house could not be found or you no longer have access. Return to the house list
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
    gap: Spacing.four,
  },
  heading: {
    gap: Spacing.one,
  },
  demoNotice: {
    borderRadius: Spacing.two,
    borderLeftWidth: 4,
    borderLeftColor: '#D97706',
    padding: Spacing.three,
    gap: Spacing.one,
  },
  section: {
    gap: Spacing.three,
  },
  statusGrid: {
    flexDirection: 'row',
    flexWrap: 'wrap',
    gap: Spacing.two,
  },
  statusItem: {
    flexGrow: 1,
    flexBasis: 150,
    minHeight: 116,
    borderRadius: Spacing.two,
    padding: Spacing.three,
    justifyContent: 'space-between',
  },
  metric: {
    fontSize: 28,
    lineHeight: 34,
    fontWeight: '700',
  },
  sectionHeading: {
    flexDirection: 'row',
    justifyContent: 'space-between',
    alignItems: 'center',
    gap: Spacing.two,
  },
  memberRow: {
    flexDirection: 'row',
    alignItems: 'center',
    borderRadius: Spacing.two,
    padding: Spacing.three,
    gap: Spacing.two,
  },
  memberInfo: {
    flex: 1,
    gap: Spacing.one,
  },
  memberName: {
    textTransform: 'capitalize',
  },
  removeButton: {
    paddingVertical: Spacing.two,
    paddingHorizontal: Spacing.two,
  },
  addForm: {
    borderRadius: Spacing.two,
    padding: Spacing.three,
    gap: Spacing.two,
  },
  addRow: {
    flexDirection: 'row',
    gap: Spacing.two,
  },
  input: {
    flex: 1,
    minWidth: 0,
    minHeight: 44,
    borderWidth: 1,
    borderRadius: Spacing.two,
    paddingHorizontal: Spacing.two,
    fontSize: 16,
  },
  addButton: {
    minHeight: 44,
    borderRadius: Spacing.two,
    paddingHorizontal: Spacing.three,
    alignItems: 'center',
    justifyContent: 'center',
  },
  addButtonText: {
    color: '#FFFFFF',
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
