import type { House, NewHouse } from '@/data/houses';
import { ApiError, isMockApiEnabled, type AuthUser } from '@/services/api';
import { getSupabase } from '@/services/supabase';

//added admin here because the old check only let managers see the houses.
export function canManageHouses(role: AuthUser['role'] | undefined): boolean {
  return role === 'manager' || role === 'admin';
}

type Grant = { unit_id: string; is_active: boolean; valid_from: string; valid_until: string | null };

export function currentUnitIds(grants: Grant[], now: number): string[] {
  //did this so future, expired, or disabled access does not show a house.
  return [...new Set(grants.filter(grant => grant.is_active &&
    Date.parse(grant.valid_from) <= now &&
    (grant.valid_until === null || Date.parse(grant.valid_until) > now),
  ).map(grant => grant.unit_id))];
}

//using the signed-in Supabase client so the database still checks permissions.
export async function getHouses(user: AuthUser, now = Date.now()): Promise<House[]> {
  if (isMockApiEnabled()) {
    //removed placeholder houses; demo accounts have no real Supabase session.
    return [];
  }

  try {
    const client = await getSupabase();
    let unitIds: string[] | undefined;
    if (!canManageHouses(user.role)) {
      //added this to get the tenant's actual house access from Supabase.
      const { data, error } = await client.from('access_grants')
        .select('unit_id,is_active,valid_from,valid_until')
        .eq('grantee_id', user.id).eq('is_active', true);
      if (error) throw new ApiError('server', 'Could not load your house access.');
      unitIds = currentUnitIds(data ?? [], now);
      if (!unitIds.length) return [];
    }

    //using real unit IDs here so opening a house and selecting it for QR match.
    let query = client.from('units').select('id,name,address').eq('is_active', true);
    if (unitIds) query = query.in('id', unitIds);
    const { data, error } = await query.order('name').order('id');
    if (error) throw new ApiError('server', 'Could not load your houses.');
    return (data ?? []).map(unit => ({ id: unit.id, name: unit.name, address: unit.address ?? '' }));
  } catch (error) {
    if (error instanceof ApiError) throw error;
    throw new ApiError('network', 'Could not load your houses. Please check your connection.');
  }
}

//added this so the new form saves real units using the signed-in client's RLS permissions.
export async function createHouse(user: AuthUser, values: NewHouse): Promise<void> {
  if (!canManageHouses(user.role)) {
    throw new ApiError('request-failed', 'Only managers and admins can add houses.', 403);
  }
  const name = values.name.trim();
  const address = values.address.trim();
  if (!name || !address) {
    throw new ApiError('request-failed', 'Enter a house name and address.');
  }
  if (isMockApiEnabled()) {
    throw new ApiError('not-configured', 'Sign in with a Supabase account to add houses.');
  }

  try {
    const client = await getSupabase();
    const { error } = await client.from('units').insert({
      name,
      address,
      building: values.building.trim(),
      floor: values.floor.trim(),
      notes: values.notes.trim(),
      created_by: user.id,
    });
    if (error) throw new ApiError('request-failed', 'Could not save the house. Please check your permissions and try again.');
  } catch (error) {
    if (error instanceof ApiError) throw error;
    throw new ApiError('network', 'Could not save the house. Please check your connection.');
  }
}
