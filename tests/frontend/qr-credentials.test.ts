import { afterEach, describe, expect, mock, test } from 'bun:test';

import { setAccessToken, setUnauthorizedHandler } from '@/services/api';
import { getQRCredential, parseQRCredential } from '@/services/qr-credentials';

const now = Date.parse('2026-10-20T12:00:00Z');
const response = { house_id: 'house-1', payload: ' exact.signed.payload\n', expires_at: '2026-10-20T12:02:00Z' };
afterEach(() => { setAccessToken(null); setUnauthorizedHandler(null); });

describe('QR credential responses', () => {
  test('preserves the opaque payload and converts the returned expiry to milliseconds', () => {
    expect(parseQRCredential(response, 'house-1', now)).toEqual({
      houseId: 'house-1', payload: response.payload, expiresAt: now + 120000, isDemo: false,
    });
  });
  test('accepts an ISO timestamp with a timezone offset', () => {
    expect(parseQRCredential({ ...response, expires_at: '2026-10-20T13:02:00+01:00' }, 'house-1', now).expiresAt)
      .toBe(now + 120000);
  });
  test.each([
    null, [], {}, { ...response, house_id: 'house-2' }, { ...response, payload: '' },
    { ...response, payload: '   ' }, { ...response, payload: {} },
    { ...response, expires_at: 'invalid' }, { ...response, expires_at: 123 },
    { ...response, expires_at: '2026-10-20T12:02:00' },
    { ...response, expires_at: '2026-10-20T12:00:00Z' },
    { ...response, expires_at: '2026-10-20T11:59:59Z' },
  ].map((data) => ({ data })))('rejects malformed, wrong-house, or expired responses %j', ({ data }) => {
    expect(() => parseQRCredential(data, 'house-1', now)).toThrow();
  });

  test('posts to the selected house endpoint with the existing bearer token', async () => {
    const data = { ...response, house_id: 'house /2', expires_at: new Date(Date.now() + 90000).toISOString() };
    const fetchMock = mock(async () => new Response(JSON.stringify(data), { status: 200 }));
    globalThis.fetch = fetchMock as unknown as typeof fetch;
    setAccessToken('access-token');
    const result = await getQRCredential('house /2');
    expect(result.payload).toBe(data.payload);
    expect(fetchMock).toHaveBeenCalledWith(
      'https://api.ostium.test/api/v1/houses/house%20%2F2/qr-credentials',
      expect.objectContaining({ method: 'POST', headers: { Accept: 'application/json', Authorization: 'Bearer access-token' } }),
    );
  });

  test.each([404, 500])('does not fall back to demo data after HTTP %i', async (status) => {
    globalThis.fetch = mock(async () => new Response('{}', { status })) as unknown as typeof fetch;
    await expect(getQRCredential('house-1')).rejects.toMatchObject({ status });
  });

  test('uses existing sign-out handling when the credential request is unauthorized', async () => {
    const signOut = mock(() => {});
    setAccessToken('access-token');
    setUnauthorizedHandler(signOut);
    globalThis.fetch = mock(async () => new Response('{}', { status: 401 })) as unknown as typeof fetch;
    await expect(getQRCredential('house-1')).rejects.toMatchObject({ kind: 'unauthorized' });
    expect(signOut).toHaveBeenCalledTimes(1);
  });
});

// Isolate module-level API configuration and the native UUID stub from other test files.
test.each([
  [true, '', '', false],
  [true, '', 'true', true],
  [true, 'https://api.ostium.test', 'true', false],
  [false, '', 'true', false],
] as const)('QR mock mode follows development=%s, API URL=%s, and opt-in=%s', (dev, url, optIn, expectedMock) => {
  const script = `
    import { mock } from 'bun:test';
    globalThis.__DEV__ = ${dev};
    process.env.EXPO_PUBLIC_API_URL = ${JSON.stringify(url)};
    process.env.EXPO_PUBLIC_MOCK_API = ${JSON.stringify(optIn)};
    mock.module('expo-crypto', () => ({ randomUUID: () => 'demo-unique-id' }));
    globalThis.fetch = async () => { throw new Error('API request attempted'); };
    const { isMockApiEnabled, signup } = await import('./src/services/api');
    const { getQRCredential } = await import('./src/services/qr-credentials');
    if (isMockApiEnabled() !== ${expectedMock}) throw new Error('Incorrect mock mode');
    if (${expectedMock}) {
      const before = Date.now();
      const result = await getQRCredential('house-1');
      const payload = JSON.parse(result.payload);
      if (!result.isDemo || payload.houseId !== 'house-1' || payload.id !== 'demo-unique-id' ||
          result.expiresAt !== payload.issuedAt + 60000 || payload.issuedAt < before) throw new Error('Invalid demo');
    } else {
      try { await getQRCredential('house-1'); throw new Error('Unexpected success'); }
      catch (error) { if (!['network', 'not-configured'].includes(error.kind)) throw error; }
      try { await signup('tenant@example.com', 'password123'); throw new Error('Fake signup succeeded'); }
      catch (error) { if (!['network', 'not-configured'].includes(error.kind)) throw error; }
    }
  `;
  const result = Bun.spawnSync([process.execPath, '-e', script], { cwd: `${import.meta.dir}/../../application` });
  expect(result.stderr.toString()).toBe('');
  expect(result.exitCode).toBe(0);
});
