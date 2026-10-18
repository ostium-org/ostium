// TOKEN STORAGE - IOS
// keeeps the signed-in session on the device so the user stays logged in after restarting the app.
// uses the operating system's secure storage (iOS Keychain / Android Keystore) via expo-secure-store,
// (token-storage.web.ts is the browser version

import * as SecureStore from 'expo-secure-store';

import { parseStoredSession, type StoredSession } from '@/services/session';

const KEY = 'ostium.session';

// only readable while the phone is unlocked
const OPTIONS: SecureStore.SecureStoreOptions = {
  keychainAccessible: SecureStore.WHEN_UNLOCKED_THIS_DEVICE_ONLY,
};

export async function saveSession(session: StoredSession): Promise<void> {
  await SecureStore.setItemAsync(KEY, JSON.stringify(session), OPTIONS);
}

// returns the saved session, or null if there isn't a valid one.
export async function loadSession(): Promise<StoredSession | null> {
  try {
    const raw = await SecureStore.getItemAsync(KEY, OPTIONS);
    const session = parseStoredSession(raw);
    // expired or corrupted data 
    if (raw && !session) await clearSession();
    return session;
  } catch {
    return null;
  }
}

export async function clearSession(): Promise<void> {
  try {
    await SecureStore.deleteItemAsync(KEY, OPTIONS);
  } catch {
    
  }
}