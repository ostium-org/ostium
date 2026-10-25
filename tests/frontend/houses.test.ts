import { beforeEach, describe, expect, test, vi } from 'vitest';
import { createClient } from '@supabase/supabase-js';

import * as api from '@/services/api';
import { canManageHouses, currentUnitIds, createHouse, getHouses } from '@/services/houses';
import { client, user } from './supabase-mock';

const now = Date.parse('2026-10-25T12:00:00Z');
const tenant = { ...user, role: 'tenant' as const };
const unitId = '81cb8a12-f50f-497d-9f63-2d9b875ca2fb';
const grant = { unit_id: unitId, is_active: true, valid_from: '2026-10-24T12:00:00Z', valid_until: null };
let grants: object[];
let units: object[];
let failure: string | undefined;
let requests: URL[];

beforeEach(() => {
  grants = [{ ...grant }];
  units = [{ id: unitId, name: 'Maple House', address: null }];
  failure = undefined;
  requests = [];
  // Exercise real SDK query construction, without calling a live database.
  const database = createClient('https://houses.ostium.test', 'publishable-test', {
    auth: { persistSession: false, autoRefreshToken: false, detectSessionInUrl: false },
    global: { fetch: async input => {
      const url = new URL(input instanceof Request ? input.url : String(input));
      requests.push(url);
      const table = url.pathname.split('/').at(-1);
      if (table !== 'units' && table !== 'access_grants') throw new Error('Unexpected table');
      return new Response(JSON.stringify(failure === table ? { message: 'Permission denied' } : table === 'units' ? units : grants), {
        status: failure === table ? 403 : 200, headers: { 'Content-Type': 'application/json' },
      });
    } },
  });
  client.from.mockImplementation((...args: unknown[]) => database.from(String(args[0])) as unknown as ReturnType<typeof client.from>);
});

describe('houses from Supabase', () => {
  test.each(['admin', 'manager'] as const)('%s can list active units without an individual grant', async role => {
    grants = [];
    expect(canManageHouses(role)).toBe(true);
    expect(await getHouses({ ...user, role }, now)).toEqual([{ id: unitId, name: 'Maple House', address: '' }]);
    expect(requests).toHaveLength(1);
    expect(requests[0].pathname).toBe('/rest/v1/units');
    expect(requests[0].searchParams.get('is_active')).toBe('eq.true');
    expect(requests[0].searchParams.get('order')).toBe('name.asc,id.asc');
  });

  test('loads tenant grants and queries real unit IDs instead of demo memberships', async () => {
    expect(canManageHouses('tenant')).toBe(false);
    expect(await getHouses(tenant, now)).toEqual([{ id: unitId, name: 'Maple House', address: '' }]);
    expect(requests[0].searchParams.get('grantee_id')).toBe(`eq.${user.id}`);
    expect(requests[0].searchParams.get('is_active')).toBe('eq.true');
    expect(requests[1].searchParams.get('id')).toBe(`in.(${unitId})`);
    expect(requests[1].searchParams.get('is_active')).toBe('eq.true');
  });

  test('excludes inactive, expired, future, and invalid grants, including exact expiry', async () => {
    grants = [
      { ...grant, is_active: false },
      { ...grant, valid_until: '2026-10-25T12:00:00Z' },
      { ...grant, valid_from: '2026-10-26T12:00:00Z' },
      { ...grant, valid_from: 'invalid' },
    ];
    expect(await getHouses(tenant, now)).toEqual([]);
    expect(requests).toHaveLength(1);
  });

  test('includes the start boundary and deduplicates multiple grants for a unit', () => {
    expect(currentUnitIds([
      { ...grant, valid_from: '2026-10-25T12:00:00Z' },
      { ...grant, valid_until: '2026-10-26T12:00:00Z' },
    ], now)).toEqual([unitId]);
  });

  test('returns a real empty result rather than sample houses', async () => {
    units = [];
    expect(await getHouses({ ...user, role: 'admin' }, now)).toEqual([]);
  });

  test.each(['access_grants', 'units'])('surfaces a %s failure without falling back to demo data', async table => {
    failure = table;
    await expect(getHouses(tenant, now)).rejects.toMatchObject({ kind: 'server' });
  });

  test('does not expose demo houses after an unexpected SDK/network failure', async () => {
    client.from.mockImplementation(() => { throw new TypeError('Offline'); });
    await expect(getHouses(tenant, now)).rejects.toMatchObject({ kind: 'network' });
  });

  test.each(['tenant', 'manager', 'admin'] as const)('shows no placeholder houses for a demo %s account', async role => {
    vi.spyOn(api, 'isMockApiEnabled').mockReturnValue(true);
    expect(await getHouses({ ...tenant, role }, now)).toEqual([]);
    expect(requests).toHaveLength(0);
  });
});

describe('creating real houses after the merge', () => {
  const values = { name: ' Maple House ', address: ' 12 Maple Street ', building: ' A ', floor: ' 2 ', notes: ' Side door ' };

  test.each(['manager', 'admin'] as const)('%s saves a unit through Supabase with the signed-in creator', async role => {
    const insert = vi.fn(async () => ({ error: null }));
    client.from.mockReturnValue({ insert } as unknown as ReturnType<typeof client.from>);
    await createHouse({ ...user, role }, values);
    expect(client.from).toHaveBeenLastCalledWith('units');
    expect(insert).toHaveBeenCalledWith({ name: 'Maple House', address: '12 Maple Street', building: 'A', floor: '2', notes: 'Side door', created_by: user.id });
  });

  test('rejects tenants, blank names and demo sessions without a database write', async () => {
    await expect(createHouse(tenant, values)).rejects.toMatchObject({ status: 403 });
    await expect(createHouse({ ...user, role: 'manager' }, { ...values, name: ' ' })).rejects.toThrow('Enter a house name and address.');
    vi.spyOn(api, 'isMockApiEnabled').mockReturnValue(true);
    await expect(createHouse({ ...user, role: 'manager' }, values)).rejects.toMatchObject({ kind: 'not-configured' });
    expect(requests).toHaveLength(0);
  });

  test('surfaces database permission failures instead of saving a local demo house', async () => {
    failure = 'units';
    await expect(createHouse({ ...user, role: 'manager' }, values)).rejects.toMatchObject({ kind: 'request-failed' });
    expect(requests[0].pathname).toBe('/rest/v1/units');
  });
});
