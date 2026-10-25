import type { House, NewHouse } from '@/data/houses';
import { ApiError, isMockApiEnabled, request, type AuthUser } from '@/services/api';

//added admin here to match the Supabase branch and backend management permissions.
export function canManageHouses(role: AuthUser['role'] | undefined): boolean {
  return role === 'manager' || role === 'admin';
}

//did this through FastAPI because this branch stores its session outside the Supabase SDK.
export async function getHouses(_user: AuthUser): Promise<House[]> {
  if (isMockApiEnabled()) return [];
  const houses = await request<unknown>('/api/v1/houses');
  if (!Array.isArray(houses) || !houses.every(house => house &&
    typeof house.id === 'string' && typeof house.name === 'string' && typeof house.address === 'string')) {
    throw new ApiError('bad-response', 'Unexpected houses response.');
  }
  return houses;
}

//added this so the incoming add-house form persists a real unit instead of a demo record.
export async function createHouse(user: AuthUser, values: NewHouse): Promise<void> {
  if (!canManageHouses(user.role)) {
    throw new ApiError('request-failed', 'Only managers and admins can add houses.', 403);
  }
  const name = values.name.trim();
  const address = values.address.trim();
  if (!name || !address) throw new ApiError('request-failed', 'Enter a house name and address.');
  if (isMockApiEnabled()) throw new ApiError('not-configured', 'Sign in with a real account to add houses.');
  await request('/api/v1/houses', { method: 'POST', body: {
    name, address, building: values.building.trim(), floor: values.floor.trim(), notes: values.notes.trim(),
  } });
}
