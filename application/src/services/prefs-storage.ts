// Non-secret, per-device preferences (language, recent activity).
//
// This is intentionally separate from `auth-storage`: preferences are not credentials, so they
// don't need the chunked/secure treatment the Supabase session gets. We reuse `expo-secure-store`
// (already a dependency) as the key-value backend so we don't add a third-party storage library —
// the value stored here is small and non-sensitive.
import * as SecureStore from "expo-secure-store";

import type { StringStorage } from "@/services/chunked-storage";

export const prefsStorage: StringStorage = {
  getItem: (key) => SecureStore.getItemAsync(key),
  setItem: (key, value) => SecureStore.setItemAsync(key, value),
  removeItem: (key) => SecureStore.deleteItemAsync(key),
};
