// the shape of a signed-in session and how to safely read one back from storage.
// shared by token-storage.ts (phones) and token-storage.web.ts (browsers).

import { parseAuthUser, type AuthUser } from '@/services/api';

export type StoredSession = {
  accessToken: string;
  expiresAt: number; // when the token expires
  user: AuthUser;
};


const EXPIRY_MARGIN_MS = 30_000;

// storage holds text that something else wrote, so it's validated before use.
export function parseStoredSession(raw: string | null, now: number = Date.now()): StoredSession | null {
  if (!raw) return null;

  try {
    const value: unknown = JSON.parse(raw);
    if (typeof value !== 'object' || value === null) return null;
    const v = value as Record<string, unknown>;

    if (typeof v.accessToken !== 'string' || v.accessToken === '') return null;
    if (typeof v.expiresAt !== 'number' || !Number.isFinite(v.expiresAt)) return null;
    if (v.expiresAt <= now + EXPIRY_MARGIN_MS) return null;
    const user = parseAuthUser(v.user);
    if (!user) return null;

    return { accessToken: v.accessToken, expiresAt: v.expiresAt, user };
  } catch {
    return null;
  }
}