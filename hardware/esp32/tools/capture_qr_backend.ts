// Records real responses from the Deno QR and device routes for the firmware
// tests (SCRUM-38).
//
// Runs createHandler from backend/supabase/functions/_shared/api.ts in process,
// against an in-memory store with the same filter rules as Memory in
// backend/supabase/tests/api_test.ts. It issues QR credentials, polls, and
// posts qr-events bodies in the exact format ostium::serializeQrReport writes,
// and records each status and body in
// hardware/esp32/test/test_core/qr_backend_responses.h. It also writes the
// startup self-test vector, hardware/esp32/lib/ostium_core/src/qr_test_vector.h,
// from backend/supabase/tests/python_compatibility.json.
//
// It records two sets. The first runs on a microsecond clock, so every time
// has a fraction of a millisecond. The second, the production-clock set, runs
// on createHandler's default clock, BigInt(Date.now()) * 1000n, as device-api
// and ostium-api do, so its times are whole milliseconds like real tokens.
// Date.now is pinned while that set runs.
//
// The clocks and credential IDs are fixed, and Ed25519 signatures are
// deterministic, so the output changes only when the backend or the fixture
// changes. Each call checks its expected status, and the tool exits 1 without
// writing anything if any status differs. Run it from the repository root
// after any change to the QR routes, the device routes, or the fixture:
//
//   deno run --check --allow-read --allow-write=hardware/esp32/test/test_core,hardware/esp32/lib/ostium_core/src hardware/esp32/tools/capture_qr_backend.ts
//
// With --check, it writes nothing, and exits 1 if either file on disk differs
// from what it would write. CI runs it that way:
//
//   deno run --check --allow-read hardware/esp32/tools/capture_qr_backend.ts --check

import {
  ApiError,
  createHandler,
  type Row,
  stamp,
  type Store,
  timestamp,
} from "../../../backend/supabase/functions/_shared/api.ts";
import golden from "../../../backend/supabase/tests/python_compatibility.json" with {
  type: "json",
};

const USER = "11111111-1111-4111-8111-111111111111";
const UNIT = "22222222-2222-4222-8222-222222222222";
const DEVICE = "33333333-3333-4333-8333-333333333333";
const OTHER_UNIT = "44444444-4444-4444-8444-444444444444";
const UNKNOWN_CREDENTIAL = "99999999-9999-4999-8999-999999999999";
const GRANT = "b0000000-0000-4000-8000-000000000001";
const DEVICE_KEY = "front";
const DEVICE_TOKEN = "device-token";
const USER_TOKEN = "user-token";
const T0 = timestamp("2026-10-22T12:00:00.123456Z");
const SECOND = 1000000n; // in microseconds, like the backend's clock
// The tenant's grant ends 20.5 s after T0, which caps kQrCappedIssued's expiry.
const GRANT_ENDS = T0 + 20500000n;
// When the production-clock set issues its first credential. ArduinoJson reads
// this iat, 1792670400.123, as a little more than the exact value, and the
// second credential's, 2 ms later, exactly, so the set covers both results of
// Q6's ceil. The tool checks that.
const PROD_T0 = timestamp("2026-10-22T12:00:00.123Z");
const PROD_SECOND_AFTER = 2000n; // µs between the two credentials
const MILLISECOND = 1000n; // in microseconds
// An Ed25519 PKCS8 private key is this 16-byte DER prefix, then the 32-byte seed.
const PKCS8_ED25519_PREFIX = "302e020100300506032b657004220420";

const RESPONSES_PATH = "hardware/esp32/test/test_core/qr_backend_responses.h";
const VECTOR_PATH = "hardware/esp32/lib/ostium_core/src/qr_test_vector.h";
const RESPONSES = new URL(
  "../test/test_core/qr_backend_responses.h",
  import.meta.url,
);
const VECTOR = new URL(
  "../lib/ostium_core/src/qr_test_vector.h",
  import.meta.url,
);
const WRITE_COMMAND = "deno run --check --allow-read " +
  "--allow-write=hardware/esp32/test/test_core,hardware/esp32/lib/ostium_core/src " +
  "hardware/esp32/tools/capture_qr_backend.ts";

const encoder = new TextEncoder();
const decoder = new TextDecoder("utf-8", { fatal: true });

interface Capture {
  status: number;
  body: string; // the response body
  request: string; // the request body, or an empty string if it had none
}

// The rows the routes read and write. rows() and insert() follow Memory in
// backend/supabase/tests/api_test.ts.
class Memory implements Store {
  roleName = "manager";
  tables: Record<string, Row[]> = {
    devices: [{
      id: DEVICE,
      device_key: DEVICE_KEY,
      status: "active",
      unit_id: UNIT,
      credential_hash: golden.device_hash,
    }],
    units: [{ id: UNIT, is_active: true }, { id: OTHER_UNIT, is_active: true }],
    access_grants: [{
      id: GRANT,
      unit_id: UNIT,
      grantee_id: USER,
      is_active: true,
      valid_from: stamp(T0 - 3600n * SECOND),
      valid_until: stamp(GRANT_ENDS),
    }],
    credentials: [],
    access_events: [],
  };

  user(token: string): Promise<Row> {
    if (token !== USER_TOKEN) {
      return Promise.reject(new ApiError(401, "Invalid token."));
    }
    return Promise.resolve({ id: USER });
  }

  role(): Promise<void> {
    return Promise.resolve();
  }

  rows(table: string, filters: Record<string, string>): Promise<Row[]> {
    const source = table === "profiles"
      ? [{ id: USER, role: this.roleName, is_active: true }]
      : this.tables[table];
    if (!source) throw new Error(`unknown table ${table}`);
    return Promise.resolve(
      structuredClone(
        source.filter((row) =>
          Object.entries(filters).every(([key, expected]) => {
            if (!expected.startsWith("eq.")) {
              throw new Error(`unsupported filter ${key}=${expected}`);
            }
            const [column, field] = key.split("->>");
            const value = field === undefined
              ? row[column]
              : row[column]?.[field];
            return String(value) === expected.slice(3);
          })
        ),
      ),
    );
  }

  insert(table: string, row: Row): Promise<void> {
    if (this.tables[table].some((item) => item.id === row.id)) {
      return Promise.reject(new ApiError(409, "Duplicate."));
    }
    this.tables[table].push(structuredClone(row));
    return Promise.resolve();
  }
}

// The credential ID the backend gives the nth credential it issues here.
function credentialId(n: number): string {
  return `a0000000-0000-4000-8000-${String(n).padStart(12, "0")}`;
}

function hex(bytes: Uint8Array): string {
  return Array.from(bytes, (b) => b.toString(16).padStart(2, "0")).join("");
}

function fromBase64Url(text: string): Uint8Array<ArrayBuffer> {
  if (!/^[A-Za-z0-9_-]*$/.test(text)) {
    throw new Error(`not base64url: ${text}`);
  }
  return Uint8Array.from(
    atob(text.replaceAll("-", "+").replaceAll("_", "/")),
    (c) => c.charCodeAt(0),
  );
}

// Formats epoch milliseconds the way ostium::formatTimestamp does.
function fw(ms: bigint): string {
  return new Date(Number(ms)).toISOString();
}

// The occurred_at the door writes for a scan as the poll arrives, with a
// round-trip time of 0: the poll's server_time truncated to ms, minus 500 ms.
function scanTimeAt(poll: Capture): string {
  const serverTime = timestamp(JSON.parse(poll.body).server_time);
  return fw(serverTime / 1000n - 500n);
}

// A qr-events body in the exact format ostium::serializeQrReport writes.
// JSON.stringify gives the same bytes as ArduinoJson's compact output.
function firmwareReport(
  eventId: string,
  credentialId: string,
  result: "granted" | "denied",
  reason: string | null,
  occurredAt: string,
): string {
  return JSON.stringify({
    event_id: eventId,
    credential_id: credentialId,
    result,
    reason,
    occurred_at: occurredAt,
  });
}

// The parts of a compact JWS that the firmware reads, checked against the
// fixture's public key.
interface TokenFacts {
  keyId: string;
  credentialId: string;
  unitId: string;
  iatMs: bigint; // Q6: ceil(iat x 1000) + 1
  expMs: bigint; // Q6: floor(exp x 1000) - 1
}

// Which backend clock signed a token: a fraction of a millisecond in every
// time, or whole milliseconds, as in production.
type ClockShape = "microsecond" | "millisecond";

// Rounds a claim's decimal text to ms exactly. For a time on an exact
// millisecond, floor and ceil are that millisecond.
function claimMs(
  name: string,
  text: string,
): { floor: bigint; ceil: bigint; wholeMs: boolean } {
  const match = /^(\d+)(?:\.(\d+))?$/.exec(text);
  if (!match) throw new Error(`${name} isn't a plain decimal number: ${text}`);
  const fraction = match[2] ?? "";
  const scale = 10n ** BigInt(fraction.length);
  const thousandths = BigInt(match[1] + fraction) * 1000n;
  const floor = thousandths / scale;
  const wholeMs = thousandths % scale === 0n;
  return { floor, ceil: wholeMs ? floor : floor + 1n, wholeMs };
}

// A double from its IEEE 754 bits.
function float64(bits: bigint): number {
  const view = new DataView(new ArrayBuffer(8));
  view.setBigUint64(0, bits);
  return view.getFloat64(0);
}

// The ArduinoJson release whose parseNumber firmwareSeconds copies. The three
// build files pin exactly this release, and the tool checks that they do.
// qr_backend_responses.h names it, so the replay tests don't build against
// another. To upgrade, check FloatTraits.hpp and parseNumber in the new
// release, update this copy and the pins, and run the tool again.
const ARDUINOJSON_VERSION = "7.4.3";
const ARDUINOJSON_PINS = [
  "../platformio.ini",
  "../native-tests/platformio.ini",
  "../lib/ostium_core/library.json",
];

// The factors that ArduinoJson 7.4.3's make_float multiplies by, one for each
// set bit of a negative exponent: 1e-1, 1e-2, 1e-4, and 1e-8, from the bit
// patterns in ArduinoJson/Numbers/FloatTraits.hpp.
const NEGATIVE_POWERS_OF_TEN = [
  0x3FB999999999999An,
  0x3F847AE147AE147Bn,
  0x3F1A36E2EB1C432Dn,
  0x3E45798EE2308C3An,
].map(float64);
const MANTISSA_MAX = (1n << 52n) - 1n; // FloatTraits<double>::mantissa_max

// The double that verifyQrToken reads for a plain decimal claim, following
// ArduinoJson 7.4.3's parseNumber: the digits as an integer mantissa, times one
// factor for each set bit of the number of fraction digits it kept. That isn't
// always the nearest double, so Number(text) can't stand in for it. For
// 1792670400.123, ArduinoJson gives 1792670400.12300015, one double above the
// nearest, 1792670400.12299991. Times 1000, the nearest double gives exactly
// 1792670400123, and ArduinoJson's gives 1792670400123.000244, so Q6 makes
// iatMs ...125, not ...124.
function firmwareSeconds(name: string, text: string): number {
  const match = /^(\d+)(?:\.(\d+))?$/.exec(text);
  if (!match) throw new Error(`${name} isn't a plain decimal number: ${text}`);
  let mantissa = BigInt(match[1]);
  if (match[2] === undefined) return Number(mantissa); // read as an integer
  // A mantissa of 2^23 or more makes parseNumber use a double. A smaller one
  // uses a float, but no epoch time from 2020 to 2100 is that small.
  if (mantissa > MANTISSA_MAX || mantissa < 1n << 23n) {
    throw new Error(`${name} ${text} is outside the range the tool handles`);
  }
  let exponent = 0;
  for (const digit of match[2]) {
    if (mantissa < MANTISSA_MAX / 10n) {
      mantissa = mantissa * 10n + BigInt(digit);
      exponent++;
    }
  }
  let value = Number(mantissa); // exact, because mantissa < 2^52
  for (let bit = 0; exponent !== 0; bit++, exponent >>= 1) {
    if (bit >= NEGATIVE_POWERS_OF_TEN.length) {
      throw new Error(`${name} ${text} has too many fraction digits`);
    }
    if (exponent & 1) value *= NEGATIVE_POWERS_OF_TEN[bit];
  }
  return value;
}

// Rounds a claim to ms the way the firmware does, in IEEE double arithmetic:
// issuedAtMsFromSeconds and expiresAtMsFromSeconds take ceil(x * 1000.0) and
// floor(x * 1000.0) of the parsed double. A microsecond claim must not fall on
// an exact millisecond, and must give the exact rounding too. A millisecond
// claim must fall on one, where the firmware's ceil can give that millisecond
// or the next.
function q6(
  name: string,
  text: string,
  shape: ClockShape,
): { floor: bigint; ceil: bigint } {
  const exact = claimMs(name, text);
  const product = firmwareSeconds(name, text) * 1000;
  const firmware = {
    floor: BigInt(Math.floor(product)),
    ceil: BigInt(Math.ceil(product)),
  };
  if (shape === "microsecond") {
    if (exact.wholeMs) {
      throw new Error(`${name} ${text} falls on an exact millisecond`);
    }
    if (firmware.floor !== exact.floor || firmware.ceil !== exact.ceil) {
      throw new Error(`${name} ${text}: the firmware's rounding isn't exact`);
    }
  } else if (!exact.wholeMs) {
    throw new Error(`${name} ${text} isn't a whole number of milliseconds`);
  }
  return firmware;
}

async function tokenFacts(
  token: string,
  publicKey: CryptoKey,
  shape: ClockShape = "microsecond",
): Promise<TokenFacts> {
  if (!/^[A-Za-z0-9_-]+\.[A-Za-z0-9_-]+\.[A-Za-z0-9_-]+$/.test(token)) {
    throw new Error(`not a compact JWS: ${token}`);
  }
  const [header, payload, signature] = token.split(".");
  const valid = await crypto.subtle.verify(
    "Ed25519",
    publicKey,
    fromBase64Url(signature),
    encoder.encode(`${header}.${payload}`),
  );
  if (!valid) {
    throw new Error("a token doesn't verify with the fixture's public key");
  }
  const headerJson = JSON.parse(decoder.decode(fromBase64Url(header)));
  const claimsText = decoder.decode(fromBase64Url(payload));
  const claims = JSON.parse(claimsText);
  const number = (name: string) => {
    const found = [
      ...claimsText.matchAll(new RegExp(`"${name}":([^,}]*)`, "g")),
    ];
    if (found.length !== 1) throw new Error(`expected one ${name} claim`);
    return found[0][1];
  };
  return {
    keyId: headerJson.kid,
    credentialId: claims.jti,
    unitId: claims.unit_id,
    iatMs: q6("iat", number("iat"), shape).ceil + 1n,
    expMs: q6("exp", number("exp"), shape).floor - 1n,
  };
}

// The fixture's TEST ONLY key: its seed, and its public key, checked against
// the fixture's public_key.
async function fixtureKey(): Promise<
  { seedHex: string; publicKey: CryptoKey }
> {
  const der = Uint8Array.from(
    atob(golden.private_key.replace(/-----[A-Z ]+-----|\s/g, "")),
    (c) => c.charCodeAt(0),
  );
  if (der.length !== 48 || hex(der.subarray(0, 16)) !== PKCS8_ED25519_PREFIX) {
    throw new Error("the fixture's private key isn't a bare Ed25519 PKCS8 key");
  }
  const privateKey = await crypto.subtle.importKey(
    "pkcs8",
    der,
    "Ed25519",
    true,
    ["sign"],
  );
  const jwk = await crypto.subtle.exportKey("jwk", privateKey);
  if (jwk.x !== golden.public_key) {
    throw new Error("the fixture's public_key doesn't match its private_key");
  }
  const publicKey = await crypto.subtle.importKey(
    "raw",
    fromBase64Url(golden.public_key),
    "Ed25519",
    false,
    ["verify"],
  );
  return { seedHex: hex(der.subarray(16)), publicKey };
}

type Handler = ReturnType<typeof createHandler>;

// Sends one request at backend time `at`, in µs, and records it under name.
// Fails unless the status is `expect`.
type Call = (
  name: string,
  expect: number,
  at: bigint,
  method: "GET" | "POST",
  path: string,
  body?: string,
  token?: string,
) => Promise<Capture>;

// A Call to these handlers. setClock moves the backend's clock to `at`.
function recorder(
  user: Handler,
  device: Handler,
  setClock: (at: bigint) => void,
  captures: Map<string, Capture>,
): Call {
  return async (
    name,
    expect,
    at,
    method,
    path,
    body,
    token = path.startsWith("/devices/") ? DEVICE_TOKEN : USER_TOKEN,
  ) => {
    setClock(at);
    const mode = path.startsWith("/devices/") ? "device" : "user";
    const headers: Record<string, string> = {
      authorization: `Bearer ${token}`,
    };
    if (body !== undefined) headers["content-type"] = "application/json";
    const response = await (mode === "user" ? user : device)(
      new Request(
        `https://project.test/functions/v1/${mode}-api/api/v1${path}`,
        {
          method,
          headers,
          body,
        },
      ),
    );
    const result = {
      status: response.status,
      body: await response.text(),
      request: body ?? "",
    };
    if (result.status !== expect) {
      throw new Error(
        `${name}: expected ${expect}, got ${result.status}: ${result.body}`,
      );
    }
    captures.set(name, result);
    return result;
  };
}

// Runs `run` while crypto.randomUUID gives the fixed IDs credentialId(first),
// credentialId(first + 1), and so on.
async function withCredentialIds<T>(
  first: number,
  run: () => Promise<T>,
): Promise<T> {
  let next = first;
  Object.defineProperty(globalThis.crypto, "randomUUID", {
    value: () => credentialId(next++),
    configurable: true,
  });
  try {
    return await run();
  } finally {
    Reflect.deleteProperty(globalThis.crypto, "randomUUID");
  }
}

const QR_EVENTS = `/devices/${DEVICE_KEY}/qr-events`;
const COMMANDS = `/devices/${DEVICE_KEY}/commands`;

interface Captured {
  captures: Map<string, Capture>; // by C++ constant name, in call order
  issuedToken: string;
  otherUnitToken: string;
  cappedToken: string;
}

async function capture(): Promise<Captured> {
  const store = new Memory();
  const clock = { now: T0 };
  const settings = { privateKey: golden.private_key };
  const user = createHandler("user", store, settings, () => clock.now);
  const device = createHandler("device", store, settings, () => clock.now);
  const captures = new Map<string, Capture>();
  const call = recorder(user, device, (at) => {
    clock.now = at;
  }, captures);

  // Issues a credential at T0 and returns its token.
  const issue = async (name: string, unit: string): Promise<string> => {
    const path = `/houses/${unit}/qr-credentials`;
    return JSON.parse((await call(name, 201, T0, "POST", path)).body).payload;
  };
  const [issuedToken, otherUnitToken, cappedToken] = await withCredentialIds(
    1,
    async () => {
      const issuedToken = await issue("kQrIssued", UNIT);
      const otherUnitToken = await issue("kQrOtherUnitIssued", OTHER_UNIT);
      // A tenant's credential ends with the tenant's grant.
      store.roleName = "tenant";
      const cappedToken = await issue("kQrCappedIssued", UNIT);
      store.roleName = "manager";
      return [issuedToken, otherUnitToken, cappedToken];
    },
  );

  const poll = await call("kQrPoll", 200, T0 + SECOND, "GET", COMMANDS);
  const granted = firmwareReport(
    `qr-${credentialId(1)}-granted`,
    credentialId(1),
    "granted",
    null,
    scanTimeAt(poll),
  );
  await call("kQrGranted", 200, T0 + 2n * SECOND, "POST", QR_EVENTS, granted);
  await call(
    "kQrGrantedRetry",
    200,
    T0 + 2n * SECOND,
    "POST",
    QR_EVENTS,
    granted,
  );
  const later = fw(timestamp(scanTimeAt(poll)) / 1000n + 1n);
  await call(
    "kQrConflict",
    409,
    T0 + 2n * SECOND,
    "POST",
    QR_EVENTS,
    firmwareReport(
      `qr-${credentialId(1)}-granted`,
      credentialId(1),
      "granted",
      null,
      later,
    ),
  );

  const pollLate = await call(
    "kQrPollLate",
    200,
    T0 + 21n * SECOND,
    "GET",
    COMMANDS,
  );
  await call(
    "kQrDeniedExpired",
    200,
    T0 + 22n * SECOND,
    "POST",
    QR_EVENTS,
    firmwareReport(
      `qr-${credentialId(3)}-denied`,
      credentialId(3),
      "denied",
      "expired",
      scanTimeAt(pollLate),
    ),
  );

  // Grants just outside each credential's window, and one in the backend's
  // future. The door never writes these.
  await call(
    "kQrGrantedBeforeIssue",
    422,
    T0 + 3n * SECOND,
    "POST",
    QR_EVENTS,
    firmwareReport(
      "qr-boundary-1",
      credentialId(1),
      "granted",
      null,
      fw(T0 / 1000n),
    ),
  );
  await call(
    "kQrGrantedAfterExpiry",
    422,
    T0 + 22n * SECOND,
    "POST",
    QR_EVENTS,
    firmwareReport(
      "qr-boundary-2",
      credentialId(3),
      "granted",
      null,
      fw((GRANT_ENDS + 999n) / 1000n),
    ),
  );
  await call(
    "kQrGrantedInFuture",
    422,
    T0 + 3n * SECOND,
    "POST",
    QR_EVENTS,
    firmwareReport(
      "qr-boundary-3",
      credentialId(1),
      "granted",
      null,
      fw((T0 + 3n * SECOND) / 1000n + 1n),
    ),
  );

  await call(
    "kQrUnknownCredential",
    404,
    T0 + 22n * SECOND,
    "POST",
    QR_EVENTS,
    firmwareReport(
      `qr-${UNKNOWN_CREDENTIAL}-denied`,
      UNKNOWN_CREDENTIAL,
      "denied",
      "expired",
      scanTimeAt(pollLate),
    ),
  );
  await call(
    "kQrOtherUnitReport",
    403,
    T0 + 22n * SECOND,
    "POST",
    QR_EVENTS,
    firmwareReport(
      `qr-${credentialId(2)}-denied`,
      credentialId(2),
      "denied",
      "expired",
      scanTimeAt(pollLate),
    ),
  );
  await call(
    "kQrBadToken",
    401,
    T0 + 22n * SECOND,
    "POST",
    QR_EVENTS,
    granted,
    "wrong-token",
  );

  return { captures, issuedToken, otherUnitToken, cappedToken };
}

interface ProductionCaptured {
  captures: Map<string, Capture>; // by C++ constant name, in call order
  token: string;
  secondToken: string;
  facts: TokenFacts;
  secondFacts: TokenFacts;
  issuedAt: string; // the stored credential's issued_at
  secondIssuedAt: string;
}

// Records the production-clock set on its own store. The handlers get no
// clock, so they use createHandler's default, BigInt(Date.now()) * 1000n,
// with Date.now pinned to each call's time. The door in the replay tests
// syncs to a recorded poll with a 0 ms round trip and scans as the poll
// arrives, so its bounds are [S, S + 1 ms] and it writes occurred_at S - 500.
async function captureProduction(
  publicKey: CryptoKey,
): Promise<ProductionCaptured> {
  const store = new Memory();
  const settings = { privateKey: golden.private_key };
  const user = createHandler("user", store, settings);
  const device = createHandler("device", store, settings);
  const captures = new Map<string, Capture>();
  let nowMs = 0;
  const call = recorder(user, device, (at) => {
    if (at % MILLISECOND !== 0n) {
      throw new Error(`${stamp(at)} isn't a whole millisecond`);
    }
    nowMs = Number(at / MILLISECOND);
  }, captures);
  const ms = (text: string) => timestamp(text) / MILLISECOND;

  const realNow = Date.now;
  Date.now = () => nowMs;
  try {
    // Issues a manager's credential, which no grant caps, at `at`.
    const issue = async (name: string, at: bigint) => {
      const path = `/houses/${UNIT}/qr-credentials`;
      const issued = JSON.parse((await call(name, 201, at, "POST", path)).body);
      const row = store.tables.credentials.find((r) =>
        r.id === issued.credential_id
      );
      if (!row || row.expires_at !== issued.expires_at) {
        throw new Error(`${name}: the stored credential doesn't match`);
      }
      const token: string = issued.payload;
      return {
        token,
        facts: await tokenFacts(token, publicKey, "millisecond"),
        issuedAt: String(row.issued_at),
        expiresAt: String(issued.expires_at),
      };
    };
    const [first, second] = await withCredentialIds(4, async () => [
      await issue("kQrProdIssued", PROD_T0),
      await issue("kQrProdSecondIssued", PROD_T0 + PROD_SECOND_AFTER),
    ]);
    if (
      first.facts.iatMs !== ms(first.issuedAt) + 2n ||
      second.facts.iatMs !== ms(second.issuedAt) + 1n
    ) {
      throw new Error("the production-clock set must cover both ceil results");
    }

    // The door's earliest grant. A poll at iatMs + 500 ms puts occurred_at at
    // iatMs, the earliest that Q16 allows.
    const poll = await call(
      "kQrProdPoll",
      200,
      (first.facts.iatMs + 500n) * MILLISECOND,
      "GET",
      COMMANDS,
    );
    if (scanTimeAt(poll) !== fw(first.facts.iatMs)) {
      throw new Error("kQrProdPoll doesn't put occurred_at at iatMs");
    }
    const reportAt = timestamp(JSON.parse(poll.body).server_time) + SECOND;
    await call(
      "kQrProdGranted",
      200,
      reportAt,
      "POST",
      QR_EVENTS,
      firmwareReport(
        `qr-${first.facts.credentialId}-granted`,
        first.facts.credentialId,
        "granted",
        null,
        scanTimeAt(poll),
      ),
    );
    // 1 ms before the stored issued_at. The door never writes it.
    await call(
      "kQrProdGrantedBeforeIssue",
      422,
      reportAt,
      "POST",
      QR_EVENTS,
      firmwareReport(
        "qr-boundary-4",
        first.facts.credentialId,
        "granted",
        null,
        fw(ms(first.issuedAt) - 1n),
      ),
    );
    // The second token's earliest grant. Its iatMs is 1 ms after issued_at,
    // the closest any door grant gets to it, where the first token's is 2 ms
    // after. kQrProdGrantedLatest reports the second token's grant under the
    // door's event ID, so this one has its own.
    await call(
      "kQrProdSecondGrantedEarliest",
      200,
      reportAt,
      "POST",
      QR_EVENTS,
      firmwareReport(
        "qr-boundary-6",
        second.facts.credentialId,
        "granted",
        null,
        fw(second.facts.iatMs),
      ),
    );

    // The door's latest grant. A poll at expMs - 2 ms puts latest at
    // expMs - 1, the latest that Q15 allows, and occurred_at at expMs - 502.
    const pollLate = await call(
      "kQrProdPollLate",
      200,
      (second.facts.expMs - 2n) * MILLISECOND,
      "GET",
      COMMANDS,
    );
    const lateReportAt = timestamp(JSON.parse(pollLate.body).server_time) +
      SECOND;
    await call(
      "kQrProdGrantedLatest",
      200,
      lateReportAt,
      "POST",
      QR_EVENTS,
      firmwareReport(
        `qr-${second.facts.credentialId}-granted`,
        second.facts.credentialId,
        "granted",
        null,
        scanTimeAt(pollLate),
      ),
    );
    // At the backend's expires_at. The report arrives after expires_at, so
    // the window check answers, not the future check. The door never writes
    // it.
    await call(
      "kQrProdGrantedAtExpiry",
      422,
      lateReportAt,
      "POST",
      QR_EVENTS,
      firmwareReport(
        "qr-boundary-5",
        second.facts.credentialId,
        "granted",
        null,
        fw(ms(second.expiresAt)),
      ),
    );

    return {
      captures,
      token: first.token,
      secondToken: second.token,
      facts: first.facts,
      secondFacts: second.facts,
      issuedAt: first.issuedAt,
      secondIssuedAt: second.issuedAt,
    };
  } finally {
    Date.now = realNow;
  }
}

// Fails unless every ArduinoJson dependency in the build files is exactly
// ARDUINOJSON_VERSION. A range would let a build pick up another release, whose
// parse the tool hasn't copied.
async function checkArduinoJsonPins(): Promise<void> {
  for (const path of ARDUINOJSON_PINS) {
    const text = await Deno.readTextFile(new URL(path, import.meta.url));
    const specs = path.endsWith(".json")
      ? [JSON.parse(text).dependencies?.["bblanchon/ArduinoJson"]]
      : [...text.matchAll(/bblanchon\/ArduinoJson\s*@\s*(\S+)/g)].map((m) =>
        m[1]
      );
    if (!specs.length || specs.some((spec) => spec !== ARDUINOJSON_VERSION)) {
      throw new Error(
        `hardware/esp32/${path.slice(3)} must pin ArduinoJson to exactly ` +
          `${ARDUINOJSON_VERSION}, the release whose parse the tool copies`,
      );
    }
  }
}

function raw(text: string): string {
  if (text.includes(')json"')) {
    throw new Error("captured text contains the raw string delimiter");
  }
  return `R"json(${text})json"`;
}

// A plain C++ string literal for text that needs no escaping.
function plain(text: string): string {
  if (!/^[A-Za-z0-9_.:-]*$/.test(text)) {
    throw new Error(`text needs escaping in C++: ${text}`);
  }
  return `"${text}"`;
}

// The C++ constant for each capture, in call order.
function captureLines(captures: Map<string, Capture>): string[] {
  const lines: string[] = [];
  for (const [name, { status, body, request }] of captures) {
    lines.push(`constexpr Capture ${name} = {${status}, ${raw(body)},`);
    lines.push(`                                ${raw(request)}};`);
  }
  return lines;
}

function responsesHeader(
  captured: Captured,
  facts: { issued: TokenFacts; otherUnit: TokenFacts; capped: TokenFacts },
  seedHex: string,
  production: ProductionCaptured,
): string {
  const arduinoJson = ARDUINOJSON_VERSION.split(".").map(Number);
  const lines = [
    "#pragma once",
    "",
    "// Generated by hardware/esp32/tools/capture_qr_backend.ts from the Deno QR",
    "// and device routes in backend/supabase/functions/_shared/api.ts. Don't edit",
    "// it by hand. Run the tool again when the backend changes.",
    "",
    "#include <cstdint>",
    "",
    "namespace qr_backend {",
    "",
    "struct Capture {",
    "  int status;",
    "  const char* body;     // The response body.",
    "  const char* request;  // The request body, or an empty string if it had none.",
    "};",
    "",
    `constexpr const char* kUnitId = ${plain(UNIT)};`,
    `constexpr const char* kOtherUnitId = ${plain(OTHER_UNIT)};`,
    `constexpr const char* kDeviceKey = ${plain(DEVICE_KEY)};`,
    `constexpr const char* kCredentialId = ${
      plain(facts.issued.credentialId)
    };`,
    `constexpr const char* kOtherUnitCredentialId = ${
      plain(facts.otherUnit.credentialId)
    };`,
    `constexpr const char* kCappedCredentialId = ${
      plain(facts.capped.credentialId)
    };`,
    "",
    "// The tokens from kQrIssued, kQrOtherUnitIssued, and kQrCappedIssued.",
    `constexpr const char* kIssuedToken = ${plain(captured.issuedToken)};`,
    `constexpr const char* kOtherUnitToken = ${
      plain(captured.otherUnitToken)
    };`,
    `constexpr const char* kCappedToken = ${plain(captured.cappedToken)};`,
    "",
    "// Q6 values, computed exactly from the signed claim text:",
    "// ceil(iat_us / 1000) + 1 and floor(exp_us / 1000) - 1.",
    `constexpr int64_t kIssuedIatMs = ${facts.issued.iatMs}LL;`,
    `constexpr int64_t kIssuedExpMs = ${facts.issued.expMs}LL;`,
    `constexpr int64_t kCappedExpMs = ${facts.capped.expMs}LL;`,
    "",
    "// TEST ONLY: the Ed25519 seed of the private key in",
    "// backend/supabase/tests/python_compatibility.json (PKCS8 DER bytes 16 to",
    "// 47). The tests sign tokens with it. Never use it in a deployed environment.",
    `constexpr const char* kTestOnlySeedHex = ${plain(seedHex)};`,
    "",
  ];
  lines.push(...captureLines(captured.captures));
  lines.push(
    "",
    "// The production-clock set: createHandler's default clock, so every time is",
    "// a whole millisecond, as in production. Two manager credentials, 2 ms",
    "// apart. kQrProdGranted is the first one's grant at the earliest",
    "// occurred_at the door writes, iatMs, 2 ms after issued_at, and",
    "// kQrProdGrantedLatest the second one's at the latest, expMs - 502 ms, from",
    "// a 0 ms round trip. kQrProdSecondGrantedEarliest is the second one's",
    "// earliest, 1 ms after issued_at, under its own event ID.",
    `constexpr const char* kProdCredentialId = ${
      plain(production.facts.credentialId)
    };`,
    `constexpr const char* kProdSecondCredentialId = ${
      plain(production.secondFacts.credentialId)
    };`,
    `constexpr const char* kProdToken = ${plain(production.token)};`,
    `constexpr const char* kProdSecondToken = ${
      plain(production.secondToken)
    };`,
    "",
    "// The issued_at the backend stored for each one. The issue response has",
    "// only expires_at.",
    `constexpr const char* kProdIssuedAt = ${plain(production.issuedAt)};`,
    `constexpr const char* kProdSecondIssuedAt = ${
      plain(production.secondIssuedAt)
    };`,
    "",
    "// Q6 values in the firmware's IEEE double arithmetic: ArduinoJson's parse",
    "// of the signed claim text, then ceil(x * 1000) + 1 and floor(x * 1000) - 1.",
    "// The tool copies the parse of this ArduinoJson release, and the replay",
    "// tests don't build against another.",
    `constexpr int kArduinoJsonMajor = ${arduinoJson[0]};`,
    `constexpr int kArduinoJsonMinor = ${arduinoJson[1]};`,
    `constexpr int kArduinoJsonRevision = ${arduinoJson[2]};`,
    `constexpr int64_t kProdIatMs = ${production.facts.iatMs}LL;`,
    `constexpr int64_t kProdExpMs = ${production.facts.expMs}LL;`,
    `constexpr int64_t kProdSecondIatMs = ${production.secondFacts.iatMs}LL;`,
    `constexpr int64_t kProdSecondExpMs = ${production.secondFacts.expMs}LL;`,
    "",
    ...captureLines(production.captures),
    "",
    "}  // namespace qr_backend",
    "",
  );
  return lines.join("\n");
}

function vectorHeader(facts: TokenFacts): string {
  return [
    "#pragma once",
    "",
    "// Generated by hardware/esp32/tools/capture_qr_backend.ts from",
    "// backend/supabase/tests/python_compatibility.json. Don't edit it by hand.",
    "//",
    "// The startup self-test vector (Q11): python_jwt from the fixture, signed by",
    "// Python's cryptography with the fixture's key, and the values that",
    "// verifyQrToken must read from it.",
    "",
    "#include <cstdint>",
    "",
    "namespace ostium {",
    "",
    "// TEST ONLY. Nothing trusts this key unless OSTIUM_BENCH_TOOLS allows it.",
    `constexpr const char* kTestOnlyQrPublicKey = ${plain(golden.public_key)};`,
    `constexpr const char* kQrSelfTestKeyId = ${plain(facts.keyId)};`,
    `constexpr const char* kQrSelfTestToken = ${plain(golden.python_jwt)};`,
    `constexpr const char* kQrSelfTestCredentialId = ${
      plain(facts.credentialId)
    };`,
    `constexpr const char* kQrSelfTestUnitId = ${plain(facts.unitId)};`,
    `constexpr int64_t kQrSelfTestIssuedAtMs = ${facts.iatMs}LL;`,
    `constexpr int64_t kQrSelfTestExpiresAtMs = ${facts.expMs}LL;`,
    "",
    "}  // namespace ostium",
    "",
  ].join("\n");
}

async function main(): Promise<number> {
  const check = Deno.args.includes("--check");
  const unknown = Deno.args.filter((arg) => arg !== "--check");
  if (unknown.length) {
    console.error(`unknown arguments: ${unknown.join(" ")}`);
    return 2;
  }

  await checkArduinoJsonPins();
  const { seedHex, publicKey } = await fixtureKey();
  const captured = await capture();
  const production = await captureProduction(publicKey);
  const facts = {
    issued: await tokenFacts(captured.issuedToken, publicKey),
    otherUnit: await tokenFacts(captured.otherUnitToken, publicKey),
    capped: await tokenFacts(captured.cappedToken, publicKey),
  };
  const vector = await tokenFacts(golden.python_jwt, publicKey);
  if (
    vector.credentialId !== golden.claims.jti ||
    vector.unitId !== golden.claims.unit_id
  ) {
    throw new Error("python_jwt doesn't carry the fixture's claims");
  }

  const outputs: [string, URL, string][] = [
    [
      RESPONSES_PATH,
      RESPONSES,
      responsesHeader(captured, facts, seedHex, production),
    ],
    [VECTOR_PATH, VECTOR, vectorHeader(vector)],
  ];
  if (check) {
    let stale = 0;
    for (const [path, url, text] of outputs) {
      let current: string | undefined;
      try {
        current = await Deno.readTextFile(url);
      } catch (error) {
        if (!(error instanceof Deno.errors.NotFound)) throw error;
      }
      if (current === text) {
        console.log(`${path} is current`);
      } else {
        console.error(`${path} is out of date. From the repository root, run:`);
        console.error(`  ${WRITE_COMMAND}`);
        stale++;
      }
    }
    return stale ? 1 : 0;
  }

  for (const set of [captured.captures, production.captures]) {
    for (const [name, { status, body }] of set) {
      console.log(`${status} ${name}: ${body.slice(0, 100)}`);
    }
  }
  for (const [path, url, text] of outputs) {
    await Deno.writeTextFile(url, text);
    console.log(`Wrote ${path}`);
  }
  return 0;
}

try {
  Deno.exit(await main());
} catch (error) {
  console.error(error instanceof Error ? error.message : error);
  Deno.exit(1);
}
