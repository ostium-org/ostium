import 'react-native-url-polyfill/auto';
import { createClient, processLock, type SupabaseClient } from '@supabase/supabase-js';
import { authStorage } from '@/services/auth-storage';

export const supabaseUrl = (process.env.EXPO_PUBLIC_SUPABASE_URL ?? '').trim().replace(/\/+$/, '');
export const supabaseKey = (process.env.EXPO_PUBLIC_SUPABASE_PUBLISHABLE_KEY ?? '').trim();
let client: Promise<SupabaseClient> | undefined;

export function getSupabase(): Promise<SupabaseClient> {
  if (!supabaseUrl || !supabaseKey) return Promise.reject(new Error('Supabase public configuration is missing.'));
  // Keep native dependencies in the initial Metro bundle; only defer client creation.
  client ??= Promise.resolve().then(() => {
    return createClient(supabaseUrl, supabaseKey, {
      auth: {
        storage: authStorage, persistSession: true, autoRefreshToken: true,
        detectSessionInUrl: false, storageKey: 'ostium.supabase.auth', lock: processLock,
      },
    });
  }).catch((error) => { client = undefined; throw error; });
  return client;
}
