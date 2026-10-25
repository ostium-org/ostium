import {
  ApiError,
  createHandler,
  reportId,
  type Row,
  stamp,
  type Store,
  timestamp,
} from "../functions/_shared/api.ts";
import { RestStore } from "../functions/_shared/rest.ts";
import golden from "./python_compatibility.json" with { type: "json" };

function equal(actual: unknown, expected: unknown) {
  if (JSON.stringify(actual) !== JSON.stringify(expected)) {
    throw new Error(
      `Expected ${JSON.stringify(expected)}, received ${
        JSON.stringify(actual)
      }`,
    );
  }
}
function assert(value: unknown, message = "Assertion failed"): asserts value {
  if (!value) throw new Error(message);
}
const USER = "11111111-1111-4111-8111-111111111111";
const UNIT = "22222222-2222-4222-8222-222222222222";
const DEVICE = "33333333-3333-4333-8333-333333333333";
const NOW = timestamp("2026-10-22T12:00:00.123456Z");
const encoder = new TextEncoder();
const decode = (text: string) =>
  Uint8Array.from(
    atob(text.replaceAll("-", "+").replaceAll("_", "/")),
    (c) => c.charCodeAt(0),
  );

class Memory implements Store {
  roleName = "manager";
  active = true;
  missingProfile = false;
  failWrite = false;
  userError: Error | undefined;
  tables: Record<string, Row[]> = {
    profiles: [],
    devices: [{
      id: DEVICE,
      device_key: "front",
      status: "active",
      unit_id: UNIT,
      credential_hash: golden.device_hash,
    }],
    units: [{ id: UNIT, is_active: true }],
    access_grants: [],
    credentials: [],
    access_events: [],
  };
  roleChanges: string[] = [];
  // deno-lint-ignore require-await
  async user(token: string): Promise<Row> {
    if (this.userError) throw this.userError;
    if (token !== "user-token") throw new ApiError(401, "Invalid token.");
    return { id: USER, email: "tenant@example.com" };
  }
  // deno-lint-ignore require-await
  async role(_id: string, role: string) {
    this.roleChanges.push(role);
  }
  async rows(table: string, filters: Record<string, string>): Promise<Row[]> {
    const source = table === "profiles"
      ? this.missingProfile
        ? []
        : [{ id: USER, role: this.roleName, is_active: this.active }]
      : this.tables[table];
    const result = structuredClone(
      source.filter((row) =>
        Object.entries(filters).every(([key, expected]) => {
          const value = key.includes("->>")
            ? row[key.split("->>")[0]]?.[key.split("->>")[1]]
            : row[key];
          return String(value) === expected.slice(3);
        })
      ),
    );
    await Promise.resolve();
    return result;
  }
  async insert(table: string, row: Row) {
    await Promise.resolve();
    if (this.failWrite) throw new ApiError(502, "Write failed.");
    if (this.tables[table].some((item) => item.id === row.id)) {
      throw new ApiError(409, "Duplicate.");
    }
    this.tables[table].push(structuredClone(row));
  }
}
function fixture() {
  const store = new Memory();
  const clock = { now: NOW };
  const settings = { privateKey: golden.private_key };
  const user = createHandler("user", store, settings, () => clock.now);
  const device = createHandler("device", store, settings, () => clock.now);
  const call = (
    mode: "user" | "device",
    path: string,
    body?: unknown,
    token: string | null = mode === "user" ? "user-token" : "device-token",
  ) => {
    const headers: Record<string, string> = {
      "content-type": "application/json",
    };
    if (token !== null) headers.authorization = `Bearer ${token}`;
    return (mode === "user" ? user : device)(
      new Request(
        `https://project.test/functions/v1/${mode}-api/api/v1${path}`,
        {
          method: body === undefined ? "GET" : "POST",
          headers,
          body: body === undefined ? undefined : JSON.stringify(body),
        },
      ),
    );
  };
  const issue = async (action = "unlock") => {
    const result = await call("user", "/doors/front/commands", { action });
    equal(result.status, 201);
    return result.json();
  };
  const poll = async () =>
    (await call("device", "/devices/front/commands")).json();
  const report = (command: Row, extra: Row = {}) => ({
    event_id: "event-1",
    command_id: command.command_id,
    action: command.action,
    result: "done",
    reason: null,
    occurred_at: "2026-10-22T09:00:00-03:00",
    ...extra,
  });
  const qr = () => call("user", `/houses/${UNIT}/qr-credentials`, null);
  return {
    store,
    clock,
    settings,
    user,
    device,
    call,
    issue,
    poll,
    report,
    qr,
  };
}

Deno.test("user authorization and the existing admin role endpoint", async () => {
  const f = fixture();
  for (const token of [null, "invalid"]) {
    equal(
      (await f.call("user", "/doors/front/commands", { action: "lock" }, token))
        .status,
      401,
    );
  }
  f.store.roleName = "tenant";
  equal(
    (await f.call("user", "/doors/front/commands", { action: "lock" })).status,
    403,
  );
  for (const role of ["tenant", "manager"]) {
    f.store.roleName = role;
    equal(
      (await f.call("user", `/auth/users/${USER}/role`, { role: "admin" }))
        .status,
      403,
    );
  }
  equal(f.store.roleChanges, []);
  f.store.roleName = "admin";
  equal(
    (await f.call("user", `/auth/users/${USER}/role`, { role: "tenant" }))
      .status,
    200,
  );
  equal(f.store.roleChanges, ["tenant"]);
  f.store.active = false;
  equal((await f.qr()).status, 403);
  equal(
    (await f.call("user", "/doors/front/commands", { action: "lock" })).status,
    403,
  );
  f.store.missingProfile = true;
  equal((await f.qr()).status, 403);
});

Deno.test("commands keep UUIDs, ten-second TTL, metadata and manager/admin access", async () => {
  const f = fixture();
  for (const role of ["manager", "admin"]) {
    f.store.roleName = role;
    const command = await f.issue("lock");
    assert(
      timestamp(command.expires_at) - timestamp(command.issued_at) ===
        10000000n,
    );
  }
});

Deno.test("command validation and device statuses", async () => {
  const f = fixture();
  const command = await f.issue();
  equal(command.door_id, "front");
  equal(f.store.tables.access_events[0].id, command.command_id);
  equal(f.store.tables.access_events[0].metadata.kind, "door_command");
  assert(!("result" in f.store.tables.access_events[0]));
  for (
    const input of [{ action: "open" }, {}, { action: "lock", extra: true }]
  ) equal((await f.call("user", "/doors/front/commands", input)).status, 422);
  equal(
    (await f.call("user", "/doors/unknown/commands", { action: "lock" }))
      .status,
    404,
  );
  for (const status of ["disabled", "provisioned"]) {
    f.store.tables.devices[0].status = status;
    equal(
      (await f.call("user", "/doors/front/commands", { action: "lock" }))
        .status,
      409,
    );
    equal((await f.call("device", "/devices/front/commands")).status, 401);
    equal(
      (await f.call("device", "/devices/front/events", f.report(command)))
        .status,
      401,
    );
  }
  f.store.tables.devices[0].status = "offline";
  await f.issue();
  equal((await f.call("device", "/devices/front/commands")).status, 200);
});

Deno.test("device credentials, device isolation and event ID scope", async () => {
  const f = fixture();
  for (const token of [null, "wrong", "user-token"]) {
    equal(
      (await f.call("device", "/devices/front/commands", undefined, token))
        .status,
      401,
    );
  }
  equal((await f.call("device", "/devices/missing/commands")).status, 401);
  const command = await f.issue();
  f.store.tables.devices.push({
    ...f.store.tables.devices[0],
    id: crypto.randomUUID(),
    device_key: "back",
  });
  equal(
    (await (await f.call("device", "/devices/back/commands")).json()).commands,
    [],
  );
  equal(
    (await f.call("device", "/devices/back/events", f.report(command))).status,
    404,
  );
  const back =
    await (await f.call("user", "/doors/back/commands", { action: "unlock" }))
      .json();
  equal(
    (await f.call("device", "/devices/front/events", f.report(command))).status,
    200,
  );
  equal(
    (await f.call("device", "/devices/back/events", f.report(back))).status,
    200,
  );
  equal(f.store.tables.access_events.length, 4);
});

Deno.test("polls repeat, sort by timestamp/ID and respect exact microsecond expiry", async () => {
  const f = fixture();
  const commands = [await f.issue(), await f.issue("lock")].sort((a, b) =>
    a.command_id < b.command_id ? -1 : 1
  );
  equal((await f.poll()).commands, commands);
  equal((await f.poll()).commands, commands);
  f.clock.now = NOW + 9999999n;
  equal((await f.poll()).commands.length, 2);
  f.clock.now = NOW + 10000000n;
  equal((await f.poll()).commands, []);
  f.clock.now = NOW + 1000000n;
  const later = await f.issue();
  equal((await f.poll()).commands, [...commands, later]);
});

Deno.test("done and rejected reports accept late delivery and timezone-equivalent retries", async () => {
  for (const result of ["done", "rejected"]) {
    const f = fixture();
    const command = await f.issue();
    f.clock.now = NOW + 86400000000n;
    equal(
      (await f.call(
        "device",
        "/devices/front/events",
        f.report(command, { result }),
      )).status,
      200,
    );
    equal(
      (await f.call(
        "device",
        "/devices/front/events",
        f.report(command, {
          result,
          occurred_at: "2026-10-22T12:00:00.000000Z",
        }),
      )).status,
      200,
    );
    f.clock.now = NOW;
    equal((await f.poll()).commands, []);
    equal(f.store.tables.access_events.length, 2);
  }
});

Deno.test("concurrent exact reports create only one row; conflicts return 409", async () => {
  const f = fixture();
  const command = await f.issue();
  const input = f.report(command);
  const responses = await Promise.all(
    Array.from(
      { length: 8 },
      () => f.call("device", "/devices/front/events", input),
    ),
  );
  equal(responses.map((r) => r.status), Array(8).fill(200));
  equal(f.store.tables.access_events.length, 2);
  for (
    const extra of [
      { result: "rejected" },
      { reason: "jammed" },
      { action: "lock" },
      { command_id: crypto.randomUUID() },
      { occurred_at: "2026-10-22T12:00:01Z" },
    ]
  ) {
    equal(
      (await f.call("device", "/devices/front/events", { ...input, ...extra }))
        .status,
      409,
    );
  }
  const fresh = fixture();
  const cmd = await fresh.issue();
  const conflict = await Promise.all([
    fresh.call("device", "/devices/front/events", fresh.report(cmd)),
    fresh.call(
      "device",
      "/devices/front/events",
      fresh.report(cmd, { reason: "different" }),
    ),
  ]);
  equal(conflict.map((r) => r.status).sort(), [200, 409]);
});

Deno.test("unknown commands, mismatched actions and invalid reports", async () => {
  const f = fixture();
  const command = await f.issue();
  equal(
    (await f.call(
      "device",
      "/devices/front/events",
      f.report(command, { command_id: crypto.randomUUID() }),
    )).status,
    404,
  );
  equal(
    (await f.call(
      "device",
      "/devices/front/events",
      f.report(command, { action: "lock" }),
    )).status,
    409,
  );
  for (
    const extra of [
      { result: "granted" },
      { reason: 42 },
      { event_id: "" },
      { event_id: "a".repeat(201) },
      { occurred_at: "2026-10-22T12:00:00" },
      { occurred_at: "2026-02-30T12:00:00Z" },
      { extra: true },
    ]
  ) {
    equal(
      (await f.call(
        "device",
        "/devices/front/events",
        f.report(command, extra),
      )).status,
      422,
    );
  }
  const missing: Row = f.report(command);
  delete missing.reason;
  equal((await f.call("device", "/devices/front/events", missing)).status, 422);
});

Deno.test("QR tokens verify with the same public key and persist exact claims", async () => {
  const f = fixture();
  const result = await f.qr();
  equal(result.status, 201);
  const data = await result.json();
  const [header, payload, signature] = data.payload.split(".");
  const key = await crypto.subtle.importKey(
    "raw",
    decode(golden.public_key),
    "Ed25519",
    false,
    ["verify"],
  );
  assert(
    await crypto.subtle.verify(
      "Ed25519",
      key,
      decode(signature),
      encoder.encode(`${header}.${payload}`),
    ),
  );
  const claims = JSON.parse(new TextDecoder().decode(decode(payload)));
  equal(JSON.parse(new TextDecoder().decode(decode(header))), {
    alg: "EdDSA",
    typ: "JWT",
    kid: "ostium-qr-1",
  });
  equal(claims.sub, USER);
  equal(claims.unit_id, UNIT);
  equal(claims.iss, "ostium");
  equal(claims.aud, "ostium-devices");
  equal(claims.iat, Number(NOW) / 1e6);
  equal(claims.exp, Number(NOW + 60000000n) / 1e6);
  equal(f.store.tables.credentials[0].jti, claims.jti);
  equal(f.store.tables.credentials[0].payload, { claims, token: data.payload });
  equal(f.store.tables.credentials[0].expires_at, data.expires_at);
  assert((await (await f.qr()).json()).payload !== data.payload);
  const [oldHeader, oldPayload, oldSignature] = golden.python_jwt.split(".");
  assert(
    await crypto.subtle.verify(
      "Ed25519",
      key,
      decode(oldSignature),
      encoder.encode(`${oldHeader}.${oldPayload}`),
    ),
  );
});

Deno.test("tenant grant boundaries and subsecond expiry caps", async () => {
  const f = fixture();
  f.store.roleName = "tenant";
  equal((await f.qr()).status, 403);
  const grant = {
    id: crypto.randomUUID(),
    unit_id: UNIT,
    grantee_id: USER,
    is_active: true,
    valid_from: stamp(NOW),
    valid_until: null as string | null,
  };
  for (
    const extra of [
      { valid_from: stamp(NOW + 1n) },
      { valid_until: stamp(NOW) },
      { is_active: false },
      { grantee_id: crypto.randomUUID() },
      { unit_id: crypto.randomUUID() },
    ]
  ) {
    f.store.tables.access_grants = [{ ...grant, ...extra }];
    equal((await f.qr()).status, 403);
  }
  for (const lifetime of [1n, 15000000n, 120000000n]) {
    f.store.tables.access_grants = [{
      ...grant,
      valid_until: stamp(NOW + lifetime),
    }];
    const response = await f.qr();
    equal(response.status, 201);
    equal(
      (await response.json()).expires_at,
      stamp(NOW + (lifetime < 60000000n ? lifetime : 60000000n)),
    );
    equal(f.store.tables.credentials.at(-1)!.grant_id, grant.id);
  }
  f.store.tables.access_grants = [grant];
  equal((await f.qr()).status, 201);
  f.store.tables.units[0].is_active = false;
  equal((await f.qr()).status, 404);
});

Deno.test("missing/invalid signing settings and failed persistence never return credentials", async () => {
  const f = fixture();
  for (const key of ["", "invalid PEM"]) {
    const handler = createHandler("user", f.store, { privateKey: key });
    equal(
      (await handler(
        new Request(`https://test/api/v1/houses/${UNIT}/qr-credentials`, {
          method: "POST",
          headers: { authorization: "Bearer user-token" },
        }),
      )).status,
      503,
    );
  }
  equal(f.store.tables.credentials, []);
  f.store.failWrite = true;
  equal((await f.qr()).status, 502);
  equal(f.store.tables.credentials, []);
  f.store.userError = new ApiError(504, "Could not reach Supabase.");
  equal((await f.qr()).status, 504);
});

Deno.test("Python UUIDv5 fixtures and an existing Python report remain retry-compatible", async () => {
  for (const item of golden.report_ids) {
    equal(await reportId(item.device_id, item.event_id), item.report_id);
  }
  const f = fixture();
  const command = await f.issue();
  const input = f.report(command);
  f.store.tables.access_events.push({
    id: golden.report_ids[0].report_id,
    device_id: DEVICE,
    event_type: "config_sync",
    metadata: {
      kind: "command_result",
      ...input,
      occurred_at: "2026-10-22T12:00:00Z",
    },
  });
  equal((await f.call("device", "/devices/front/events", input)).status, 200);
  equal(f.store.tables.access_events.length, 2);
  equal((await f.poll()).commands, []);
});

Deno.test("CORS preflight requires no bearer but data routes do", async () => {
  const f = fixture();
  const result = await f.user(
    new Request("https://test/api/v1/doors/front/commands", {
      method: "OPTIONS",
      headers: { origin: "http://localhost:8081" },
    }),
  );
  equal(result.status, 204);
  equal(result.headers.get("access-control-allow-origin"), "*");
  equal(
    (await f.call("user", "/devices/front/commands", undefined, "device-token"))
      .status,
    401,
  );
});

Deno.test("REST pagination, filters, user token and unique violation handling", async () => {
  const original = globalThis.fetch;
  const requests: Request[] = [];
  // deno-lint-ignore require-await
  globalThis.fetch = (async (url, init) => {
    const req = new Request(url, init);
    requests.push(req);
    equal(req.headers.get("apikey"), "sb_secret_test");
    if (req.method === "POST") {
      return Response.json({ code: "23505" }, { status: 409 });
    }
    if (new URL(req.url).pathname === "/auth/v1/user") {
      return Response.json({ id: USER });
    }
    const offset = new URL(req.url).searchParams.get("offset");
    return Response.json(
      offset === "0"
        ? Array.from({ length: 500 }, (_, id) => ({ id }))
        : offset === "500"
        ? [{ id: 500 }]
        : [],
    );
  }) as typeof fetch;
  try {
    const store = new RestStore("https://test", "sb_secret_test");
    const rows = await store.rows("access_events", {
      device_id: `eq.${DEVICE}`,
      "metadata->>kind": "eq.door_command",
    });
    equal(rows.length, 501);
    equal(new URL(requests[1].url).searchParams.get("offset"), "500");
    equal(
      new URL(requests[0].url).searchParams.get("metadata->>kind"),
      "eq.door_command",
    );
    equal(requests[0].headers.get("authorization"), null);
    await store.user("user-token");
    equal(requests.at(-1)!.headers.get("authorization"), "Bearer user-token");
    try {
      await store.insert("access_events", { id: "row" });
      throw new Error("Expected collision");
    } catch (error) {
      assert(error instanceof ApiError);
      equal(error.status, 409);
    }
  } finally {
    globalThis.fetch = original;
  }
});
