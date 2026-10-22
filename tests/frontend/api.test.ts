import { describe, expect, mock, test } from 'bun:test';
import { getDevLoginHint, loadProfile, login, parseAuthUser, request, signup } from '@/services/api';
import type { Session } from '@supabase/supabase-js';
import { client, session, state, user } from './supabase-mock';

function respond(value: unknown, status = 200) {
  const fn = mock(async () => new Response(status === 204 ? null : JSON.stringify(value), { status }));
  globalThis.fetch = fn as unknown as typeof fetch;
  return fn;
}

describe('direct Supabase authentication', () => {
  test('logs in with the SDK and derives role from the active database profile', async () => {
    state.profile!.role = 'admin';
    const result = await login(user.email, 'password123');
    expect(client.auth.signInWithPassword).toHaveBeenCalledWith({ email: user.email, password: 'password123' });
    expect(result.user.role).toBe('admin');
    expect(result.accessToken).toBe('session-token');
    expect(client.from).toHaveBeenCalledWith('profiles');
  });
  test('sends only full_name in signup metadata', async () => {
    await signup(user.email, 'password123', '  Tenant  ');
    expect(client.auth.signUp).toHaveBeenCalledWith({ email: user.email, password: 'password123', options: { data: { full_name: 'Tenant' } } });
  });
  test('returns confirmation-required signup without looking for a profile', async () => {
    state.session = null;
    client.from.mockClear();
    expect(await signup(user.email, 'password123')).toEqual({ confirmationRequired: true, email: user.email });
    expect(client.from).not.toHaveBeenCalled();
  });
  test.each([null, { id: user.id, role: 'tenant', is_active: false }])('rejects a missing or inactive profile', async (profile) => {
    state.profile = profile;
    await expect(login(user.email, 'password123')).rejects.toMatchObject({ status: 403 });
    expect(client.auth.signOut).toHaveBeenCalledWith({ scope: 'local' });
  });
  test('does not turn profile failures into successful authentication', async () => {
    state.profileError = { message: 'database offline' };
    await expect(loadProfile(session as Session)).rejects.toMatchObject({ kind: 'server' });
  });
  test('maps bad credentials and rate limiting', async () => {
    client.auth.signInWithPassword.mockImplementation(async () => ({ data: { session: null }, error: { status: 400 } }));
    await expect(login(user.email, 'wrong')).rejects.toMatchObject({ kind: 'invalid-credentials' });
    client.auth.signInWithPassword.mockImplementation(async () => ({ data: { session: null }, error: { status: 429 } }));
    await expect(login(user.email, 'wrong')).rejects.toMatchObject({ kind: 'rate-limited' });
  });
  test('does not expose demo hints in a configured application', () => expect(getDevLoginHint()).toBeNull());
});

describe('user profile parsing', () => {
  test.each(['tenant', 'manager', 'admin'])('supports %s', (role) => {
    expect(parseAuthUser({ ...user, role })).toEqual({ ...user, role });
  });
  test.each([null, [], {}, { ...user, role: 'owner' }, { ...user, role: 'tenant', email: '' }].map(value => ({ value })))('rejects malformed profile %j', ({ value }) => {
    expect(parseAuthUser(value)).toBeNull();
  });
});

describe('Edge Function transport', () => {
  test('uses the hosted route, public key and current SDK session', async () => {
    state.session = { ...session, access_token: 'refreshed-token' };
    const fetch = respond({ ok: true });
    expect(await request('/api/v1/doors/front/commands', { method: 'POST', body: { action: 'unlock' } })).toEqual({ ok: true });
    expect(fetch).toHaveBeenCalledWith('https://project.ostium.test/functions/v1/ostium-api/api/v1/doors/front/commands', expect.objectContaining({
      method: 'POST', body: '{"action":"unlock"}', headers: { Accept: 'application/json', apikey: 'publishable-test',
        'Content-Type': 'application/json', Authorization: 'Bearer refreshed-token' },
    }));
  });
  test('refuses authenticated calls without a session', async () => {
    state.session = null;
    await expect(request('/api/v1/houses/unit/qr-credentials')).rejects.toMatchObject({ kind: 'unauthorized' });
  });
  test('supports empty success responses', async () => {
    respond(null, 204);
    expect(await request('/api/v1/test')).toBeUndefined();
  });
  test('rejects invalid successful JSON', async () => {
    globalThis.fetch = mock(async () => new Response('{')) as unknown as typeof fetch;
    await expect(request('/api/v1/test')).rejects.toMatchObject({ kind: 'bad-response' });
  });
  test.each([[401, 'unauthorized'], [403, 'request-failed'], [429, 'rate-limited'], [502, 'server']])('maps HTTP %s', async (status, kind) => {
    respond({}, Number(status));
    await expect(request('/api/v1/test')).rejects.toMatchObject({ kind, status });
  });
  test('clears the local session after an authenticated 401', async () => {
    respond({}, 401);
    await expect(request('/api/v1/test')).rejects.toMatchObject({ kind: 'unauthorized' });
    expect(client.auth.signOut).toHaveBeenCalledWith({ scope: 'local' });
  });
  test('does not clear a session for an explicitly unauthenticated request', async () => {
    respond({}, 401);
    await expect(request('/api/v1/test', { authenticated: false })).rejects.toMatchObject({ kind: 'unauthorized' });
    expect(client.auth.signOut).not.toHaveBeenCalled();
  });
  test('maps network failures', async () => {
    await expect(request('/api/v1/test')).rejects.toMatchObject({ kind: 'network' });
  });
  test('maps request aborts to timeout', async () => {
    globalThis.fetch = mock(async () => { throw new DOMException('aborted', 'AbortError'); }) as unknown as typeof fetch;
    await expect(request('/api/v1/test')).rejects.toMatchObject({ kind: 'timeout' });
  });
});
