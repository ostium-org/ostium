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
      `Expected ${JSON.stringify(expected)}, received ${JSON.stringify(
        actual,
      )}`,
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
  Uint8Array.from(atob(text.replaceAll("-", "+").replaceAll("_", "/")), (c) =>
    c.charCodeAt(0),
  );

class Memory implements Store {
  roleName = "manager";
  active = true;
  missingProfile = false;
  failWrite = false;
  userError: Error | undefined;
  tables: Record<string, Row[]> = {
    profiles: [],
    devices: [
      {
        id: DEVICE,
        device_key: "front",
        status: "active",
        unit_id: UNIT,
        credential_hash: golden.device_hash,
      },
    ],
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
    const source =
      table === "profiles"
        ? this.missingProfile
          ? []
          : [
              { id: USER, role: this.roleName, is_active: this.active },
              ...this.tables.profiles.filter((p) => p.id !== USER),
            ]
        : this.tables[table];
    const result = structuredClone(
      source.filter((row) =>
        Object.entries(filters).every(([key, expected]) => {
          const value = key.includes("->>")
            ? row[key.split("->>")[0]]?.[key.split("->>")[1]]
            : row[key];
          return String(value) === expected.slice(3);
        }),
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
  async update(table: string, filters: Record<string, string>, row: Row) {
    await Promise.resolve();
    if (this.failWrite) throw new ApiError(502, "Write failed.");
    const matches = this.tables[table].filter((existing) =>
      Object.entries(filters).every(
        ([key, expected]) => String(existing[key]) === expected.slice(3),
      ),
    );
    if (!matches.length) throw new ApiError(404, "No matching row.");
    for (const target of matches) Object.assign(target, structuredClone(row));
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
    method?: string,
  ) => {
    const headers: Record<string, string> = {
      "content-type": "application/json",
    };
    if (token !== null) headers.authorization = `Bearer ${token}`;
    return (mode === "user" ? user : device)(
      new Request(
        `https://project.test/functions/v1/${mode}-api/api/v1${path}`,
        {
          method: method ?? (body === undefined ? "GET" : "POST"),
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

Deno.test(
  "user authorization and the existing admin role endpoint",
  async () => {
    const f = fixture();
    for (const token of [null, "invalid"]) {
      equal(
        (
          await f.call(
            "user",
            "/doors/front/commands",
            { action: "lock" },
            token,
          )
        ).status,
        401,
      );
    }
    f.store.roleName = "tenant";
    equal(
      (await f.call("user", "/doors/front/commands", { action: "lock" }))
        .status,
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
      (await f.call("user", "/doors/front/commands", { action: "lock" }))
        .status,
      403,
    );
    f.store.missingProfile = true;
    equal((await f.qr()).status, 403);
  },
);

Deno.test(
  "commands keep UUIDs, ten-second TTL, metadata and manager/admin access",
  async () => {
    const f = fixture();
    for (const role of ["manager", "admin"]) {
      f.store.roleName = role;
      const command = await f.issue("lock");
      assert(
        timestamp(command.expires_at) - timestamp(command.issued_at) ===
          10000000n,
      );
    }
  },
);

Deno.test("command validation and device statuses", async () => {
  const f = fixture();
  const command = await f.issue();
  equal(command.door_id, "front");
  equal(f.store.tables.access_events[0].id, command.command_id);
  equal(f.store.tables.access_events[0].metadata.kind, "door_command");
  assert(!("result" in f.store.tables.access_events[0]));
  for (const input of [{ action: "open" }, {}, { action: "lock", extra: true }])
    equal((await f.call("user", "/doors/front/commands", input)).status, 422);
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

Deno.test(
  "device credentials, device isolation and event ID scope",
  async () => {
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
      (await (await f.call("device", "/devices/back/commands")).json())
        .commands,
      [],
    );
    equal(
      (await f.call("device", "/devices/back/events", f.report(command)))
        .status,
      404,
    );
    const back = await (
      await f.call("user", "/doors/back/commands", { action: "unlock" })
    ).json();
    equal(
      (await f.call("device", "/devices/front/events", f.report(command)))
        .status,
      200,
    );
    equal(
      (await f.call("device", "/devices/back/events", f.report(back))).status,
      200,
    );
    equal(f.store.tables.access_events.length, 4);
  },
);

Deno.test(
  "polls repeat, sort by timestamp/ID and respect exact microsecond expiry",
  async () => {
    const f = fixture();
    const commands = [await f.issue(), await f.issue("lock")].sort((a, b) =>
      a.command_id < b.command_id ? -1 : 1,
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
  },
);

Deno.test(
  "done and rejected reports accept late delivery and timezone-equivalent retries",
  async () => {
    for (const result of ["done", "rejected"]) {
      const f = fixture();
      const command = await f.issue();
      f.clock.now = NOW + 86400000000n;
      equal(
        (
          await f.call(
            "device",
            "/devices/front/events",
            f.report(command, { result }),
          )
        ).status,
        200,
      );
      equal(
        (
          await f.call(
            "device",
            "/devices/front/events",
            f.report(command, {
              result,
              occurred_at: "2026-10-22T12:00:00.000000Z",
            }),
          )
        ).status,
        200,
      );
      f.clock.now = NOW;
      equal((await f.poll()).commands, []);
      equal(f.store.tables.access_events.length, 2);
    }
  },
);

Deno.test(
  "concurrent exact reports create only one row; conflicts return 409",
  async () => {
    const f = fixture();
    const command = await f.issue();
    const input = f.report(command);
    const responses = await Promise.all(
      Array.from({ length: 8 }, () =>
        f.call("device", "/devices/front/events", input),
      ),
    );
    equal(
      responses.map((r) => r.status),
      Array(8).fill(200),
    );
    equal(f.store.tables.access_events.length, 2);
    for (const extra of [
      { result: "rejected" },
      { reason: "jammed" },
      { action: "lock" },
      { command_id: crypto.randomUUID() },
      { occurred_at: "2026-10-22T12:00:01Z" },
    ]) {
      equal(
        (
          await f.call("device", "/devices/front/events", {
            ...input,
            ...extra,
          })
        ).status,
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
  },
);

Deno.test(
  "unknown commands, mismatched actions and invalid reports",
  async () => {
    const f = fixture();
    const command = await f.issue();
    equal(
      (
        await f.call(
          "device",
          "/devices/front/events",
          f.report(command, { command_id: crypto.randomUUID() }),
        )
      ).status,
      404,
    );
    equal(
      (
        await f.call(
          "device",
          "/devices/front/events",
          f.report(command, { action: "lock" }),
        )
      ).status,
      409,
    );
    for (const extra of [
      { result: "granted" },
      { reason: 42 },
      { event_id: "" },
      { event_id: "a".repeat(201) },
      { occurred_at: "2026-10-22T12:00:00" },
      { occurred_at: "2026-02-30T12:00:00Z" },
      { extra: true },
    ]) {
      equal(
        (
          await f.call(
            "device",
            "/devices/front/events",
            f.report(command, extra),
          )
        ).status,
        422,
      );
    }
    const missing: Row = f.report(command);
    delete missing.reason;
    equal(
      (await f.call("device", "/devices/front/events", missing)).status,
      422,
    );
  },
);

Deno.test(
  "QR tokens verify with the same public key and persist exact claims",
  async () => {
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
    equal(f.store.tables.credentials[0].payload, {
      claims,
      token: data.payload,
    });
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
  },
);

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
  for (const extra of [
    { valid_from: stamp(NOW + 1n) },
    { valid_until: stamp(NOW) },
    { is_active: false },
    { grantee_id: crypto.randomUUID() },
    { unit_id: crypto.randomUUID() },
  ]) {
    f.store.tables.access_grants = [{ ...grant, ...extra }];
    equal((await f.qr()).status, 403);
  }
  for (const lifetime of [1n, 15000000n, 120000000n]) {
    f.store.tables.access_grants = [
      {
        ...grant,
        valid_until: stamp(NOW + lifetime),
      },
    ];
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

Deno.test(
  "missing/invalid signing settings and failed persistence never return credentials",
  async () => {
    const f = fixture();
    for (const key of ["", "invalid PEM"]) {
      const handler = createHandler("user", f.store, { privateKey: key });
      equal(
        (
          await handler(
            new Request(`https://test/api/v1/houses/${UNIT}/qr-credentials`, {
              method: "POST",
              headers: { authorization: "Bearer user-token" },
            }),
          )
        ).status,
        503,
      );
    }
    equal(f.store.tables.credentials, []);
    f.store.failWrite = true;
    equal((await f.qr()).status, 502);
    equal(f.store.tables.credentials, []);
    f.store.userError = new ApiError(504, "Could not reach Supabase.");
    equal((await f.qr()).status, 504);
  },
);

Deno.test(
  "Python UUIDv5 fixtures and an existing Python report remain retry-compatible",
  async () => {
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
  },
);

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

Deno.test(
  "REST pagination, filters, user token and unique violation handling",
  async () => {
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
  },
);

Deno.test(
  "QR scan reports confirm the exact credential and survive delayed delivery",
  async () => {
    const f = fixture();
    const qr = await (await f.qr()).json();
    equal(qr.credential_id, f.store.tables.credentials[0].id);
    const path = `/houses/${UNIT}/qr-credentials/${qr.credential_id}/status`;
    equal(await (await f.call("user", path)).json(), {
      credential_id: qr.credential_id,
      house_id: UNIT,
      status: "pending",
      event: null,
    });
    const report = {
      event_id: "scan-1",
      credential_id: qr.credential_id,
      result: "granted",
      reason: null,
      occurred_at: stamp(NOW + 1000000n),
    };
    f.clock.now = NOW + 65000000n;
    const results = await Promise.all(
      Array.from({ length: 5 }, () =>
        f.call("device", "/devices/front/qr-events", report),
      ),
    );
    equal(
      results.map((r) => r.status),
      [200, 200, 200, 200, 200],
    );
    equal(f.store.tables.access_events.length, 1);
    const event = f.store.tables.access_events[0];
    equal(event.user_id, USER);
    equal(event.unit_id, UNIT);
    equal(event.credential_id, qr.credential_id);
    equal(await (await f.call("user", path)).json(), {
      credential_id: qr.credential_id,
      house_id: UNIT,
      status: "granted",
      event: { id: event.id, occurred_at: report.occurred_at },
    });
    for (const extra of [
      { result: "denied" },
      { reason: "changed" },
      {
        occurred_at: stamp(NOW + 2000000n),
      },
      { credential_id: crypto.randomUUID() },
    ]) {
      equal(
        (
          await f.call("device", "/devices/front/qr-events", {
            ...report,
            ...extra,
          })
        ).status,
        409,
      );
    }
    const other = await (await f.qr()).json();
    equal(
      (
        await (
          await f.call(
            "user",
            `/houses/${UNIT}/qr-credentials/${other.credential_id}/status`,
          )
        ).json()
      ).status,
      "pending",
    );
  },
);

Deno.test(
  "QR status enforces ownership for every role and requires user authentication",
  async () => {
    const f = fixture();
    const qr = await (await f.qr()).json();
    const path = `/houses/${UNIT}/qr-credentials/${qr.credential_id}/status`;
    equal((await f.call("user", path, undefined, null)).status, 401);
    equal((await f.call("user", path, undefined, "device-token")).status, 401);
    equal(
      (await f.call("user", path.replace(UNIT, crypto.randomUUID()))).status,
      404,
    );
    f.store.tables.credentials[0].issued_to = crypto.randomUUID();
    for (const role of ["tenant", "manager", "admin"]) {
      f.store.roleName = role;
      equal((await f.call("user", path)).status, 404);
    }
  },
);

Deno.test(
  "QR reports authenticate devices, isolate units, validate scan time and keep denial pending",
  async () => {
    const f = fixture();
    const qr = await (await f.qr()).json();
    const report = {
      event_id: "scan-1",
      credential_id: qr.credential_id,
      result: "granted",
      reason: null,
      occurred_at: stamp(NOW),
    };
    for (const token of [null, "wrong", "user-token"]) {
      equal(
        (await f.call("device", "/devices/front/qr-events", report, token))
          .status,
        401,
      );
    }
    for (const extra of [
      { result: "done" },
      { reason: 4 },
      { event_id: "" },
      { event_id: "a".repeat(201) },
      { credential_id: "bad" },
      { extra: true },
      { occurred_at: "2026-10-22T12:00:00" },
      { occurred_at: "2026-02-30T12:00:00Z" },
      { occurred_at: stamp(NOW - 1n) },
      { occurred_at: stamp(NOW + 1n) },
    ]) {
      equal(
        (
          await f.call("device", "/devices/front/qr-events", {
            ...report,
            ...extra,
          })
        ).status,
        422,
      );
    }
    equal((await f.call("device", "/devices/front/qr-events", {})).status, 422);
    equal(
      (
        await f.call("device", "/devices/front/qr-events", {
          ...report,
          credential_id: crypto.randomUUID(),
        })
      ).status,
      404,
    );
    f.store.tables.devices[0].unit_id = crypto.randomUUID();
    equal(
      (await f.call("device", "/devices/front/qr-events", report)).status,
      403,
    );
    f.store.tables.devices[0].unit_id = UNIT;
    f.store.tables.credentials[0].device_id = crypto.randomUUID();
    equal(
      (await f.call("device", "/devices/front/qr-events", report)).status,
      403,
    );
    f.store.tables.credentials[0].device_id = null;
    f.clock.now = NOW + 60000000n;
    equal(
      (
        await f.call("device", "/devices/front/qr-events", {
          ...report,
          occurred_at: stamp(f.clock.now),
        })
      ).status,
      422,
    );
    f.store.tables.credentials[0].is_revoked = true;
    f.store.tables.credentials[0].revoked_at = stamp(NOW);
    equal(
      (await f.call("device", "/devices/front/qr-events", report)).status,
      422,
    );
    equal(
      (
        await f.call("device", "/devices/front/qr-events", {
          ...report,
          result: "denied",
          reason: "revoked",
        })
      ).status,
      200,
    );
    equal(
      (
        await (
          await f.call(
            "user",
            `/houses/${UNIT}/qr-credentials/${qr.credential_id}/status`,
          )
        ).json()
      ).status,
      "pending",
    );
  },
);

Deno.test(
  "managers create, expire, activate and revoke tenant access grants",
  async () => {
    const f = fixture();
    const GUEST = crypto.randomUUID();
    f.store.tables.profiles.push({
      id: GUEST,
      role: "tenant",
      is_active: true,
    });

    // Tenants cannot manage grants.
    f.store.roleName = "tenant";
    equal(
      (
        await f.call("user", `/houses/${UNIT}/access-grants`, {
          grantee_id: GUEST,
        })
      ).status,
      403,
    );

    // Managers create a grant with an explicit window.
    f.store.roleName = "manager";
    const created = await f.call("user", `/houses/${UNIT}/access-grants`, {
      grantee_id: GUEST,
      grantee_role: "guest",
      valid_from: stamp(NOW),
      valid_until: stamp(NOW + 3600000000n),
      notes: "visit",
    });
    equal(created.status, 201);
    const grant = await created.json();
    equal(grant.unit_id, UNIT);
    equal(grant.grantee_id, GUEST);
    equal(grant.grantee_role, "guest");
    equal(grant.granted_by, USER);
    equal(grant.is_active, true);
    equal(grant.valid_until, stamp(NOW + 3600000000n));
    equal(f.store.tables.access_grants.length, 1);

    // Validation: unknown unit, unknown grantee, bad role, inverted window.
    equal(
      (
        await f.call("user", `/houses/${crypto.randomUUID()}/access-grants`, {
          grantee_id: GUEST,
        })
      ).status,
      404,
    );
    equal(
      (
        await f.call("user", `/houses/${UNIT}/access-grants`, {
          grantee_id: crypto.randomUUID(),
        })
      ).status,
      404,
    );
    equal(
      (
        await f.call("user", `/houses/${UNIT}/access-grants`, {
          grantee_id: GUEST,
          grantee_role: "admin",
        })
      ).status,
      422,
    );
    equal(
      (
        await f.call("user", `/houses/${UNIT}/access-grants`, {
          grantee_id: GUEST,
          valid_from: stamp(NOW + 1000000000n),
          valid_until: stamp(NOW),
        })
      ).status,
      422,
    );
    equal(
      (
        await f.call("user", `/houses/${UNIT}/access-grants`, {
          grantee_id: GUEST,
          extra: true,
        })
      ).status,
      422,
    );

    // Expire sets valid_until to now and deactivates.
    f.clock.now = NOW + 500000000n;
    const expired = await f.call(
      "user",
      `/houses/${UNIT}/access-grants/${grant.id}`,
      { action: "expire" },
      null,
      "PATCH",
    );
    equal(expired.status, 200);
    equal((await expired.json()).is_active, false);
    equal((await expired.json()).valid_until, stamp(NOW + 500000000n));

    // Activate re-enables the grant.
    const activated = await f.call(
      "user",
      `/houses/${UNIT}/access-grants/${grant.id}`,
      { action: "activate" },
      null,
      "PATCH",
    );
    equal(activated.status, 200);
    equal((await activated.json()).is_active, true);

    // Revoke deactivates without touching the window.
    const revoked = await f.call(
      "user",
      `/houses/${UNIT}/access-grants/${grant.id}`,
      { action: "revoke" },
      null,
      "PATCH",
    );
    equal(revoked.status, 200);
    equal((await revoked.json()).is_active, false);

    // Unknown grant, unknown unit, and invalid action are rejected.
    equal(
      (
        await f.call(
          "user",
          `/houses/${UNIT}/access-grants/${crypto.randomUUID()}`,
          { action: "revoke" },
          null,
          "PATCH",
        )
      ).status,
      404,
    );
    equal(
      (
        await f.call(
          "user",
          `/houses/${crypto.randomUUID()}/access-grants/${grant.id}`,
          { action: "revoke" },
          null,
          "PATCH",
        )
      ).status,
      404,
    );
    equal(
      (
        await f.call(
          "user",
          `/houses/${UNIT}/access-grants/${grant.id}`,
          { action: "delete" },
          null,
          "PATCH",
        )
      ).status,
      422,
    );
  },
);

Deno.test(
  "a revoked grant cannot be used to issue new QR credentials",
  async () => {
    const f = fixture();
    f.store.roleName = "tenant";
    const grant = {
      id: crypto.randomUUID(),
      unit_id: UNIT,
      grantee_id: USER,
      is_active: true,
      valid_from: stamp(NOW),
      valid_until: null as string | null,
    };
    f.store.tables.access_grants = [{ ...grant }];
    equal((await f.qr()).status, 201);

    // Manager revokes the grant; the tenant can no longer mint credentials.
    f.store.roleName = "manager";
    await f.call(
      "user",
      `/houses/${UNIT}/access-grants/${grant.id}`,
      { action: "revoke" },
      null,
      "PATCH",
    );
    f.store.roleName = "tenant";
    equal((await f.qr()).status, 403);
  },
);

Deno.test(
  "authenticated device requests refresh last_seen_at and status",
  async () => {
    const f = fixture();
    const command = await f.issue();
    const before = f.store.tables.devices[0];
    assert(before.last_seen_at === undefined || before.last_seen_at === null);

    // An offline device authenticating is promoted to active and heartbeated.
    f.store.tables.devices[0].status = "offline";
    f.clock.now = NOW + 123456789n;
    equal((await f.call("device", "/devices/front/commands")).status, 200);
    equal(f.store.tables.devices[0].status, "active");
    equal(f.store.tables.devices[0].last_seen_at, stamp(NOW + 123456789n));

    // A disabled device is rejected and never heartbeated.
    f.store.tables.devices[0].status = "disabled";
    f.store.tables.devices[0].last_seen_at = null;
    equal((await f.call("device", "/devices/front/commands")).status, 401);
    equal(f.store.tables.devices[0].last_seen_at, null);
    equal(
      (await f.call("device", "/devices/front/events", f.report(command)))
        .status,
      401,
    );
  },
);

Deno.test(
  "devices sync temperature readings as idempotent events",
  async () => {
    const f = fixture();
    const report = {
      event_id: "temp-1",
      temperature_c: 21.5,
      occurred_at: stamp(NOW),
    };

    // First report and an exact retry both succeed, creating one row.
    equal(
      (await f.call("device", "/devices/front/temperature", report)).status,
      200,
    );
    equal(
      (await f.call("device", "/devices/front/temperature", report)).status,
      200,
    );
    equal(f.store.tables.access_events.length, 1);
    const event = f.store.tables.access_events[0];
    equal(event.device_id, DEVICE);
    equal(event.unit_id, UNIT);
    equal(event.event_type, "temperature_sync");
    equal(event.metadata.kind, "temperature_sync");
    equal(event.metadata.temperature_c, 21.5);
    equal(event.occurred_at, stamp(NOW));

    // Reusing the event ID with a different reading is a collision.
    equal(
      (
        await f.call("device", "/devices/front/temperature", {
          ...report,
          temperature_c: 22.0,
        })
      ).status,
      409,
    );

    // Validation: non-finite, out-of-range, and malformed readings are rejected.
    for (const extra of [
      { temperature_c: "21" },
      { temperature_c: NaN },
      { temperature_c: -41 },
      { temperature_c: 126 },
      { event_id: "" },
      { occurred_at: "2026-10-22T12:00:00" },
      { extra: true },
    ]) {
      equal(
        (
          await f.call("device", "/devices/front/temperature", {
            ...report,
            ...extra,
          })
        ).status,
        422,
      );
    }

    // A disabled device cannot sync temperature.
    f.store.tables.devices[0].status = "disabled";
    equal(
      (await f.call("device", "/devices/front/temperature", report)).status,
      401,
    );
  },
);
