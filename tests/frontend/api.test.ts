import { describe, expect, vi, test } from 'vitest';
import { getDevLoginHint, loadProfile, login, parseAuthUser, request, signup } from '@/services/api';
import type { Session } from '@supabase/supabase-js';
import { client, session, state, user } from './supabase-mock';

function respond(value: unknown, status = 200) {
  const fn = vi.fn(async () => new Response(status === 204 ? null : JSON.stringify(value), { status }));
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
  test.each(['email_exists', 'user_already_exists'])('explains duplicate registration (%s)', async (code) => {
    client.auth.signUp.mockResolvedValue({ data: { session: null, user }, error: { code, status: 422 } });
    await expect(signup(user.email, 'password123')).rejects.toMatchObject({
      kind: 'signup-rejected', status: 422,
      message: 'An account with this email already exists. Please sign in instead.',
    });
  });
  test.each([
    ['email_address_invalid', 'Supabase rejected this email address. Please check it and try again.'],
    ['weak_password', 'This password does not meet the account security requirements. Please choose a stronger password.'],
    ['signup_disabled', 'Account registration is disabled. Please contact your project administrator.'],
    ['email_address_not_authorized', 'This project cannot send a confirmation email to this address. Please contact your project administrator.'],
  ])('explains the signup rejection (%s) without raw server details', async (code, message) => {
    client.auth.signUp.mockResolvedValue({ data: { session: null, user }, error: { code, status: 422, message: 'Private server details' } });
    await expect(signup(user.email, 'password123')).rejects.toMatchObject({ kind: 'signup-rejected', message });
  });
  test('preserves rate limiting and safely handles unknown signup errors', async () => {
    client.auth.signUp.mockResolvedValue({ data: { session: null, user }, error: { code: 'over_email_send_rate_limit', status: 429 } });
    await expect(signup(user.email, 'password123')).rejects.toMatchObject({ kind: 'rate-limited' });
    client.auth.signUp.mockResolvedValue({ data: { session: null, user }, error: { code: 'future_error', status: 422, message: 'Private server details' } });
    await expect(signup(user.email, 'password123')).rejects.toMatchObject({ kind: 'request-failed', message: 'Authentication failed.' });
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
    globalThis.fetch = vi.fn(async () => new Response('{')) as unknown as typeof fetch;
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
    globalThis.fetch = vi.fn(async () => { throw new DOMException('aborted', 'AbortError'); }) as unknown as typeof fetch;
    await expect(request('/api/v1/test')).rejects.toMatchObject({ kind: 'timeout' });
  });
});

describe('Edge Function transport deadlines', () => {
  test('maps a response-body read failure to a network error', async () => {
    const response = new Response('{}');
    vi.spyOn(response, 'text').mockRejectedValue(new TypeError('Connection closed'));
    globalThis.fetch = vi.fn(async () => response) as unknown as typeof fetch;
    await expect(request('/resource', { authenticated: false })).rejects.toMatchObject({ kind: 'network' });
  });

  test('aborts after the configured deadline and clears its timer', async () => {
    let expire: (() => void) | undefined;
    const timer = 123 as unknown as ReturnType<typeof setTimeout>;
    const schedule = vi.spyOn(globalThis, 'setTimeout').mockImplementation(((handler: TimerHandler) => {
      if (typeof handler !== 'function') throw new Error('Expected a timer callback');
      expire = () => handler();
      return timer;
    }) as unknown as typeof setTimeout);
    const cancel = vi.spyOn(globalThis, 'clearTimeout').mockImplementation(() => {});
    let signal: AbortSignal | null | undefined;
    globalThis.fetch = vi.fn((_url: unknown, options?: RequestInit) => {
      signal = options?.signal;
      return new Promise<Response>((_resolve, reject) => {
        signal?.addEventListener('abort', () => reject(new DOMException('Aborted', 'AbortError')), { once: true });
      });
    }) as unknown as typeof fetch;

    const pending = request('/resource', { authenticated: false });
    expect(schedule).toHaveBeenCalledWith(expect.any(Function), 15_000);
    expect(signal?.aborted).toBe(false);
    expect(expire).toBeDefined();
    expire!();
    await expect(pending).rejects.toMatchObject({ kind: 'timeout' });
    expect(signal?.aborted).toBe(true);
    expect(cancel).toHaveBeenCalledWith(timer);
  });

  test.each([200, 401, 500])('clears the timeout after HTTP %i', async (status) => {
    const schedule = vi.spyOn(globalThis, 'setTimeout');
    const cancel = vi.spyOn(globalThis, 'clearTimeout');
    respond({}, status);
    await request('/resource', { authenticated: false }).catch(() => {});
    expect(cancel).toHaveBeenCalledWith(schedule.mock.results[0].value);
  });

});
