import { useSyncExternalStore } from 'react';

import { HOUSES } from '@/data/houses';

export type HouseMember = {
  id: string;
  name: string;
  email: string;
  role: 'Tenant';
};

const tenantMember: HouseMember = {
  id: 'dev-tenant',
  name: 'Dev Tenant',
  email: 'tenant@example.com',
  role: 'Tenant',
};

const membersByHouse = new Map<string, HouseMember[]>(
  HOUSES.map((house) => [house.id, [{ ...tenantMember }]]),
);
const listeners = new Set<() => void>();
let revision = 0;

function notify() {
  revision += 1;
  listeners.forEach((listener) => listener());
}

export function subscribeToHouseAccess(listener: () => void) {
  listeners.add(listener);
  return () => listeners.delete(listener);
}

export function useHouseAccessRevision() {
  return useSyncExternalStore(subscribeToHouseAccess, () => revision, () => revision);
}

export function getHouseMembers(houseId: string): HouseMember[] {
  return membersByHouse.get(houseId) ?? [];
}

export function userHasHouseAccess(houseId: string, userId: string, email: string): boolean {
  return getHouseMembers(houseId).some(
    (member) => member.id === userId || member.email === email.toLowerCase(),
  );
}

export function grantHouseAccess(houseId: string, member: HouseMember) {
  const current = getHouseMembers(houseId);
  if (current.some((item) => item.id === member.id || item.email === member.email)) return false;
  membersByHouse.set(houseId, [...current, member]);
  notify();
  return true;
}

export function revokeHouseAccess(houseId: string, memberId: string) {
  const current = getHouseMembers(houseId);
  const next = current.filter((member) => member.id !== memberId);
  if (next.length === current.length) return false;
  membersByHouse.set(houseId, next);
  notify();
  return true;
}