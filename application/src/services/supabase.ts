import type { SupabaseClient } from '@supabase/supabase-js';

export const supabaseUrl = (process.env.EXPO_PUBLIC_SUPABASE_URL ?? '').trim().replace(/\/+$/, '');
export const supabaseKey = (process.env.EXPO_PUBLIC_SUPABASE_PUBLISHABLE_KEY ?? '').trim();
let client: Promise<SupabaseClient> | undefined;

export function getSupabase(): Promise<SupabaseClient> {
  if (!supabaseUrl || !supabaseKey) return Promise.reject(new Error('Supabase public configuration is missing.'));
  client ??= Promise.all([
    import('@supabase/supabase-js'),
    import('@/services/auth-storage'),
    import('react-native-url-polyfill'),
  ]).then(([{ createClient, processLock }, { authStorage }, { setupURLPolyfill }]) => {
    setupURLPolyfill();
    return createClient(supabaseUrl, supabaseKey, {
      auth: {
        storage: authStorage, persistSession: true, autoRefreshToken: true,
        detectSessionInUrl: false, storageKey: 'ostium.supabase.auth', lock: processLock,
      },
    });
  }).catch((error) => { client = undefined; throw error; });
  return client;
}
