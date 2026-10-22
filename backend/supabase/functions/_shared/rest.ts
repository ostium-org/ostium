import { ApiError, type Row, type Settings, type Store } from "./api.ts";

export class RestStore implements Store {
  constructor(private url: string, private secret: string) {}

  private async call(
    path: string,
    init: RequestInit = {},
    token?: string,
  ): Promise<Response> {
    if (!this.url || !this.secret) {
      throw new ApiError(503, "Supabase is not configured.");
    }
    const headers = new Headers(init.headers);
    headers.set("apikey", this.secret);
    headers.set("Content-Type", "application/json");
    // New sb_secret keys belong on apikey; legacy service-role JWTs also use Authorization.
    if (token) headers.set("Authorization", `Bearer ${token}`);
    else if (!this.secret.startsWith("sb_secret_")) {
      headers.set("Authorization", `Bearer ${this.secret}`);
    }
    try {
      return await fetch(`${this.url.replace(/\/$/, "")}${path}`, {
        ...init,
        headers,
        signal: AbortSignal.timeout(20000),
      });
    } catch (error) {
      throw new ApiError(
        error instanceof DOMException &&
          ["TimeoutError", "AbortError"].includes(error.name)
          ? 504
          : 502,
        "Could not reach Supabase.",
      );
    }
  }

  async rows(table: string, filters: Record<string, string>): Promise<Row[]> {
    const result: Row[] = [];
    while (true) {
      const params = new URLSearchParams({
        select: "*",
        order: "id.asc",
        ...filters,
        offset: String(result.length),
        limit: "500",
      });
      const response = await this.call(`/rest/v1/${table}?${params}`);
      if (!response.ok) throw new ApiError(502, "Database read failed.");
      const page = await response.json();
      if (!Array.isArray(page)) {
        throw new ApiError(502, "Invalid database response.");
      }
      if (!page.length) return result;
      result.push(...page);
    }
  }

  async insert(table: string, row: Row): Promise<void> {
    const response = await this.call(`/rest/v1/${table}`, {
      method: "POST",
      headers: { Prefer: "return=minimal" },
      body: JSON.stringify(row),
    });
    if (response.ok) return;
    const details = await response.json().catch(() => ({}));
    if (response.status === 409 && details.code === "23505") {
      throw new ApiError(409, "Duplicate record.");
    }
    throw new ApiError(502, "Database write failed.");
  }

  async user(token: string): Promise<Row> {
    const response = await this.call("/auth/v1/user", {}, token);
    if ([400, 401, 403, 422].includes(response.status)) {
      throw new ApiError(401, "Invalid or expired token.");
    }
    if (!response.ok) throw new ApiError(502, "User validation unavailable.");
    const user = await response.json();
    if (!user?.id) throw new ApiError(502, "Invalid user response.");
    return user;
  }

  async role(userId: string, role: string): Promise<void> {
    const response = await this.call("/rest/v1/rpc/set_user_role", {
      method: "POST",
      body: JSON.stringify({ p_user_id: userId, p_role: role }),
    });
    if (!response.ok) throw new ApiError(502, "Role update failed.");
  }
}

export function environment(): { store: Store; settings: Settings } {
  const secret = Deno.env.get("SUPABASE_SERVICE_ROLE_KEY") ||
    Object.values(
      JSON.parse(Deno.env.get("SUPABASE_SECRET_KEYS") || "{}"),
    )[0] as string || "";
  return {
    store: new RestStore(Deno.env.get("SUPABASE_URL") || "", secret),
    settings: {
      privateKey: Deno.env.get("QR_SIGNING_PRIVATE_KEY"),
      issuer: Deno.env.get("QR_ISSUER") ?? "ostium",
      audience: Deno.env.get("QR_AUDIENCE") ?? "ostium-devices",
      keyId: Deno.env.get("QR_KEY_ID") ?? "ostium-qr-1",
      origins: Deno.env.get("CORS_ORIGINS") ?? "*",
    },
  };
}
