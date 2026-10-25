import { vi } from 'vitest';

export const user = { id: 'tenant-1', email: 'tenant@example.com' };
export const session = { access_token: 'session-token', refresh_token: 'refresh-token', token_type: 'bearer', expires_in: 3600,
  expires_at: Math.floor(Date.now() / 1000) + 3600, user };
export const state = {
  session: session as typeof session | null,
  profile: { id: user.id, full_name: 'Tenant', role: 'tenant', is_active: true } as Record<string, unknown> | null,
  profileError: null as unknown,
};
const query = { eq: vi.fn(() => query), maybeSingle: vi.fn(async () => ({ data: state.profile, error: state.profileError })) };
export const client = {
  auth: {
    getSession: vi.fn(async () => ({ data: { session: state.session }, error: null })),
    signInWithPassword: vi.fn(async () => ({ data: { session: state.session }, error: null as unknown })),
    signUp: vi.fn(async () => ({ data: { session: state.session, user }, error: null as unknown })),
    signOut: vi.fn(async () => ({ error: null })),
  },
  from: vi.fn(() => ({ select: vi.fn(() => query) })),
};
export function resetSupabase() {
  state.session = session;
  state.profile = { id: user.id, full_name: 'Tenant', role: 'tenant', is_active: true };
  state.profileError = null;
  client.auth.getSession.mockReset().mockImplementation(async () => ({ data: { session: state.session }, error: null }));
  client.auth.signInWithPassword.mockReset().mockImplementation(async () => ({ data: { session: state.session }, error: null }));
  client.auth.signUp.mockReset().mockImplementation(async () => ({ data: { session: state.session, user }, error: null }));
  client.auth.signOut.mockClear();
}
