import { afterEach, describe, expect, mock, spyOn, test } from 'bun:test';

import {
  getDevLoginHint,
  login,
  parseAuthUser,
  request,
  setAccessToken,
  setUnauthorizedHandler,
} from '@/services/api';

const user = { id: 'tenant-1', email: 'tenant@example.com', role: 'tenant' } as const;

function respond(body: unknown, status = 200) {
  const fetchMock = mock(async (_url: string | URL | Request, _options?: RequestInit) =>
    new Response(JSON.stringify(body), { status }),
  );
  globalThis.fetch = fetchMock as unknown as typeof fetch;
  return fetchMock;
}

afterEach(() => {
  setAccessToken(null);
  setUnauthorizedHandler(null);
});

describe('parseAuthUser', () => {
  test.each(['tenant', 'manager'])('accepts the %s role', (role) => {
    expect(parseAuthUser({ ...user, role })).toEqual({ ...user, role });
  });

  test('normalizes numeric IDs and preserves a name', () => {
    expect(parseAuthUser({ ...user, id: 42, name: 'Tenant' })).toEqual({
      ...user, id: '42', name: 'Tenant',
    });
  });

  test('omits a null optional name and unknown fields', () => {
    expect(parseAuthUser({ ...user, name: null, extra: true })).toEqual(user);
  });

  test.each([
    null, 'user', 42, {},
    { ...user, id: '' },
    { ...user, id: null },
    { ...user, email: '' },
    { ...user, email: 42 },
    { ...user, role: 'admin' },
    { ...user, name: 42 },
  ])('rejects malformed user %j', (value) => {
    expect(parseAuthUser(value)).toBeNull();
  });

  test('rejects an array', () => {
    expect(parseAuthUser([])).toBeNull();
  });
});

describe('request', () => {
  test('defaults to a JSON-accepting GET with no body or token', async () => {
    const fetchMock = respond({ ok: true });
    await expect(request('/health')).resolves.toEqual({ ok: true });
    expect(fetchMock).toHaveBeenCalledWith('https://api.ostium.test/health', {
      method: 'GET',
      headers: { Accept: 'application/json' },
      body: undefined,
      signal: expect.any(AbortSignal),
    });
  });

  test.each(['POST', 'PUT', 'PATCH', 'DELETE'] as const)('sends a JSON body with %s', async (method) => {
    const fetchMock = respond({ ok: true });
    setAccessToken('access-token');
    await request('/resource', { method, body: { enabled: false } });
    expect(fetchMock).toHaveBeenCalledWith('https://api.ostium.test/resource', expect.objectContaining({
      method,
      body: '{"enabled":false}',
      headers: {
        Accept: 'application/json',
        'Content-Type': 'application/json',
        Authorization: 'Bearer access-token',
      },
    }));
  });

  test('does not send a token for an unauthenticated request', async () => {
    const fetchMock = respond({});
    setAccessToken('access-token');
    await request('/public', { authenticated: false });
    expect(fetchMock.mock.calls[0][1]?.headers).toEqual({ Accept: 'application/json' });
  });

  test('stops sending a cleared token', async () => {
    const fetchMock = respond({});
    setAccessToken('access-token');
    setAccessToken(null);
    await request('/resource');
    expect(fetchMock.mock.calls[0][1]?.headers).toEqual({ Accept: 'application/json' });
  });

  test('accepts an empty 204 response', async () => {
    globalThis.fetch = mock(async () => new Response(null, { status: 204 })) as unknown as typeof fetch;
    await expect(request('/resource')).resolves.toBeUndefined();
  });

  test('rejects invalid JSON in a successful response', async () => {
    globalThis.fetch = mock(async () => new Response('{')) as unknown as typeof fetch;
    await expect(request('/resource')).rejects.toMatchObject({
      name: 'ApiError', kind: 'bad-response', status: 200,
    });
  });

  test.each([
    [401, 'unauthorized'], [429, 'rate-limited'], [500, 'server'], [503, 'server'],
    [400, 'request-failed'], [403, 'request-failed'], [404, 'request-failed'],
  ] as const)('maps HTTP %i to %s', async (status, kind) => {
    respond({ detail: 'Rejected' }, status);
    await expect(request('/resource')).rejects.toMatchObject({ name: 'ApiError', kind, status });
  });

  test('maps a fetch failure to a network error', async () => {
    globalThis.fetch = mock(async () => { throw new TypeError('Offline'); }) as unknown as typeof fetch;
    await expect(request('/resource')).rejects.toMatchObject({ kind: 'network' });
  });

  test('maps a response-body read failure to a network error', async () => {
    const response = new Response('{}');
    spyOn(response, 'text').mockRejectedValue(new TypeError('Connection closed'));
    globalThis.fetch = mock(async () => response) as unknown as typeof fetch;
    await expect(request('/resource')).rejects.toMatchObject({ kind: 'network' });
  });

  test('aborts after the configured deadline and clears its timer', async () => {
    let expire: (() => void) | undefined;
    const timer = 123 as unknown as ReturnType<typeof setTimeout>;
    const schedule = spyOn(globalThis, 'setTimeout').mockImplementation(((handler: TimerHandler) => {
      if (typeof handler !== 'function') throw new Error('Expected a timer callback');
      expire = () => handler();
      return timer;
    }) as unknown as typeof setTimeout);
    const cancel = spyOn(globalThis, 'clearTimeout').mockImplementation(() => {});
    let signal: AbortSignal | null | undefined;
    globalThis.fetch = mock((_url: unknown, options?: RequestInit) => {
      signal = options?.signal;
      return new Promise<Response>((_resolve, reject) => {
        signal?.addEventListener('abort', () => reject(new DOMException('Aborted', 'AbortError')), { once: true });
      });
    }) as unknown as typeof fetch;

    const pending = request('/resource');
    expect(schedule).toHaveBeenCalledWith(expect.any(Function), 15_000);
    expect(signal?.aborted).toBe(false);
    expect(expire).toBeDefined();
    expire!();
    await expect(pending).rejects.toMatchObject({ kind: 'timeout' });
    expect(signal?.aborted).toBe(true);
    expect(cancel).toHaveBeenCalledWith(timer);
  });

  test.each([200, 401, 500])('clears the timeout after HTTP %i', async (status) => {
    const schedule = spyOn(globalThis, 'setTimeout');
    const cancel = spyOn(globalThis, 'clearTimeout');
    respond({}, status);
    await request('/resource').catch(() => {});
    expect(cancel).toHaveBeenCalledWith(schedule.mock.results[0].value);
  });

  test.each([
    [null, true, false], ['token', false, false], ['token', true, true],
  ] as const)('handles a 401 with token=%j and authenticated=%j', async (token, authenticated, notified) => {
    const handler = mock(() => {});
    setUnauthorizedHandler(handler);
    setAccessToken(token);
    respond({}, 401);
    await expect(request('/private', { authenticated })).rejects.toMatchObject({ kind: 'unauthorized' });
    expect(handler).toHaveBeenCalledTimes(notified ? 1 : 0);
  });

  test('allows the unauthorized handler to be removed', async () => {
    const handler = mock(() => {});
    setUnauthorizedHandler(handler);
    setUnauthorizedHandler(null);
    setAccessToken('token');
    respond({}, 401);
    await expect(request('/private')).rejects.toMatchObject({ kind: 'unauthorized' });
    expect(handler).not.toHaveBeenCalled();
  });
});

describe('login', () => {
  const response = { access_token: 'new-token', expires_in: 3600, user };

  test('posts credentials without an existing token and normalizes the response', async () => {
    const fetchMock = respond(response);
    setAccessToken('old-token');
    await expect(login(user.email, ' password ')).resolves.toEqual({
      accessToken: 'new-token', expiresIn: 3600, user,
    });
    expect(fetchMock).toHaveBeenCalledWith('https://api.ostium.test/api/v1/auth/login', expect.objectContaining({
      method: 'POST',
      body: JSON.stringify({ email: user.email, password: ' password ' }),
      headers: { Accept: 'application/json', 'Content-Type': 'application/json' },
    }));
  });

  test.each([
    null, {},
    { ...response, access_token: '' },
    { ...response, access_token: 123 },
    { ...response, expires_in: 0 },
    { ...response, expires_in: -1 },
    { ...response, expires_in: '3600' },
    { ...response, expires_in: null },
    { ...response, user: null },
    { ...response, user: { ...user, role: 'admin' } },
  ])('rejects an invalid login response %j', async (body) => {
    respond(body);
    await expect(login(user.email, 'password')).rejects.toMatchObject({ kind: 'bad-response' });
  });

  test('rejects an array login response', async () => {
    respond([]);
    await expect(login(user.email, 'password')).rejects.toMatchObject({ kind: 'bad-response' });
  });

  test('rejects a non-finite token lifetime', async () => {
    const body = JSON.stringify(response).replace('3600', '1e400');
    globalThis.fetch = mock(async () => new Response(body)) as unknown as typeof fetch;
    await expect(login(user.email, 'password')).rejects.toMatchObject({ kind: 'bad-response' });
  });

  test('maps a login 401 to invalid credentials without signing out an existing session', async () => {
    const handler = mock(() => {});
    setAccessToken('old-token');
    setUnauthorizedHandler(handler);
    respond({}, 401);
    await expect(login(user.email, 'wrong')).rejects.toMatchObject({ kind: 'invalid-credentials', status: 401 });
    expect(handler).not.toHaveBeenCalled();
  });

  test('preserves other API errors', async () => {
    respond({}, 429);
    await expect(login(user.email, 'password')).rejects.toMatchObject({ kind: 'rate-limited', status: 429 });
  });

  test('does not expose development login hints with a configured production API', () => {
    expect(getDevLoginHint()).toBeNull();
  });
});
