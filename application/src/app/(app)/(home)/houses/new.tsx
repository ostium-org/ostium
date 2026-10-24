import { router } from 'expo-router';
import { useState } from 'react';
import { ScrollView, StyleSheet, View } from 'react-native';
import { SafeAreaView } from 'react-native-safe-area-context';

import { Button } from '@/components/button';
import { TextField } from '@/components/text-field';
import { ThemedText } from '@/components/themed-text';
import { ThemedView } from '@/components/themed-view';
import {
  BottomTabInset,
  MaxContentWidth,
  Spacing,
  TopTabInset,
} from '@/constants/theme';
import { useAuth } from '@/hooks/auth';

const fields = [
  { key: 'name', label: 'House name' },
  { key: 'address', label: 'Address' },
  { key: 'building', label: 'Building' },
  { key: 'floor', label: 'Floor' },
  { key: 'notes', label: 'Notes' },
] as const;

type FieldName = (typeof fields)[number]['key'];

export default function AddHouseScreen() {
  const { user } = useAuth();
  const [values, setValues] = useState<Record<FieldName, string>>({
    name: '',
    address: '',
    building: '',
    floor: '',
    notes: '',
  });

  function goBack() {
    if (router.canGoBack()) {
      router.back();
    } else {
      router.replace('/');
    }
  }

  return (
    <ThemedView style={styles.page}>
      <SafeAreaView style={styles.page} edges={['top']}>
        <ScrollView
          keyboardShouldPersistTaps="handled"
          contentContainerStyle={styles.content}>
          <Button title="Back" variant="secondary" onPress={goBack} />

          {user?.role === 'manager' ? (
            <>
              <ThemedText type="title">ADD HOUSE</ThemedText>

              <View style={styles.fields}>
                {fields.map(({ key, label }) => (
                  <TextField
                    key={key}
                    label={label}
                    value={values[key]}
                    onChangeText={(text) =>
                      setValues((current) => ({
                        ...current,
                        [key]: text,
                      }))
                    }
                  />
                ))}
              </View>

              <ThemedText type="small" themeColor="textSecondary">
                Saving is not available yet. This form is awaiting backend
                integration.
              </ThemedText>

              <Button
                title="Save house"
                disabled
                onPress={() => {}}
              />
            </>
          ) : (
            <ThemedText>
              Only managers can add houses.
            </ThemedText>
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
  content: {
    width: '100%',
    maxWidth: MaxContentWidth,
    alignSelf: 'center',
    paddingHorizontal: Spacing.four,
    paddingTop: TopTabInset + Spacing.three,
    paddingBottom: BottomTabInset + Spacing.four,
    gap: Spacing.three,
  },
  fields: {
    gap: Spacing.three,
  },
});