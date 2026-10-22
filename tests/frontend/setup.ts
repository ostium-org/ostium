import { afterAll, afterEach, beforeEach, mock } from 'bun:test';
import { client, resetSupabase } from './supabase-mock';

const originalDev = Object.getOwnPropertyDescriptor(globalThis, '__DEV__');
const originalFetch = globalThis.fetch;
Object.defineProperty(globalThis, '__DEV__', { value: false, configurable: true, writable: true });
mock.module('../../application/src/services/supabase', () => ({
  supabaseUrl: 'https://project.ostium.test', supabaseKey: 'publishable-test', getSupabase: async () => client,
}));
beforeEach(() => {
  resetSupabase();
  globalThis.fetch = mock(() => { throw new Error('Unexpected fetch: mock the response in this test.'); }) as unknown as typeof fetch;
});
afterEach(() => { mock.restore(); globalThis.fetch = originalFetch; });
afterAll(() => {
  if (originalDev) Object.defineProperty(globalThis, '__DEV__', originalDev);
  else Reflect.deleteProperty(globalThis, '__DEV__');
});
