import { describe, expect, vi, test } from 'vitest';

import { parseStoredSession } from '@/services/session';

const now = 1_800_000_000_000;
const session = {
  accessToken: 'saved-token',
  expiresAt: now + 60_000,
  user: { id: 'tenant-1', email: 'tenant@example.com', role: 'tenant' as const },
};

describe('parseStoredSession', () => {
  test('restores a valid saved session', () => {
    expect(parseStoredSession(JSON.stringify(session), now)).toEqual(session);
  });

  test('uses the current clock when no time is supplied', () => {
    vi.spyOn(Date, 'now').mockReturnValue(now);
    expect(parseStoredSession(JSON.stringify(session))).toEqual(session);
  });

  test.each([null, '', '{', 'null', '[]', 'true', '42', '"text"'])(
    'rejects missing or malformed storage %j',
    (raw) => expect(parseStoredSession(raw, now)).toBeNull(),
  );

  test.each([
    { accessToken: '' },
    { accessToken: null },
    { accessToken: 123 },
    { accessToken: undefined },
    { expiresAt: undefined },
    { expiresAt: 'tomorrow' },
    { expiresAt: null },
    { user: undefined },
    { user: { ...session.user, role: 'admin' } },
  ])('rejects invalid session fields %j', (fields) => {
    expect(parseStoredSession(JSON.stringify({ ...session, ...fields }), now)).toBeNull();
  });

  test('rejects a non-finite expiration encoded as JSON', () => {
    const raw = JSON.stringify(session).replace(String(session.expiresAt), '1e400');
    expect(parseStoredSession(raw, now)).toBeNull();
  });

  test.each([-1, 0, 29_999, 30_000])('rejects expiration at now + %i ms', (offset) => {
    expect(parseStoredSession(JSON.stringify({ ...session, expiresAt: now + offset }), now)).toBeNull();
  });

  test('accepts expiration one millisecond beyond the safety margin', () => {
    const value = { ...session, expiresAt: now + 30_001 };
    expect(parseStoredSession(JSON.stringify(value), now)).toEqual(value);
  });

  test('normalizes stored user data through the shared user parser', () => {
    const user = { id: 42, email: 'manager@example.com', role: 'manager' as const, name: 'Manager' };
    expect(parseStoredSession(JSON.stringify({ ...session, user }), now)?.user).toEqual({
      ...user, id: '42',
    });
  });
});
