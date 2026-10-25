import { describe, expect, test, vi, afterEach } from 'vitest';
import * as api from '@/services/api';
import { canManageHouses, createHouse, getHouses } from '@/services/houses';

const user = { id: 'real-user', email: 'manager@example.com', role: 'manager' as const };
const values = { name: ' Maple ', address: ' Main St ', building: '', floor: '', notes: '' };
afterEach(() => api.setAccessToken(null));

function respond(body: unknown, status = 200) {
  const fetch = vi.fn(async () => new Response(JSON.stringify(body), { status }));
  globalThis.fetch = fetch;
  return fetch;
}

describe('real houses through FastAPI', () => {
  test('loads database houses with the existing login token', async () => {
    const houses = [{ id: 'real-unit', name: 'Maple', address: '' }];
    const fetch = respond(houses);
    api.setAccessToken('login-token');
    expect(await getHouses(user)).toEqual(houses);
    expect(fetch).toHaveBeenCalledWith('https://api.ostium.test/api/v1/houses', expect.objectContaining({
      headers: { Accept: 'application/json', Authorization: 'Bearer login-token' },
    }));
  });
  test('keeps empty results empty and surfaces database failures without demo fallback', async () => {
    respond([]);
    expect(await getHouses(user)).toEqual([]);
    respond({}, 502);
    await expect(getHouses(user)).rejects.toMatchObject({ kind: 'server' });
    respond([{ id: 'bad' }]);
    await expect(getHouses(user)).rejects.toMatchObject({ kind: 'bad-response' });
  });
  test.each(['manager', 'admin'] as const)('%s saves through the authenticated API', async role => {
    const fetch = respond({}, 201);
    expect(canManageHouses(role)).toBe(true);
    await createHouse({ ...user, role }, values);
    expect(fetch).toHaveBeenCalledWith('https://api.ostium.test/api/v1/houses', expect.objectContaining({
      method: 'POST', body: JSON.stringify({ ...values, name: 'Maple', address: 'Main St' }),
    }));
  });
  test('blocks tenant creation and blank required fields', async () => {
    expect(canManageHouses('tenant')).toBe(false);
    await expect(createHouse({ ...user, role: 'tenant' }, values)).rejects.toMatchObject({ status: 403 });
    await expect(createHouse(user, { ...values, name: ' ' })).rejects.toThrow('Enter a house name and address.');
    expect(fetch).not.toHaveBeenCalled();
  });
  test('demo accounts cannot create or see fake database houses', async () => {
    vi.spyOn(api, 'isMockApiEnabled').mockReturnValue(true);
    expect(await getHouses(user)).toEqual([]);
    await expect(createHouse(user, values)).rejects.toMatchObject({ kind: 'not-configured' });
    expect(fetch).not.toHaveBeenCalled();
  });
});
