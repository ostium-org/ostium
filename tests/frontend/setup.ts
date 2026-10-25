import { afterAll, afterEach, beforeEach, vi } from 'vitest';

const originalDev = Object.getOwnPropertyDescriptor(globalThis, '__DEV__');
const originalUrl = process.env.EXPO_PUBLIC_API_URL;
const originalFetch = globalThis.fetch;

// Configure services before their first import, regardless of the developer's environment.
Object.defineProperty(globalThis, '__DEV__', { value: false, configurable: true, writable: true });
process.env.EXPO_PUBLIC_API_URL = 'https://api.ostium.test';

beforeEach(() => {
  // An unmocked request must fail locally instead of reaching any network.
  globalThis.fetch = vi.fn(() => {
    throw new Error('Unexpected fetch: mock the response in this test.');
  }) as unknown as typeof fetch;
});

afterEach(() => {
  vi.restoreAllMocks();
  globalThis.fetch = originalFetch;
});

afterAll(() => {
  if (originalDev) Object.defineProperty(globalThis, '__DEV__', originalDev);
  else Reflect.deleteProperty(globalThis, '__DEV__');
  if (originalUrl === undefined) delete process.env.EXPO_PUBLIC_API_URL;
  else process.env.EXPO_PUBLIC_API_URL = originalUrl;
});
