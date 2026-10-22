import { createContext, useCallback, useContext, useEffect, useMemo, useState, type ReactNode } from 'react';
import { AppState, Platform } from 'react-native';

import { isMockApiEnabled, loadProfile, login, signup, type AuthUser } from '@/services/api';
import { observeSession, type AuthState } from '@/services/session';
import { getSupabase } from '@/services/supabase';

type AuthContextValue = {
  user: AuthUser | null;
  isLoading: boolean;
  signIn: (email: string, password: string) => Promise<void>;
  signUp: (email: string, password: string, fullName?: string) => Promise<'signed-in' | 'confirmation-required'>;
  signOut: () => Promise<void>;
};
const AuthContext = createContext<AuthContextValue | null>(null);

export function AuthProvider({ children }: { children: ReactNode }) {
  const [state, setState] = useState<AuthState>({ user: null, isLoading: true });
  useEffect(() => {
    let stopped = false;
    let unsubscribe: (() => void) | undefined;
    let appState: ReturnType<typeof AppState.addEventListener> | undefined;
    let stopRefresh: (() => void) | undefined;
    if (isMockApiEnabled()) {
      queueMicrotask(() => { if (!stopped) setState({ user: null, isLoading: false }); });
    } else {
      void getSupabase().then((client) => {
        if (stopped) return;
        unsubscribe = observeSession(client, loadProfile, setState);
        if (Platform.OS !== 'web') {
          const refresh = (value: string) => {
            if (value === 'active') client.auth.startAutoRefresh();
            else client.auth.stopAutoRefresh();
          };
          refresh(AppState.currentState);
          appState = AppState.addEventListener('change', refresh);
          stopRefresh = () => client.auth.stopAutoRefresh();
        }
      }).catch((error) => { if (!stopped) setState({ user: null, isLoading: false, error }); });
    }
    return () => { stopped = true; unsubscribe?.(); appState?.remove(); stopRefresh?.(); };
  }, []);

  const signIn = useCallback(async (email: string, password: string) => {
    const result = await login(email, password);
    if (isMockApiEnabled()) setState({ user: result.user, isLoading: false });
  }, []);
  const signUp = useCallback(async (email: string, password: string, fullName?: string) => {
    const result = await signup(email, password, fullName);
    if ('confirmationRequired' in result) return 'confirmation-required' as const;
    if (isMockApiEnabled()) setState({ user: result.user, isLoading: false });
    return 'signed-in' as const;
  }, []);
  const signOut = useCallback(async () => {
    if (!isMockApiEnabled()) {
      const { error } = await (await getSupabase()).auth.signOut({ scope: 'local' });
      if (error) throw error;
    }
    setState({ user: null, isLoading: false });
  }, []);
  const value = useMemo(() => ({ user: state.user, isLoading: state.isLoading, signIn, signUp, signOut }), [state.user, state.isLoading, signIn, signUp, signOut]);
  return <AuthContext.Provider value={value}>{children}</AuthContext.Provider>;
}
export function useAuth() {
  const context = useContext(AuthContext);
  if (!context) throw new Error('useAuth must be used inside AuthProvider');
  return context;
}
