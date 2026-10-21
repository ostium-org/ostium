// AUTH — who is signed in, and how to sign in / out
// _layout.tsx wraps the app in <AuthProvider> and uses user to decide whether to show the
//login screen or the main app.
// login.tsx calls signIn(); settings.tsx calls signOut().
//front-end session handling ONLY

import {
  createContext,
  useCallback,
  useContext,
  useEffect,
  useMemo,
  useState,
  type ReactNode,
} from "react";

import {
  login,
  setAccessToken,
  setUnauthorizedHandler,
  signup,
  type AuthUser,
} from "@/services/api";
import type { StoredSession } from "@/services/session";
import {
  clearSession,
  loadSession,
  saveSession,
} from "@/services/token-storage";

type AuthContextValue = {
  user: AuthUser | null; // null = signed out
  isLoading: boolean; // true only while checking storage for an existing session at startup
  signIn: (email: string, password: string) => Promise<void>; // throws ApiError on failure
  signUp: (email: string, password: string, fullName?: string) => Promise<void>; // throws ApiError on failure
  signOut: () => Promise<void>;
};

const AuthContext = createContext<AuthContextValue | null>(null);

export function AuthProvider({ children }: { children: ReactNode }) {
  const [session, setSession] = useState<StoredSession | null>(null);
  const [isLoading, setIsLoading] = useState(true);

  // restore a saved session, if there is a valid one.
  useEffect(() => {
    let cancelled = false;

    loadSession()
      .then((saved) => {
        if (cancelled || !saved) return;
        setAccessToken(saved.accessToken);
        setSession(saved);
      })
      .finally(() => {
        if (!cancelled) setIsLoading(false);
      });

    return () => {
      cancelled = true;
    };
  }, []);

  const signOut = useCallback(async () => {
    // clear memory first so the UI reacts  immediately
    setAccessToken(null);
    setSession(null);
    await clearSession();
  }, []);

  // if any later request is rejected as unauthorized sign out so the
  // user lands back on the login screen instead of seeing broken screens.
  useEffect(() => {
    setUnauthorizedHandler(() => {
      void signOut();
    });
    return () => setUnauthorizedHandler(null);
  }, [signOut]);

  // Shared by signIn and signUp: persist the session and flip the UI to the app.
  const adoptSession = useCallback(
    async (result: {
      accessToken: string;
      expiresIn: number;
      user: AuthUser;
    }) => {
      const next: StoredSession = {
        accessToken: result.accessToken,
        expiresAt: Date.now() + result.expiresIn * 1000,
        user: result.user,
      };

      setAccessToken(next.accessToken);
      await saveSession(next).catch(() => {});
      setSession(next); // flips the app from the login screen to the main app
    },
    [],
  );

  const signIn = useCallback(
    async (email: string, password: string) => {
      const result = await login(email, password); // throws if the credentials are rejected
      await adoptSession(result);
    },
    [adoptSession],
  );

  const signUp = useCallback(
    async (email: string, password: string, fullName?: string) => {
      const result = await signup(email, password, fullName); // throws on failure
      await adoptSession(result);
    },
    [adoptSession],
  );

  const value = useMemo(
    () => ({ user: session?.user ?? null, isLoading, signIn, signUp, signOut }),
    [session, isLoading, signIn, signUp, signOut],
  );

  return <AuthContext.Provider value={value}>{children}</AuthContext.Provider>;
}

export function useAuth() {
  const context = useContext(AuthContext);
  // fail if a component is rendered outside the provider.
  if (!context) {
    throw new Error("useAuth must be used inside AuthProvider");
  }
  return context;
}
