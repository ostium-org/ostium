import { describe, expect, mock, test } from 'bun:test';
import type { Session, SupabaseClient } from '@supabase/supabase-js';
import { observeSession, type AuthState } from '@/services/session';
import { session } from './supabase-mock';

const tick = () => new Promise<void>((resolve) => setTimeout(resolve, 0));
const user = { id: 'tenant-1', email: 'tenant@example.com', role: 'tenant' as const };
function fixture() {
  let callback: (event: string, session: Session | null) => void;
  const unsubscribe = mock(() => {});
  const signOut = mock(async () => ({ error: null }));
  const client = { auth: {
    getSession: mock(async () => ({ data: { session }, error: null })),
    onAuthStateChange: (fn: typeof callback) => { callback = fn; return { data: { subscription: { unsubscribe } } }; },
    signOut,
  } };
  const states: AuthState[] = [];
  return { client: client as unknown as SupabaseClient, states, unsubscribe, signOut,
    event: (value: Session | null) => callback('TEST', value),
    emit: (state: AuthState) => states.push(state) };
}

describe('Supabase session lifecycle', () => {
  test('restores a session through its active profile', async () => {
    const f = fixture();
    const profile = mock(async () => user);
    const stop = observeSession(f.client, profile, f.emit);
    await tick();
    expect(f.states.at(-1)).toEqual({ user, isLoading: false });
    expect(profile).toHaveBeenCalledTimes(1);
    stop(); expect(f.unsubscribe).toHaveBeenCalledTimes(1);
  });
  test('does not restore a delayed profile after sign-out', async () => {
    const f = fixture();
    let finish!: (value: typeof user) => void;
    const stop = observeSession(f.client, () => new Promise((resolve) => { finish = resolve; }), f.emit);
    await tick();
    f.event(null);
    finish(user);
    await tick();
    expect(f.states).toEqual([{ user: null, isLoading: false }]);
    stop();
  });
  test('ignores profile completion after unmount', async () => {
    const f = fixture();
    let finish!: (value: typeof user) => void;
    const stop = observeSession(f.client, () => new Promise((resolve) => { finish = resolve; }), f.emit);
    await tick(); stop(); finish(user); await tick();
    expect(f.states).toEqual([]);
  });
  test('loads the current profile on a refreshed session', async () => {
    const f = fixture();
    const profile = mock(async () => user);
    const stop = observeSession(f.client, profile, f.emit);
    await tick();
    f.event({ ...session, access_token: 'new-token' } as Session);
    await tick();
    expect(profile).toHaveBeenCalledTimes(2);
    expect(f.states.at(-1)?.user).toEqual(user);
    stop();
  });
  test('rejects disabled profiles and clears the local session', async () => {
    const f = fixture();
    const error = { status: 403 };
    const stop = observeSession(f.client, async () => { throw error; }, f.emit);
    await tick();
    expect(f.states.at(-1)).toEqual({ user: null, isLoading: false, error });
    expect(f.signOut).toHaveBeenCalledWith({ scope: 'local' });
    stop();
  });
  test('reports transient profile failures without deleting refresh credentials', async () => {
    const f = fixture();
    const stop = observeSession(f.client, async () => { throw new Error('offline'); }, f.emit);
    await tick();
    expect(f.states.at(-1)?.isLoading).toBe(false);
    expect(f.signOut).not.toHaveBeenCalled();
    stop();
  });
});
