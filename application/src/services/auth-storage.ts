import * as SecureStore from 'expo-secure-store';
import { createChunkedStorage } from '@/services/chunked-storage';

const options = { keychainAccessible: SecureStore.WHEN_UNLOCKED_THIS_DEVICE_ONLY };
export const authStorage = createChunkedStorage({
  getItem: key => SecureStore.getItemAsync(key, options),
  setItem: (key, value) => SecureStore.setItemAsync(key, value, options),
  removeItem: key => SecureStore.deleteItemAsync(key, options),
});
