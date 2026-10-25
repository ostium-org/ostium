
import { useState, type ReactNode, type Ref } from 'react';
import {
  StyleSheet,
  TextInput,
  View,
  type TextInputProps,
  type TextInputInstance,
} from 'react-native';

import { ThemedText } from '@/components/themed-text';
import { ThemedView } from '@/components/themed-view';
import { Fonts, Spacing } from '@/constants/theme';
import { useTheme } from '@/hooks/use-theme';

type TextFieldProps = Omit<TextInputProps, 'style'> & {
  label: string;
  error?: string | null; 
  right?: ReactNode; 
  ref?: Ref<TextInputInstance>;
};

export function TextField({ label, error, right, ref, onFocus, onBlur, ...inputProps }: TextFieldProps) {
  const theme = useTheme();
  const [focused, setFocused] = useState(false);

 
  const borderColor = error ? theme.danger : focused ? theme.accent : theme.backgroundSelected;

  return (
    <View style={styles.wrapper}>
      <ThemedText type="smallBold" themeColor="textSecondary">
        {label}
      </ThemedText>

      <ThemedView type="backgroundElement" style={[styles.field, { borderColor }]}>
        <TextInput
          ref={ref}
          accessibilityLabel={label} 
          placeholderTextColor={theme.textSecondary}
          selectionColor={theme.accent}
          style={[styles.input, { color: theme.text }]}
          onFocus={(event) => {
            setFocused(true);
            onFocus?.(event);
          }}
          onBlur={(event) => {
            setFocused(false);
            onBlur?.(event);
          }}
          {...inputProps}
        />
        {right}
      </ThemedView>

      {}
      {error ? (
        <ThemedText type="small" themeColor="danger" role="alert">
          {error}
        </ThemedText>
      ) : null}
    </View>
  );
}

const styles = StyleSheet.create({
  wrapper: {
    gap: Spacing.one,
  },
  field: {
    flexDirection: 'row',
    alignItems: 'center',
    borderWidth: 1.5,
    borderRadius: Spacing.three,
    paddingHorizontal: Spacing.three,
  },
  input: {
    flex: 1,
    minHeight: 48,
    fontFamily: Fonts.body,
    fontSize: 16,
  },
});