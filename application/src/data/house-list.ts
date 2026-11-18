import { useSyncExternalStore } from 'react';

const listeners = new Set<() => void>();
let revision = 0;

function subscribe(listener: () => void) {
  listeners.add(listener);
  return () => listeners.delete(listener);
}

export function useHouseListRevision() {
  return useSyncExternalStore(subscribe, () => revision, () => revision);
}

export function invalidateHouseList() {
  revision += 1;
  listeners.forEach(listener => listener());
}
