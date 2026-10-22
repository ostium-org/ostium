import type { Session, SupabaseClient } from '@supabase/supabase-js';
import type { AuthUser } from '@/services/api';

export type AuthState = { user: AuthUser | null; isLoading: boolean; error?: unknown };

/** No SDK calls inside its auth callback: Supabase holds its session lock there. */
export function observeSession(
  client: SupabaseClient,
  profile: (session: Session) => Promise<AuthUser>,
  emit: (state: AuthState) => void,
): () => void {
  let stopped = false;
  let generation = 0;
  let events = 0;
  async function apply(session: Session | null) {
    const ticket = ++generation;
    if (!session) { if (!stopped) emit({ user: null, isLoading: false }); return; }
    try {
      const user = await profile(session);
      if (!stopped && ticket === generation) emit({ user, isLoading: false });
    } catch (error) {
      if (stopped || ticket !== generation) return;
      emit({ user: null, isLoading: false, error });
      if ((error as { status?: number })?.status === 403) await client.auth.signOut({ scope: 'local' });
    }
  }
  const { data: { subscription } } = client.auth.onAuthStateChange((_event, session) => {
    events++;
    // Invalidate in-flight profile loads immediately, before the deferred update.
    generation++;
    queueMicrotask(() => { if (!stopped) void apply(session); });
  });
  void client.auth.getSession().then(({ data, error }) => {
    if (stopped || events) return;
    if (error) emit({ user: null, isLoading: false, error });
    else void apply(data.session);
  }).catch((error) => { if (!stopped && !events) emit({ user: null, isLoading: false, error }); });
  return () => { stopped = true; generation++; subscription.unsubscribe(); };
}
