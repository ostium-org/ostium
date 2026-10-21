// SIGNUP SCREEN (route "/signup")
// shown to anyone who isn't signed in, linked from the login screen.
// Creates a new account. The backend always creates the user as a 'tenant';
// only an admin can promote them later.

import { useRef, useState } from "react";
import {
  KeyboardAvoidingView,
  Platform,
  Pressable,
  ScrollView,
  StyleSheet,
  TextInput,
  View,
} from "react-native";
import { SafeAreaView } from "react-native-safe-area-context";
import { Link } from "expo-router";

import { Button } from "@/components/button";
import { TextField } from "@/components/text-field";
import { ThemedText } from "@/components/themed-text";
import { ThemedView } from "@/components/themed-view";
import { PageTitleSize, Spacing } from "@/constants/theme";
import { useAuth } from "@/hooks/auth";
import { ApiError, type ApiErrorKind } from "@/services/api";
import { validateSignupForm, type SignupFieldErrors } from "@/utils/validation";

const ERROR_MESSAGES: Record<ApiErrorKind, string> = {
  "invalid-credentials": "We couldn’t create that account. Please try again.",
  unauthorized: "That email may already be registered.",
  "rate-limited": "Too many attempts. Please wait a moment and try again.",
  server: "Something went wrong on our end. Please try again later.",
  "request-failed": "We couldn’t complete your request. Please try again.",
  "bad-response":
    "We got an unexpected response from the server. Please try again.",
  network: "Can’t reach the server. Check your connection and try again.",
  timeout: "The server took too long to respond. Please try again.",
  "not-configured": "The app isn’t connected to a server yet.",
};

function messageFor(error: unknown): string {
  if (error instanceof ApiError) return ERROR_MESSAGES[error.kind];
  if (error instanceof Error) return error.message;
  return "Something went wrong. Please try again.";
}

export default function SignupScreen() {
  const { signUp } = useAuth();

  const passwordRef = useRef<TextInput>(null);
  const confirmRef = useRef<TextInput>(null);

  const [fullName, setFullName] = useState("");
  const [email, setEmail] = useState("");
  const [password, setPassword] = useState("");
  const [confirmPassword, setConfirmPassword] = useState("");
  const [showPassword, setShowPassword] = useState(false);
  const [fieldErrors, setFieldErrors] = useState<SignupFieldErrors>({});
  const [formError, setFormError] = useState<string | null>(null);
  const [submitting, setSubmitting] = useState(false);

  const submittingRef = useRef(false);

  function clearError(key: keyof SignupFieldErrors) {
    setFieldErrors((current) => ({ ...current, [key]: undefined }));
  }

  async function handleSubmit() {
    if (submittingRef.current) return;

    const errors = validateSignupForm(
      fullName,
      email,
      password,
      confirmPassword,
    );
    setFieldErrors(errors);
    setFormError(null);
    if (errors.email || errors.password || errors.confirmPassword) return;

    submittingRef.current = true;
    setSubmitting(true);
    try {
      // On success the AuthProvider updates `user` and the layout swaps to the app.
      await signUp(email.trim(), password, fullName.trim() || undefined);
    } catch (error) {
      setFormError(messageFor(error));
      setPassword("");
      setConfirmPassword("");
      setShowPassword(false);
    } finally {
      submittingRef.current = false;
      setSubmitting(false);
    }
  }

  return (
    <ThemedView style={styles.page}>
      <SafeAreaView style={styles.flex}>
        <KeyboardAvoidingView
          style={styles.flex}
          behavior={Platform.OS === "ios" ? "padding" : undefined}
        >
          <ScrollView
            contentContainerStyle={styles.scroll}
            keyboardShouldPersistTaps="handled"
            showsVerticalScrollIndicator={false}
          >
            <View style={styles.content}>
              <View style={styles.brand}>
                <ThemedText type="title" style={styles.title}>
                  CREATE ACCOUNT
                </ThemedText>
                <ThemedText
                  type="small"
                  themeColor="textSecondary"
                  style={styles.subtitle}
                >
                  Welcome to Ostium
                </ThemedText>
              </View>

              <View style={styles.form}>
                <TextField
                  label="Full name (optional)"
                  value={fullName}
                  onChangeText={(text) => {
                    setFullName(text);
                    clearError("fullName");
                  }}
                  error={fieldErrors.fullName}
                  placeholder="Your name"
                  autoCapitalize="words"
                  autoCorrect={false}
                  autoComplete="name"
                  textContentType="name"
                  returnKeyType="next"
                  onSubmitEditing={() => passwordRef.current?.focus()}
                  editable={!submitting}
                />

                <TextField
                  label="Email"
                  value={email}
                  onChangeText={(text) => {
                    setEmail(text);
                    clearError("email");
                  }}
                  error={fieldErrors.email}
                  placeholder="you@example.com"
                  keyboardType="email-address"
                  autoCapitalize="none"
                  autoCorrect={false}
                  autoComplete="username"
                  textContentType="username"
                  returnKeyType="next"
                  onSubmitEditing={() => passwordRef.current?.focus()}
                  editable={!submitting}
                />

                <TextField
                  ref={passwordRef}
                  label="Password"
                  value={password}
                  onChangeText={(text) => {
                    setPassword(text);
                    clearError("password");
                  }}
                  error={fieldErrors.password}
                  placeholder="At least 8 characters"
                  secureTextEntry={!showPassword}
                  autoCapitalize="none"
                  autoCorrect={false}
                  autoComplete="new-password"
                  textContentType="newPassword"
                  returnKeyType="next"
                  onSubmitEditing={() => confirmRef.current?.focus()}
                  editable={!submitting}
                  right={
                    <Pressable
                      accessibilityRole="button"
                      accessibilityLabel={
                        showPassword ? "Hide password" : "Show password"
                      }
                      onPress={() => setShowPassword((current) => !current)}
                      hitSlop={Spacing.two}
                    >
                      <ThemedText type="smallBold" themeColor="textSecondary">
                        {showPassword ? "HIDE" : "SHOW"}
                      </ThemedText>
                    </Pressable>
                  }
                />

                <TextField
                  ref={confirmRef}
                  label="Confirm password"
                  value={confirmPassword}
                  onChangeText={(text) => {
                    setConfirmPassword(text);
                    clearError("confirmPassword");
                  }}
                  error={fieldErrors.confirmPassword}
                  placeholder="Re-enter your password"
                  secureTextEntry={!showPassword}
                  autoCapitalize="none"
                  autoCorrect={false}
                  autoComplete="new-password"
                  textContentType="newPassword"
                  returnKeyType="go"
                  onSubmitEditing={handleSubmit}
                  editable={!submitting}
                />

                {formError ? (
                  <ThemedText
                    type="small"
                    themeColor="danger"
                    role="alert"
                    style={styles.formError}
                  >
                    {formError}
                  </ThemedText>
                ) : null}

                <Button
                  title="CREATE ACCOUNT"
                  onPress={handleSubmit}
                  loading={submitting}
                />

                <View style={styles.switchRow}>
                  <ThemedText type="small" themeColor="textSecondary">
                    Already have an account?
                  </ThemedText>
                  <Link href="/login" asChild>
                    <Pressable accessibilityRole="link" hitSlop={Spacing.two}>
                      <ThemedText type="smallBold" themeColor="accent">
                        SIGN IN
                      </ThemedText>
                    </Pressable>
                  </Link>
                </View>
              </View>
            </View>
          </ScrollView>
        </KeyboardAvoidingView>
      </SafeAreaView>
    </ThemedView>
  );
}

const styles = StyleSheet.create({
  page: {
    flex: 1,
    width: "100%",
  },
  flex: {
    flex: 1,
  },
  scroll: {
    flexGrow: 1,
    justifyContent: "center",
    padding: Spacing.four,
  },
  content: {
    width: "100%",
    maxWidth: 420,
    alignSelf: "center",
    gap: Spacing.five,
  },
  brand: {
    alignItems: "center",
    gap: Spacing.one,
  },
  title: {
    ...PageTitleSize,
    textAlign: "center",
  },
  subtitle: {
    letterSpacing: 1,
  },
  form: {
    gap: Spacing.three,
  },
  formError: {
    textAlign: "center",
  },
  switchRow: {
    flexDirection: "row",
    justifyContent: "center",
    alignItems: "center",
    gap: Spacing.one,
  },
});
