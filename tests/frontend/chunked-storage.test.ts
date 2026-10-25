import { describe, expect, test } from 'vitest';
import { createChunkedStorage } from '@/services/chunked-storage';

function fixture() {
  const entries = new Map<string, string>();
  let failed = false;
  const storage = createChunkedStorage({
    getItem: async (key) => entries.get(key) ?? null,
    setItem: async (key, value) => {
      if (failed && !key.endsWith('.manifest')) throw new Error('Keychain failure');
      if (new TextEncoder().encode(value).length > 1800) throw new Error('Too large');
      entries.set(key, value);
    },
    removeItem: async (key) => { entries.delete(key); },
  });
  return { entries, storage, fail: () => { failed = true; } };
}

describe('secure session chunks', () => {
  test('round trips large Unicode sessions below each item byte limit', async () => {
    const f = fixture();
    const value = JSON.stringify({ access_token: 'x'.repeat(6000), refresh_token: 'refresh', name: '🙂é'.repeat(800) });
    await f.storage.setItem('session', value);
    expect(await f.storage.getItem('session')).toBe(value);
    expect(f.entries.size).toBeGreaterThan(4);
    await f.storage.removeItem('session');
    expect(f.entries.size).toBe(0);
    expect(await f.storage.getItem('session')).toBeNull();
  });
  test('keeps the old session if replacement fails', async () => {
    const f = fixture();
    await f.storage.setItem('session', 'old');
    f.fail();
    await expect(f.storage.setItem('session', 'new')).rejects.toThrow();
    expect(await f.storage.getItem('session')).toBe('old');
  });
  test('cleans replaced chunks and serializes concurrent writes', async () => {
    const f = fixture();
    await f.storage.setItem('session', 'x'.repeat(5000));
    await Promise.all([f.storage.setItem('session', 'first'), f.storage.setItem('session', 'second')]);
    expect(await f.storage.getItem('session')).toBe('second');
    expect(f.entries.size).toBe(2);
  });
  test('rejects incomplete or malformed manifests', async () => {
    const f = fixture();
    f.entries.set('session.manifest', '{');
    expect(await f.storage.getItem('session')).toBeNull();
    await f.storage.setItem('session', 'value');
    for (const key of f.entries.keys()) if (!key.endsWith('.manifest')) f.entries.delete(key);
    expect(await f.storage.getItem('session')).toBeNull();
  });
});
