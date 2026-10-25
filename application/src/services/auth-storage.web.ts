import type { StringStorage } from '@/services/chunked-storage';

// Browser-only storage; web static rendering has no sessionStorage.
function store(): Storage | null {
  return typeof sessionStorage === 'undefined' ? null : sessionStorage;
}
export const authStorage: StringStorage = {
  getItem: async key => store()?.getItem(key) ?? null,
  setItem: async (key, value) => { store()?.setItem(key, value); },
  removeItem: async key => { store()?.removeItem(key); },
};
