import { afterAll, afterEach, beforeEach, mock } from 'bun:test';

declare global {
  var __DEV__: boolean;
}

const originalDev = Object.getOwnPropertyDescriptor(globalThis, '__DEV__');
const originalUrl = process.env.EXPO_PUBLIC_API_URL;
const originalFetch = globalThis.fetch;

// Configure services before their first import, regardless of the developer's environment.
globalThis.__DEV__ = false;
process.env.EXPO_PUBLIC_API_URL = 'https://api.ostium.test';

beforeEach(() => {
  // An unmocked request must fail locally instead of reaching any network.
  globalThis.fetch = mock(() => {
    throw new Error('Unexpected fetch: mock the response in this test.');
  }) as unknown as typeof fetch;
});

afterEach(() => {
  mock.restore();
  globalThis.fetch = originalFetch;
});

afterAll(() => {
  if (originalDev) Object.defineProperty(globalThis, '__DEV__', originalDev);
  else Reflect.deleteProperty(globalThis, '__DEV__');
  if (originalUrl === undefined) delete process.env.EXPO_PUBLIC_API_URL;
  else process.env.EXPO_PUBLIC_API_URL = originalUrl;
});
