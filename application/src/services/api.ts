export type UserRole = "tenant" | "manager";

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

const BASE_URL = (process.env.EXPO_PUBLIC_API_URL ?? "")
  .trim()
  .replace(/\/+$/, "");

// Demo mode must be intentional: missing configuration must not fake a signup.
const MOCK_ENABLED =
  __DEV__ && BASE_URL === "" && process.env.EXPO_PUBLIC_MOCK_API === "true";

export function isMockApiEnabled(): boolean {
  return MOCK_ENABLED;
}

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
    ? "Demo mode (no Supabase data): tenant@example.com / tenant-pass"
    : null;
}

function getBaseUrl(): string {
  if (BASE_URL === "") {
    throw new ApiError("not-configured", "EXPO_PUBLIC_API_URL is not set.");
  }

  if (!__DEV__ && !BASE_URL.startsWith("https://")) {
    throw new ApiError("not-configured", "The API URL must use HTTPS.");
  }
  return BASE_URL;
}

let accessToken: string | null = null;

export function setAccessToken(token: string | null) {
  accessToken = token;
}

let onUnauthorized: (() => void) | null = null;

export function setUnauthorizedHandler(handler: (() => void) | null) {
  onUnauthorized = handler;
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
  const url = `${getBaseUrl()}${path}`;

  const headers: Record<string, string> = { Accept: "application/json" };
  if (body !== undefined) headers["Content-Type"] = "application/json";
  const sentToken = authenticated && accessToken !== null;
  if (sentToken) headers.Authorization = `Bearer ${accessToken}`;

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
    if (sentToken) onUnauthorized?.();
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
  if (v.role !== "tenant" && v.role !== "manager") return null;
  if (v.name !== undefined && v.name !== null && typeof v.name !== "string")
    return null;

  return {
    id,
    email: v.email,
    role: v.role,
    ...(typeof v.name === "string" ? { name: v.name } : {}),
  };
}

function parseLoginResponse(data: unknown): LoginResult {
  const bad = () => new ApiError("bad-response", "Unexpected login response.");
  if (typeof data !== "object" || data === null) throw bad();
  const d = data as Record<string, unknown>;

  if (typeof d.access_token !== "string" || d.access_token === "") throw bad();
  if (
    typeof d.expires_in !== "number" ||
    !Number.isFinite(d.expires_in) ||
    d.expires_in <= 0
  ) {
    throw bad();
  }
  const user = parseAuthUser(d.user);
  if (!user) throw bad();

  return { accessToken: d.access_token, expiresIn: d.expires_in, user };
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

export async function login(
  email: string,
  password: string,
): Promise<LoginResult> {
  if (MOCK_ENABLED) return mockLogin(email, password);

  try {
    const data = await request("/api/v1/auth/login", {
      method: "POST",
      body: { email, password },
      authenticated: false, // we have no token yet
    });
    return parseLoginResponse(data);
  } catch (error) {
    if (error instanceof ApiError && error.kind === "unauthorized") {
      throw new ApiError(
        "invalid-credentials",
        "Incorrect email or password.",
        error.status,
      );
    }
    throw error;
  }
}

// Create a new account. The backend always creates the user as a 'tenant';
// only an admin can promote them later.
export async function signup(
  email: string,
  password: string,
  fullName?: string,
): Promise<SignupResult> {
  if (MOCK_ENABLED) {
    // In dev mode there is no real backend, so "signup" just signs the caller
    // in as a tenant so the rest of the app can be exercised.
    await new Promise((resolve) => setTimeout(resolve, 600));
    return {
      accessToken: "dev-token-tenant",
      expiresIn: 60 * 60,
      user: {
        id: "dev-tenant",
        email: email.trim().toLowerCase(),
        name: fullName,
        role: "tenant",
      },
    };
  }

  const body: Record<string, unknown> = { email, password };
  if (fullName && fullName.trim() !== "") body.full_name = fullName.trim();

  try {
    const data = await request("/api/v1/auth/signup", {
      method: "POST",
      body,
      authenticated: false, // we have no token yet
    });
    if (typeof data === "object" && data !== null &&
        "confirmation_required" in data && data.confirmation_required === true) {
      if (!("email" in data) || typeof data.email !== "string" || !data.email.trim()) {
        throw new ApiError("bad-response", "Unexpected signup response.");
      }
      return { confirmationRequired: true, email: data.email };
    }
    return parseLoginResponse(data);
  } catch (error) {
    // A 401 from signup means the email is already taken / auth rejected.
    if (error instanceof ApiError && error.kind === "unauthorized") {
      throw new ApiError(
        "request-failed",
        "That email may already be registered.",
        error.status,
      );
    }
    throw error;
  }
}
