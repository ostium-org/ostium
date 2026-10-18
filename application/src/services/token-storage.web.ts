// TOKEN STORAGE — web
// browser version of token-storage.ts 

import { parseStoredSession, type StoredSession } from '@/services/session';

const KEY = 'ostium.session';


function getStore(): Storage | null {
  try {
    return typeof sessionStorage === 'undefined' ? null : sessionStorage;
  } catch {
    return null;
  }
}

export async function saveSession(session: StoredSession): Promise<void> {
  getStore()?.setItem(KEY, JSON.stringify(session));
}

export async function loadSession(): Promise<StoredSession | null> {
  try {
    const raw = getStore()?.getItem(KEY) ?? null;
    const session = parseStoredSession(raw);
    if (raw && !session) await clearSession();
    return session;
  } catch {
    return null;
  }
}

export async function clearSession(): Promise<void> {
  try {
    getStore()?.removeItem(KEY);
  } catch {
    
  }
}