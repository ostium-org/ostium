// LOGIN SCREEN (route "/login")
// shown to anyone who isn't signed in
// actual credential check to happen on thhe backend.

import { Image } from "expo-image";
import { Link } from "expo-router";
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

import { Button } from "@/components/button";
import { TextField } from "@/components/text-field";
import { ThemedText } from "@/components/themed-text";
import { ThemedView } from "@/components/themed-view";
import { PageTitleSize, Spacing } from "@/constants/theme";
import { useAuth } from "@/hooks/auth";
import { ApiError, getDevLoginHint, type ApiErrorKind } from "@/services/api";
import { validateLoginForm, type LoginFieldErrors } from "@/utils/validation";

const ERROR_MESSAGES: Record<ApiErrorKind, string> = {
  "invalid-credentials": "Incorrect email or password.",
  unauthorized: "Your session has expired. Please sign in again.",
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

export default function LoginScreen() {
  const { signIn } = useAuth();

  //pressing "next" on the keyboard jump from the email field to the password field.
  const passwordRef = useRef<TextInput>(null);

  const [email, setEmail] = useState("");
  const [password, setPassword] = useState("");
  const [showPassword, setShowPassword] = useState(false);
  const [fieldErrors, setFieldErrors] = useState<LoginFieldErrors>({});
  const [formError, setFormError] = useState<string | null>(null); // problems from the server
  const [submitting, setSubmitting] = useState(false);

  const submittingRef = useRef(false);

  async function handleSubmit() {
    if (submittingRef.current) return; // ignore repeat taps

    // local checks, so obvious mistakes don't cost a network round trip.
    const errors = validateLoginForm(email, password);
    setFieldErrors(errors);
    setFormError(null);
    if (errors.email || errors.password) return;

    // ask the backend. on success AuthProvider updates `user` and the layout swaps this screen

    submittingRef.current = true;
    setSubmitting(true);
    try {
      await signIn(email.trim(), password);
    } catch (error) {
      setFormError(messageFor(error));
      // don't leave a rejected password sitting in the box
      setPassword("");
      setShowPassword(false);
    } finally {
      submittingRef.current = false;
      setSubmitting(false);
    }
  }

  const devHint = getDevLoginHint(); // null except in development with no API URL set

  return (
    <ThemedView style={styles.page}>
      <SafeAreaView style={styles.flex}>
        {/* moves the form up when the iOS keyboard opens. */}
        <KeyboardAvoidingView
          style={styles.flex}
          behavior={Platform.OS === "ios" ? "padding" : undefined}
        >
          {}
          <ScrollView
            contentContainerStyle={styles.scroll}
            keyboardShouldPersistTaps="handled"
            showsVerticalScrollIndicator={false}
          >
            <View style={styles.content}>
              <View style={styles.brand}>
                <ThemedText type="title" style={styles.title}>
                  SIGN IN
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
                  label="Email"
                  value={email}
                  onChangeText={(text) => {
                    setEmail(text);
                    setFieldErrors((current: LoginFieldErrors) => ({
                      ...current,
                      email: undefined,
                    })); // clear as they fix it
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
                    setFieldErrors((current: LoginFieldErrors) => ({
                      ...current,
                      password: undefined,
                    }));
                  }}
                  error={fieldErrors.password}
                  placeholder="Your password"
                  secureTextEntry={!showPassword} // dots instead of letters
                  autoCapitalize="none"
                  autoCorrect={false}
                  autoComplete="current-password"
                  textContentType="password"
                  returnKeyType="go"
                  onSubmitEditing={handleSubmit} // pressing Enter or Go submits
                  editable={!submitting}
                  right={
                    <Pressable
                      accessibilityRole="button"
                      accessibilityLabel={
                        showPassword ? "Hide password" : "Show password"
                      }
                      onPress={() =>
                        setShowPassword((current: boolean) => !current)
                      }
                      hitSlop={Spacing.two}
                    >
                      <ThemedText type="smallBold" themeColor="textSecondary">
                        {showPassword ? "HIDE" : "SHOW"}
                      </ThemedText>
                    </Pressable>
                  }
                />

                {/* Server-side problems */}
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
                  title="SIGN IN"
                  onPress={handleSubmit}
                  loading={submitting}
                />

                <View style={styles.switchRow}>
                  <ThemedText type="small" themeColor="textSecondary">
                    New to Ostium?
                  </ThemedText>
                  <Link href="/signup" asChild>
                    <Pressable accessibilityRole="link" hitSlop={Spacing.two}>
                      <ThemedText type="smallBold" themeColor="accent">
                        CREATE ACCOUNT
                      </ThemedText>
                    </Pressable>
                  </Link>
                </View>

                {devHint ? (
                  <ThemedText
                    type="small"
                    themeColor="textSecondary"
                    style={styles.devHint}
                  >
                    {devHint}
                  </ThemedText>
                ) : null}
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
    justifyContent: "center", // vertically centered when there's room
    padding: Spacing.four,
  },
  content: {
    width: "100%",
    maxWidth: 420, // keep the form a width on tablets and browsers
    alignSelf: "center",
    gap: Spacing.five,
  },
  brand: {
    alignItems: "center",
    gap: Spacing.one,
  },
  logo: {
    width: 120,
    height: 120,
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
  devHint: {
    textAlign: "center",
  },
});
