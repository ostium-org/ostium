import type { Session } from "@supabase/supabase-js";
import { getSupabase, supabaseUrl, supabaseKey } from "@/services/supabase";

//added this because active Supabase profiles can also belong to admins.
export type UserRole = "tenant" | "manager" | "admin";

export type AuthUser = {
  id: string;
  email: string;
  name?: string;
  role: UserRole;
};

export type LoginResult = {
  accessToken: string;
  expiresIn: number; // seconds
  user: AuthUser;
};

export type SignupResult = LoginResult | { confirmationRequired: true; email: string };

// What went wrong
export type ApiErrorKind =
  | "invalid-credentials" // login rejected (401)
  | "signup-rejected" // safe, actionable signup error from a known Auth code
  | "unauthorized" // token missing/expired/revoked on a signedin request (401)
  | "rate-limited" // 429
  | "server" // 5xx
  | "request-failed" // any other non-2xx
  | "bad-response" // 2xx but not the shape we expected
  | "network" // couldn't reach the server at all
  | "timeout" // no answer within REQUEST_TIMEOUT_MS
  | "not-configured"; // no API URL

export class ApiError extends Error {
  readonly kind: ApiErrorKind;
  readonly status?: number;

  constructor(kind: ApiErrorKind, message: string, status?: number) {
    super(message);
    this.name = "ApiError";
    this.kind = kind;
    this.status = status;
  }
}

const MOCK_ENABLED = __DEV__ && supabaseUrl === "" && process.env.EXPO_PUBLIC_MOCK_API === "true";
export function isMockApiEnabled(): boolean { return MOCK_ENABLED; }

const REQUEST_TIMEOUT_MS = 15_000;

// mock accounts for the fake login
const DEV_ACCOUNTS: { password: string; user: AuthUser }[] = [
  {
    password: "tenant-pass",
    user: {
      id: "dev-tenant",
      email: "tenant@example.com",
      name: "Dev Tenant",
      role: "tenant",
    },
  },
  {
    password: "manager-pass",
    user: {
      id: "dev-manager",
      email: "manager@example.com",
      name: "Dev Manager",
      role: "manager",
    },
  },
];

export function getDevLoginHint(): string | null {
  return MOCK_ENABLED
    ? 'Dev accounts: tenant@example.com / tenant-pass (tenant) | manager@example.com / manager-pass (manager)'
    : null;
}

function configured() {
  if (!supabaseUrl || !supabaseKey) throw new ApiError("not-configured", "Supabase public configuration is missing.");
  if (!__DEV__ && !supabaseUrl.startsWith("https://")) throw new ApiError("not-configured", "Supabase URL must use HTTPS.");
}

function authError(error: { status?: number; name?: string; message?: string }, loginAttempt = false): ApiError {
  const status = error.status;
  if (error.name === "AuthRetryableFetchError") return new ApiError("network", "Could not reach Supabase.", status);
  if (status === 429) return new ApiError("rate-limited", "Too many requests.", status);
  if (status && status >= 500) return new ApiError("server", "Server error.", status);
  if (status === 400 || status === 401 || status === 403) return new ApiError(loginAttempt ? "invalid-credentials" : "unauthorized", loginAttempt ? "Incorrect email or password." : "Not authorized.", status);
  return new ApiError("request-failed", "Authentication failed.", status);
}

function signupError(error: { code?: string; status?: number; name?: string; message?: string }): ApiError {
  // Translate known codes rather than exposing raw server error messages.
  switch (error.code) {
    case "email_exists":
    case "user_already_exists":
      return new ApiError("signup-rejected", "An account with this email already exists. Please sign in instead.", error.status);
    case "email_address_invalid":
      return new ApiError("signup-rejected", "Supabase rejected this email address. Please check it and try again.", error.status);
    case "weak_password":
      return new ApiError("signup-rejected", "This password does not meet the account security requirements. Please choose a stronger password.", error.status);
    case "signup_disabled":
      return new ApiError("signup-rejected", "Account registration is disabled. Please contact your project administrator.", error.status);
    case "email_address_not_authorized":
      return new ApiError("signup-rejected", "This project cannot send a confirmation email to this address. Please contact your project administrator.", error.status);
    default:
      return authError(error);
  }
}

export async function loadProfile(session: Session): Promise<AuthUser> {
  const client = await getSupabase();
  const { data, error } = await client.from("profiles").select("id, full_name, role, is_active").eq("id", session.user.id).maybeSingle();
  if (error) throw new ApiError("server", "Could not load your profile.");
  if (!data || data.is_active !== true) throw new ApiError("request-failed", "An active profile is required.", 403);
  const user = parseAuthUser({ id: data.id, email: session.user.email, name: data.full_name, role: data.role });
  if (!user) throw new ApiError("bad-response", "Invalid profile response.");
  return user;
}

async function sessionResult(session: Session): Promise<LoginResult> {
  try {
    return { accessToken: session.access_token, expiresIn: session.expires_in, user: await loadProfile(session) };
  } catch (error) {
    // Never leave a new session signed in after its profile was rejected.
    await (await getSupabase()).auth.signOut({ scope: "local" });
    throw error;
  }
}

type RequestOptions = {
  method?: "GET" | "POST" | "PUT" | "PATCH" | "DELETE";
  body?: unknown;
  authenticated?: boolean;
};

export async function request<T = unknown>(
  path: string,
  { method = "GET", body, authenticated = true }: RequestOptions = {},
): Promise<T> {
  configured();
  const url = `${supabaseUrl}/functions/v1/ostium-api${path}`;

  const headers: Record<string, string> = { Accept: "application/json", apikey: supabaseKey };
  if (body !== undefined) headers["Content-Type"] = "application/json";
  let sentToken = false;
  if (authenticated) {
    const { data, error } = await (await getSupabase()).auth.getSession();
    if (error) throw authError(error);
    if (!data.session) throw new ApiError("unauthorized", "Not authorized.", 401);
    headers.Authorization = `Bearer ${data.session.access_token}`;
    sentToken = true;
  }

  const controller = new AbortController();
  const timer = setTimeout(() => controller.abort(), REQUEST_TIMEOUT_MS);

  let status: number;
  let text: string;
  try {
    const response = await fetch(url, {
      method,
      headers,
      body: body === undefined ? undefined : JSON.stringify(body),
      signal: controller.signal,
    });
    status = response.status;
    text = await response.text();
  } catch (error) {
    if ((error as { name?: string } | null)?.name === "AbortError") {
      throw new ApiError("timeout", "The request timed out.");
    }
    throw new ApiError("network", "Could not reach the server.");
  } finally {
    clearTimeout(timer);
  }

  if (status >= 200 && status < 300) {
    if (text === "") return undefined as T;
    try {
      return JSON.parse(text) as T;
    } catch {
      throw new ApiError(
        "bad-response",
        "The server sent invalid JSON.",
        status,
      );
    }
  }

  if (status === 401) {
    if (sentToken) await (await getSupabase()).auth.signOut({ scope: "local" });
    throw new ApiError("unauthorized", "Not authorized.", status);
  }
  if (status === 429)
    throw new ApiError("rate-limited", "Too many requests.", status);
  if (status >= 500) throw new ApiError("server", "Server error.", status);
  throw new ApiError("request-failed", `Request failed (${status}).`, status);
}

export function parseAuthUser(value: unknown): AuthUser | null {
  if (typeof value !== "object" || value === null) return null;
  const v = value as Record<string, unknown>;

  const id = typeof v.id === "number" ? String(v.id) : v.id; // SQLite ids are often integers
  if (typeof id !== "string" || id === "") return null;
  if (typeof v.email !== "string" || v.email === "") return null;
  if (v.role !== "tenant" && v.role !== "manager" && v.role !== "admin") return null;
  if (v.name !== undefined && v.name !== null && typeof v.name !== "string")
    return null;

  return {
    id,
    email: v.email,
    role: v.role,
    ...(typeof v.name === "string" ? { name: v.name } : {}),
  };
}

async function mockLogin(
  email: string,
  password: string,
): Promise<LoginResult> {
  await new Promise((resolve) => setTimeout(resolve, 600)); // feel like a real network call
  const account = DEV_ACCOUNTS.find(
    (a) =>
      a.user.email === email.trim().toLowerCase() && a.password === password,
  );
  if (!account)
    throw new ApiError(
      "invalid-credentials",
      "Incorrect email or password.",
      401,
    );
  return {
    accessToken: `dev-token-${account.user.role}`,
    expiresIn: 60 * 60,
    user: account.user,
  };
}

export async function login(email: string, password: string): Promise<LoginResult> {
  if (MOCK_ENABLED) return mockLogin(email, password);
  configured();
  const { data, error } = await (await getSupabase()).auth.signInWithPassword({ email, password });
  if (error) throw authError(error, true);
  if (!data.session) throw new ApiError("bad-response", "Unexpected login response.");
  return sessionResult(data.session);
}

export async function signup(email: string, password: string, fullName?: string): Promise<SignupResult> {
  if (MOCK_ENABLED) return { accessToken: "dev-token-tenant", expiresIn: 3600,
    user: { id: "dev-tenant", email: email.trim().toLowerCase(), name: fullName, role: "tenant" } };
  configured();
  const { data, error } = await (await getSupabase()).auth.signUp({ email, password,
    options: { data: fullName?.trim() ? { full_name: fullName.trim() } : {} } });
  if (error) {
    if (__DEV__) console.warn("Supabase signup rejected", { code: error.code, status: error.status, name: error.name });
    throw signupError(error);
  }
  if (!data.session) return { confirmationRequired: true, email };
  return sessionResult(data.session);
}
