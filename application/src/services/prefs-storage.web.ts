// Web implementation of the preferences store. `localStorage` (not `sessionStorage`) so a chosen
// language survives closing and reopening the browser tab.
import type { StringStorage } from "@/services/chunked-storage";

function store(): Storage | null {
  return typeof localStorage === "undefined" ? null : localStorage;
}

export const prefsStorage: StringStorage = {
  getItem: async (key) => store()?.getItem(key) ?? null,
  setItem: async (key, value) => {
    store()?.setItem(key, value);
  },
  removeItem: async (key) => {
    store()?.removeItem(key);
  },
};
