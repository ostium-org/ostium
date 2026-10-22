import { timingSafeEqual } from "node:crypto";

// deno-lint-ignore no-explicit-any
export type Row = Record<string, any>; // Database JSON rows; validated at route boundaries.
export interface Store {
  rows(table: string, filters: Record<string, string>): Promise<Row[]>;
  insert(table: string, row: Row): Promise<void>;
  user(token: string): Promise<Row>;
  role(userId: string, role: string): Promise<void>;
}
export interface Settings {
  privateKey?: string;
  issuer?: string;
  audience?: string;
  keyId?: string;
  origins?: string;
}
export class ApiError extends Error {
  constructor(public status: number, message: string) {
    super(message);
  }
}
const encoder = new TextEncoder();
export const REPORT_NAMESPACE = "8fc7b31b-f08a-4d13-8b61-899f720f3278";

export function timestamp(value: string): bigint {
  const match =
    /^(\d{4}-\d{2}-\d{2})[Tt ](\d{2}:\d{2}:\d{2})(?:\.(\d+))?([Zz]|[+-]\d{2}:\d{2})$/
      .exec(value);
  if (!match) {
    throw new ApiError(422, "A timezone-aware ISO timestamp is required.");
  }
  const milliseconds = Date.parse(`${match[1]}T${match[2]}${match[4]}`);
  const local = new Date(`${match[1]}T${match[2]}Z`);
  if (
    !Number.isFinite(milliseconds) || !Number.isFinite(local.getTime()) ||
    local.toISOString().slice(0, 19) !== `${match[1]}T${match[2]}`
  ) {
    throw new ApiError(422, "Invalid timestamp.");
  }
  return BigInt(milliseconds) * 1000n +
    BigInt((match[3] ?? "").padEnd(6, "0").slice(0, 6));
}
export function stamp(value: bigint): string {
  const fraction = ((value % 1000000n) + 1000000n) % 1000000n;
  const seconds = (value - fraction) / 1000000n;
  return new Date(Number(seconds) * 1000).toISOString().slice(0, 19) +
    (fraction ? `.${fraction.toString().padStart(6, "0")}` : "") + "Z";
}
function uuid(value: unknown): string {
  if (typeof value !== "string") throw new ApiError(422, "A UUID is required.");
  const hex = value.replaceAll("-", "").toLowerCase();
  if (!/^[0-9a-f]{32}$/.test(hex)) throw new ApiError(422, "Invalid UUID.");
  return `${hex.slice(0, 8)}-${hex.slice(8, 12)}-${hex.slice(12, 16)}-${
    hex.slice(16, 20)
  }-${hex.slice(20)}`;
}
export async function reportId(
  deviceId: string,
  eventId: string,
): Promise<string> {
  // Match Python json.dumps([device_id, event_id]): comma-space and ensure_ascii=True.
  const pythonJSON = `[${JSON.stringify(deviceId)}, ${JSON.stringify(eventId)}]`
    .replace(
      /[\u007f-\uffff]/g,
      (char) => `\\u${char.charCodeAt(0).toString(16).padStart(4, "0")}`,
    );
  const bytes = Uint8Array.from(
    REPORT_NAMESPACE.replaceAll("-", "").match(/../g)!,
    (b) => parseInt(b, 16),
  );
  const name = encoder.encode(pythonJSON);
  const input = new Uint8Array(bytes.length + name.length);
  input.set(bytes);
  input.set(name, bytes.length);
  const digest = new Uint8Array(await crypto.subtle.digest("SHA-1", input))
    .slice(0, 16);
  digest[6] = (digest[6] & 15) | 80;
  digest[8] = (digest[8] & 63) | 128;
  return uuid(
    Array.from(digest, (b) => b.toString(16).padStart(2, "0")).join(""),
  );
}
function base64url(bytes: Uint8Array): string {
  return btoa(String.fromCharCode(...bytes)).replaceAll("+", "-").replaceAll(
    "/",
    "_",
  ).replace(/=+$/, "");
}
export async function signJWT(
  claims: Row,
  settings: Settings,
): Promise<string> {
  if (
    !settings.privateKey || !settings.issuer || !settings.audience ||
    !settings.keyId
  ) {
    throw new ApiError(503, "QR signing is not configured.");
  }
  try {
    const pem = settings.privateKey.replaceAll("\\n", "\n");
    const encoded = pem.replace(
      /-----BEGIN PRIVATE KEY-----|-----END PRIVATE KEY-----|\s/g,
      "",
    );
    const bytes = Uint8Array.from(atob(encoded), (c) => c.charCodeAt(0));
    const key = await crypto.subtle.importKey(
      "pkcs8",
      bytes,
      "Ed25519",
      false,
      ["sign"],
    );
    const header = { alg: "EdDSA", typ: "JWT", kid: settings.keyId };
    const content = [header, claims].map((part) =>
      base64url(encoder.encode(JSON.stringify(part)))
    ).join(".");
    const signature = await crypto.subtle.sign(
      "Ed25519",
      key,
      encoder.encode(content),
    );
    return `${content}.${base64url(new Uint8Array(signature))}`;
  } catch {
    throw new ApiError(503, "QR signing is not configured correctly.");
  }
}
function bearer(request: Request): string {
  const parts = (request.headers.get("authorization") ?? "").trim().split(
    /\s+/,
  );
  if (parts.length !== 2 || parts[0].toLowerCase() !== "bearer" || !parts[1]) {
    throw new ApiError(401, "Missing or invalid bearer token.");
  }
  return parts[1];
}
async function body(request: Request, fields: string[]): Promise<Row> {
  let value;
  try {
    value = await request.json();
  } catch {
    throw new ApiError(422, "Invalid JSON body.");
  }
  if (
    !value || typeof value !== "object" || Array.isArray(value) ||
    Object.keys(value).some((key) => !fields.includes(key)) ||
    fields.some((key) => !(key in value))
  ) {
    throw new ApiError(422, "Missing or unexpected request fields.");
  }
  return value;
}
function action(value: unknown): asserts value is "lock" | "unlock" {
  if (value !== "lock" && value !== "unlock") {
    throw new ApiError(422, "Action must be lock or unlock.");
  }
}
async function activeUser(request: Request, store: Store): Promise<Row> {
  const user = await store.user(bearer(request));
  const profiles = await store.rows("profiles", { id: `eq.${user.id}` });
  if (!profiles[0] || profiles[0].is_active !== true) {
    throw new ApiError(403, "An active profile is required.");
  }
  return { ...user, role: profiles[0].role };
}
async function device(
  request: Request,
  store: Store,
  key: string,
): Promise<Row> {
  const token = bearer(request);
  const rows = await store.rows("devices", { device_key: `eq.${key}` });
  const row = rows[0];
  const actual = new Uint8Array(
    await crypto.subtle.digest("SHA-256", encoder.encode(token)),
  );
  const hash = row?.credential_hash;
  const expected = typeof hash === "string" && /^[a-f0-9]{64}$/.test(hash)
    ? Uint8Array.from(hash.match(/../g)!, (part) => parseInt(part, 16))
    : new Uint8Array(32);
  const matches = timingSafeEqual(actual, expected);
  if (!matches || !row || !["active", "offline"].includes(row.status)) {
    throw new ApiError(401, "Invalid device credentials.");
  }
  return row;
}
function sameReport(left: Row, right: Row): boolean {
  return ["kind", "event_id", "command_id", "action", "result", "reason"].every(
    (key) => left[key] === right[key],
  ) &&
    typeof left.occurred_at === "string" &&
    timestamp(left.occurred_at) === timestamp(right.occurred_at);
}

export function createHandler(
  mode: "user" | "device",
  store: Store,
  options: Settings = {},
  now: () => bigint = () => BigInt(Date.now()) * 1000n,
): (request: Request) => Promise<Response> {
  const settings = {
    issuer: "ostium",
    audience: "ostium-devices",
    keyId: "ostium-qr-1",
    ...options,
  };
  return async (request) => {
    const headers = new Headers({
      "Content-Type": "application/json",
      "Vary": "Origin",
    });
    const origin = request.headers.get("origin");
    const origins = (settings.origins ?? "*").split(",").map((value) =>
      value.trim()
    );
    if (origin && (origins.includes("*") || origins.includes(origin))) {
      headers.set(
        "Access-Control-Allow-Origin",
        origins.includes("*") ? "*" : origin,
      );
    }
    headers.set(
      "Access-Control-Allow-Headers",
      "authorization, apikey, content-type, x-client-info",
    );
    headers.set("Access-Control-Allow-Methods", "GET, POST, OPTIONS");
    const response = (value: unknown, status = 200) =>
      new Response(JSON.stringify(value), { status, headers });
    if (request.method === "OPTIONS") {
      return new Response(null, { status: 204, headers });
    }
    try {
      const path = new URL(request.url).pathname;
      const start = path.indexOf("/api/v1/");
      const route = start < 0 ? "" : path.slice(start);
      let match;
      if (mode === "user") {
        const user = await activeUser(request, store);
        if (
          (match = /^\/api\/v1\/doors\/([^/]+)\/commands$/.exec(route)) &&
          request.method === "POST"
        ) {
          if (!["manager", "admin"].includes(user.role)) {
            throw new ApiError(403, "Manager or admin access required.");
          }
          const input = await body(request, ["action"]);
          action(input.action);
          const door = decodeURIComponent(match[1]);
          const rows = await store.rows("devices", {
            device_key: `eq.${door}`,
          });
          const target = rows[0];
          if (!target) throw new ApiError(404, "Unknown door.");
          if (!["active", "offline"].includes(target.status)) {
            throw new ApiError(409, "Door device is not active or offline.");
          }
          const issued = now();
          const command = {
            command_id: crypto.randomUUID(),
            action: input.action,
            door_id: door,
            issued_at: stamp(issued),
            expires_at: stamp(issued + 10000000n),
          };
          await store.insert("access_events", {
            id: command.command_id,
            device_id: target.id,
            unit_id: target.unit_id ?? null,
            user_id: user.id,
            event_type: "config_sync",
            occurred_at: command.issued_at,
            metadata: { kind: "door_command", ...command },
          });
          return response(command, 201);
        }
        if (
          (match = /^\/api\/v1\/houses\/([^/]+)\/qr-credentials$/.exec(
            route,
          )) && request.method === "POST"
        ) {
          const unitId = uuid(match[1]);
          const units = await store.rows("units", {
            id: `eq.${unitId}`,
            is_active: "eq.true",
          });
          if (!units.length) {
            throw new ApiError(404, "Unknown or inactive unit.");
          }
          const issued = now();
          let expires = issued + 60000000n;
          let grant: Row | undefined;
          if (!["manager", "admin"].includes(user.role)) {
            const grants = await store.rows("access_grants", {
              unit_id: `eq.${unitId}`,
              grantee_id: `eq.${user.id}`,
              is_active: "eq.true",
            });
            grant = grants.find((g) =>
              timestamp(g.valid_from) <= issued &&
              (!g.valid_until || timestamp(g.valid_until) > issued)
            );
            if (!grant) throw new ApiError(403, "No current access grant.");
            if (grant.valid_until && timestamp(grant.valid_until) < expires) {
              expires = timestamp(grant.valid_until);
            }
          }
          const jti = crypto.randomUUID();
          const claims = {
            iss: settings.issuer,
            aud: settings.audience,
            sub: user.id,
            unit_id: unitId,
            jti,
            iat: Number(issued) / 1e6,
            exp: Number(expires) / 1e6,
          };
          const payload = await signJWT(claims, settings);
          await store.insert("credentials", {
            id: jti,
            jti,
            grant_id: grant?.id ?? null,
            unit_id: unitId,
            issued_to: user.id,
            issued_by: user.id,
            issued_at: stamp(issued),
            expires_at: stamp(expires),
            payload: { claims, token: payload },
          });
          return response({
            house_id: unitId,
            payload,
            expires_at: stamp(expires),
          }, 201);
        }
        if (
          (match = /^\/api\/v1\/auth\/users\/([^/]+)\/role$/.exec(route)) &&
          request.method === "POST"
        ) {
          if (user.role !== "admin") {
            throw new ApiError(403, "Admin access required.");
          }
          const input = await body(request, ["role"]);
          if (!["tenant", "manager", "admin"].includes(input.role)) {
            throw new ApiError(422, "Invalid role.");
          }
          const id = uuid(match[1]);
          await store.role(id, input.role);
          return response({ detail: `Role for ${id} set to ${input.role}.` });
        }
      } else if (
        (match = /^\/api\/v1\/devices\/([^/]+)\/(commands|events)$/.exec(route))
      ) {
        const target = await device(
          request,
          store,
          decodeURIComponent(match[1]),
        );
        if (match[2] === "commands" && request.method === "GET") {
          const time = now();
          const rows = await store.rows("access_events", {
            device_id: `eq.${target.id}`,
            event_type: "eq.config_sync",
          });
          const reported = new Set(
            rows.filter((r) => r.metadata?.kind === "command_result").map((r) =>
              r.metadata.command_id
            ),
          );
          const commands = rows.filter((r) =>
            r.metadata?.kind === "door_command" && !reported.has(r.id) &&
            timestamp(r.metadata.expires_at) > time
          )
            .map((r) =>
              Object.fromEntries(
                ["command_id", "action", "door_id", "issued_at", "expires_at"]
                  .map((key) => [key, r.metadata[key]]),
              )
            );
          commands.sort((a, b) =>
            timestamp(a.issued_at) < timestamp(b.issued_at)
              ? -1
              : timestamp(a.issued_at) > timestamp(b.issued_at)
              ? 1
              : a.command_id < b.command_id
              ? -1
              : a.command_id > b.command_id
              ? 1
              : 0
          );
          return response({ server_time: stamp(time), commands });
        }
        if (match[2] === "events" && request.method === "POST") {
          const input = await body(request, [
            "event_id",
            "command_id",
            "action",
            "result",
            "reason",
            "occurred_at",
          ]);
          action(input.action);
          if (
            typeof input.event_id !== "string" ||
            Array.from(input.event_id).length < 1 ||
            Array.from(input.event_id).length > 200 ||
            !["done", "rejected"].includes(input.result) ||
            (input.reason !== null && typeof input.reason !== "string") ||
            typeof input.occurred_at !== "string"
          ) {
            throw new ApiError(422, "Invalid command report.");
          }
          const report: Row = {
            ...input,
            command_id: uuid(input.command_id),
            occurred_at: stamp(timestamp(input.occurred_at)),
          };
          const id = await reportId(target.id, report.event_id);
          const metadata = { kind: "command_result", ...report };
          const checkExisting = async () => {
            const rows = await store.rows("access_events", { id: `eq.${id}` });
            if (!rows.length) return false;
            if (
              rows[0].device_id !== target.id ||
              !sameReport(rows[0].metadata, metadata)
            ) {
              throw new ApiError(
                409,
                "Event ID already used for a different report.",
              );
            }
            return true;
          };
          const result = {
            event_id: report.event_id,
            command_id: report.command_id,
            recorded: true,
          };
          if (await checkExisting()) return response(result);
          const commands = await store.rows("access_events", {
            id: `eq.${report.command_id}`,
            device_id: `eq.${target.id}`,
            event_type: "eq.config_sync",
            "metadata->>kind": "eq.door_command",
          });
          if (!commands.length) {
            throw new ApiError(404, "Unknown command for this device.");
          }
          if (commands[0].metadata.action !== report.action) {
            throw new ApiError(409, "Command action does not match.");
          }
          try {
            await store.insert("access_events", {
              id,
              device_id: target.id,
              unit_id: target.unit_id ?? null,
              event_type: "config_sync",
              occurred_at: report.occurred_at,
              metadata,
            });
          } catch (error) {
            if (!(error instanceof ApiError) || error.status !== 409) {
              throw error;
            }
            if (!await checkExisting()) {
              throw new ApiError(409, "Report collision; retry delivery.");
            }
          }
          return response(result);
        }
      }
      throw new ApiError(404, "Unknown endpoint.");
    } catch (error) {
      return response({
        detail: error instanceof ApiError
          ? error.message
          : "Service unavailable.",
      }, error instanceof ApiError ? error.status : 502);
    }
  };
}
