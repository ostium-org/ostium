import { afterAll, afterEach, beforeEach, vi } from 'vitest';
import { resetSupabase } from './supabase-mock';

const originalDev = Object.getOwnPropertyDescriptor(globalThis, '__DEV__');
const originalFetch = globalThis.fetch;
Object.defineProperty(globalThis, '__DEV__', { value: false, configurable: true, writable: true });
vi.mock('@/services/supabase', async () => ({
  supabaseUrl: 'https://project.ostium.test', supabaseKey: 'publishable-test', getSupabase: async () => (await import('./supabase-mock')).client,
}));
beforeEach(() => {
  resetSupabase();
  globalThis.fetch = vi.fn(() => { throw new Error('Unexpected fetch: mock the response in this test.'); }) as unknown as typeof fetch;
});
afterEach(() => { vi.restoreAllMocks(); globalThis.fetch = originalFetch; });
afterAll(() => {
  if (originalDev) Object.defineProperty(globalThis, '__DEV__', originalDev);
  else Reflect.deleteProperty(globalThis, '__DEV__');
});
