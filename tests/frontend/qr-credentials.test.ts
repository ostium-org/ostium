import { describe, expect, vi, test } from 'vitest';

import { client } from './supabase-mock';
import { getQRCredential, parseQRCredential } from '@/services/qr-credentials';

const now = Date.parse('2026-10-20T12:00:00Z');
const response = { house_id: 'house-1', payload: ' exact.signed.payload\n', expires_at: '2026-10-20T12:02:00Z' };

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
    const fetchMock = vi.fn(async () => new Response(JSON.stringify(data), { status: 200 }));
    globalThis.fetch = fetchMock as unknown as typeof fetch;
    const result = await getQRCredential('house /2');
    expect(result.payload).toBe(data.payload);
    expect(fetchMock).toHaveBeenCalledWith(
      'https://project.ostium.test/functions/v1/ostium-api/api/v1/houses/house%20%2F2/qr-credentials',
      expect.objectContaining({ method: 'POST', headers: { Accept: 'application/json', apikey: 'publishable-test', Authorization: 'Bearer session-token' } }),
    );
  });

  test.each([404, 500])('does not fall back to demo data after HTTP %i', async (status) => {
    globalThis.fetch = vi.fn(async () => new Response('{}', { status })) as unknown as typeof fetch;
    await expect(getQRCredential('house-1')).rejects.toMatchObject({ status });
  });

  test('uses existing sign-out handling when the credential request is unauthorized', async () => {
    globalThis.fetch = vi.fn(async () => new Response('{}', { status: 401 })) as unknown as typeof fetch;
    await expect(getQRCredential('house-1')).rejects.toMatchObject({ kind: 'unauthorized' });
    expect(client.auth.signOut).toHaveBeenCalledTimes(1);
  });
});

// Reload configuration per case while keeping native modules and the SDK offline.
test.each([
  [true, '', '', false],
  [true, '', 'true', true],
  [true, 'https://project.ostium.test', 'true', false],
  [false, '', 'true', false],
] as const)('QR mock mode follows development=%s, Supabase URL=%s, and opt-in=%s', async (dev, url, optIn, expectedMock) => {
  vi.resetModules();
  vi.stubGlobal('__DEV__', dev);
  vi.stubEnv('EXPO_PUBLIC_MOCK_API', optIn);
  vi.doMock('expo-crypto', () => ({ randomUUID: () => 'demo-unique-id' }));
  vi.doMock('@/services/supabase', () => ({
    supabaseUrl: url, supabaseKey: url ? 'publishable-test' : '',
    getSupabase: async () => { throw { kind: 'network' }; },
  }));
  try {
    const { isMockApiEnabled, signup } = await import('@/services/api');
    const { getQRCredential: configuredCredential } = await import('@/services/qr-credentials');
    expect(isMockApiEnabled()).toBe(expectedMock);
    if (expectedMock) {
      const before = Date.now();
      const result = await configuredCredential('house-1');
      const payload = JSON.parse(result.payload);
      expect(result.isDemo).toBe(true);
      expect(payload.houseId).toBe('house-1');
      expect(payload.id).toBe('demo-unique-id');
      expect(result.expiresAt).toBe(payload.issuedAt + 60000);
      expect(payload.issuedAt).toBeGreaterThanOrEqual(before);
    } else {
      const kind = url ? 'network' : 'not-configured';
      await expect(configuredCredential('house-1')).rejects.toMatchObject({ kind });
      await expect(signup('tenant@example.com', 'password123')).rejects.toMatchObject({ kind });
    }
  } finally {
    vi.doUnmock('expo-crypto');
    vi.doUnmock('@/services/supabase');
    vi.unstubAllEnvs();
    vi.unstubAllGlobals();
    vi.resetModules();
  }
});
