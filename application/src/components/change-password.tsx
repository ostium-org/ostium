// Change-password form for the settings screen. Validates locally (length + match) for instant
// feedback, then calls the auth hook's `changePassword`. On success it records the action and
// reports back to the parent so the settings screen can show a confirmation.
import { useState } from "react";
import { Pressable, StyleSheet, View } from "react-native";

import { Button } from "@/components/button";
import { TextField } from "@/components/text-field";
import { ThemedText } from "@/components/themed-text";
import { Spacing } from "@/constants/theme";
import { useAuth } from "@/hooks/auth";
import { useI18n } from "@/i18n/i18n";
import { ApiError } from "@/services/api";

type ChangePasswordProps = {
  onDone: () => void;
};

type FieldErrors = {
  current?: string;
  newPassword?: string;
  confirm?: string;
};

export function ChangePassword({ onDone }: ChangePasswordProps) {
  const { t, recordAction } = useI18n();
  const { changePassword } = useAuth();

  const [current, setCurrent] = useState("");
  const [newPassword, setNewPassword] = useState("");
  const [confirm, setConfirm] = useState("");
  const [showNew, setShowNew] = useState(false);
  const [fieldErrors, setFieldErrors] = useState<FieldErrors>({});
  const [formError, setFormError] = useState<string | null>(null);
  const [submitting, setSubmitting] = useState(false);

  function validate(): FieldErrors {
    const errors: FieldErrors = {};
    if (current === "") errors.current = t("password.error.required");
    if (newPassword === "") errors.newPassword = t("password.error.required");
    else if (newPassword.length < 8)
      errors.newPassword = t("password.error.min");
    if (confirm !== newPassword) errors.confirm = t("password.error.mismatch");
    return errors;
  }

  async function handleSubmit() {
    const errors = validate();
    setFieldErrors(errors);
    setFormError(null);
    if (errors.current || errors.newPassword || errors.confirm) return;

    setSubmitting(true);
    try {
      await changePassword(newPassword);
      recordAction("action.passwordChanged");
      onDone();
    } catch (error) {
      setFormError(
        error instanceof ApiError ? error.message : t("password.title"),
      );
    } finally {
      setSubmitting(false);
    }
  }

  return (
    <View style={styles.form}>
      <TextField
        label={t("password.current")}
        value={current}
        onChangeText={(text) => {
          setCurrent(text);
          setFieldErrors((e) => ({ ...e, current: undefined }));
        }}
        error={fieldErrors.current}
        secureTextEntry
        autoCapitalize="none"
        autoCorrect={false}
        autoComplete="current-password"
        textContentType="password"
        editable={!submitting}
      />
      <TextField
        label={t("password.new")}
        value={newPassword}
        onChangeText={(text) => {
          setNewPassword(text);
          setFieldErrors((e) => ({ ...e, newPassword: undefined }));
        }}
        error={fieldErrors.newPassword}
        secureTextEntry={!showNew}
        autoCapitalize="none"
        autoCorrect={false}
        autoComplete="new-password"
        textContentType="newPassword"
        editable={!submitting}
        right={
          <Pressable
            accessibilityRole="button"
            accessibilityLabel={showNew ? t("login.hide") : t("login.show")}
            onPress={() => setShowNew((v) => !v)}
            hitSlop={Spacing.two}
          >
            <ThemedText type="smallBold" themeColor="textSecondary">
              {showNew ? t("login.hide") : t("login.show")}
            </ThemedText>
          </Pressable>
        }
      />
      <TextField
        label={t("password.confirm")}
        value={confirm}
        onChangeText={(text) => {
          setConfirm(text);
          setFieldErrors((e) => ({ ...e, confirm: undefined }));
        }}
        error={fieldErrors.confirm}
        secureTextEntry
        autoCapitalize="none"
        autoCorrect={false}
        autoComplete="new-password"
        textContentType="newPassword"
        editable={!submitting}
      />

      {formError ? (
        <ThemedText type="small" themeColor="danger" role="alert">
          {formError}
        </ThemedText>
      ) : null}

      <Button
        title={t("password.submit")}
        onPress={handleSubmit}
        loading={submitting}
      />
    </View>
  );
}

const styles = StyleSheet.create({
  form: {
    gap: Spacing.three,
  },
});
