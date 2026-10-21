# API

FastAPI exposes JSON over HTTPS under `/api/v1`. User calls validate the
Supabase bearer token and require an active `profiles` row. Manager/admin
roles may command every door. The role-change route requires an authenticated,
active admin. Device bearer tokens are independent of user tokens.

## Door commands

`POST /api/v1/doors/{door_id}/commands`, with a manager/admin bearer token:

```json
{"action":"unlock"}
```

Only `unlock` and `lock` are accepted; extra fields are rejected. A successful
response is `201`:

```json
{
  "command_id":"2392307c-9fef-4bc6-8b4f-d8aa89c442da",
  "action":"unlock",
  "door_id":"front-door",
  "issued_at":"2026-10-22T12:00:00Z",
  "expires_at":"2026-10-22T12:00:10Z"
}
```

Door/device path IDs are `devices.device_key`; internal UUID foreign keys use
`devices.id`. An unknown door returns `404`. Only `active` and `offline`
devices accept commands; other statuses return `409`. Issuance means queued,
not that the relay has moved.

`GET /api/v1/devices/{device_id}/commands`, with that device's bearer token,
returns `200`:

```json
{
  "server_time":"2026-10-22T12:00:01Z",
  "commands":[{
    "command_id":"2392307c-9fef-4bc6-8b4f-d8aa89c442da",
    "action":"unlock",
    "door_id":"front-door",
    "issued_at":"2026-10-22T12:00:00Z",
    "expires_at":"2026-10-22T12:00:10Z"
  }]
}
```

Commands are ordered by issuance time then command UUID. Polling does not
consume them. At `expires_at` they disappear; either report result also removes
them. Firmware must enforce expiry and deduplicate execution by command ID
across repeated polls. No command is delivered to another device.

## Device reports

`POST /api/v1/devices/{device_id}/events`, with the same device bearer token:

```json
{
  "event_id":"firmware-event-123",
  "command_id":"2392307c-9fef-4bc6-8b4f-d8aa89c442da",
  "action":"unlock",
  "result":"done",
  "reason":null,
  "occurred_at":"2026-10-22T12:00:02Z"
}
```

`result` is `done` or `rejected`. All fields are required; `reason` may be null.
`occurred_at` must include a timezone and is normalized to UTC. A first report
or exact retry returns `200`:

```json
{"event_id":"firmware-event-123","command_id":"2392307c-9fef-4bc6-8b4f-d8aa89c442da","recorded":true}
```

Reports may arrive after command expiry. Unknown/other-device commands return
`404`; an action mismatch returns `409`. Event IDs are scoped to the internal
device UUID. Reusing one with different report fields returns `409`, including
a changed result, reason, command, or occurrence time. Equivalent timezone
representations compare equal. A deterministic report UUID plus the existing
primary key makes concurrent exact retries idempotent.

Missing/incorrect device tokens, unknown devices, and devices outside
`active`/`offline` return `401` for both device endpoints.

## QR credentials

`POST /api/v1/houses/{houseId}/qr-credentials` uses a user bearer token and no
body. `houseId` is the UUID in `units.id`. The unit and profile must be active.
Managers/admins may issue for any active unit; tenants need their own active
grant with `valid_from <= now` and `valid_until > now` (or no expiry).

A successful response is `201`, matching the frontend contract:

```json
{
  "house_id":"c10ad395-f37e-4b2f-bfc1-c4b883ac3835",
  "payload":"eyJhbGciOiJFZERTQSJ9.…",
  "expires_at":"2026-10-22T12:01:00Z"
}
```

The QR encodes `payload` exactly as returned. The JWT header is
`alg=EdDSA`, `typ=JWT`, and the configured `kid`. Claims are `iss` (default
`ostium`), `aud` (default `ostium-devices`), `sub` (user UUID), `unit_id` (unit
UUID), unique `jti` (UUID), `iat`, and `exp`. NumericDate times are UTC epoch
seconds and may have fractional seconds. Lifetime is 60 seconds, capped at
the tenant grant expiry. Firmware must preserve fractional expiry precision
or conservatively expire earlier, never round expiry up.

The signed token and claims are persisted in `credentials.payload`, with its
JTI, unit, recipient, issuer, grant (for tenants), and timestamps before returning.
No credential is returned on a persistence failure. Missing or invalid signing
configuration returns `503`. A missing/inactive unit returns `404`; no current
grant returns `403`.

The frontend QR placeholder remains enabled and sends no QR issuance requests.
The house selector still uses sample IDs. This backend change does not connect
those sample houses or enable the live QR display.

## Persistence and failures

Commands and reports reuse `access_events`, with `event_type=config_sync` and
`metadata.kind=door_command` / `command_result`. Command rows use the command
UUID as their ID and store the response fields in metadata. Reports store all
normalized original fields in metadata. Unrelated constrained fields such as
`result` remain null. No schema, policies, or SQL functions are changed.

Upstream connection failures return `502`, timeouts `504`, and database read
or write failures `502`. Missing Supabase configuration returns `503`. API
errors do not log tokens or secret keys. Event scans are suitable for the
integration workload; indexing and schema changes are deferred.

## Thursday walkthrough

Use existing active manager, unit, and active/offline device records. Configure
the backend and firmware as described in [backend setup](../backend/readme.md).
Set `API`, `MANAGER_TOKEN`, `DEVICE_ID` (device key), and `DEVICE_TOKEN` in your
local shell. Do not put tokens into committed scripts or shared logs.

1. Issue a command and copy its `command_id`:

   ```sh
   curl -X POST "$API/api/v1/doors/$DEVICE_ID/commands" \
     -H "Authorization: Bearer $MANAGER_TOKEN" \
     -H 'Content-Type: application/json' -d '{"action":"unlock"}'
   ```

2. Within ten seconds, poll twice. Both responses should contain the command:

   ```sh
   curl "$API/api/v1/devices/$DEVICE_ID/commands" \
     -H "Authorization: Bearer $DEVICE_TOKEN"
   ```

3. Set `COMMAND_ID` to the issued UUID and `OCCURRED_AT` to a current UTC ISO
   timestamp. Report the result (the same timestamp must be used for retries):

   ```sh
   curl -X POST "$API/api/v1/devices/$DEVICE_ID/events" \
     -H "Authorization: Bearer $DEVICE_TOKEN" \
     -H 'Content-Type: application/json' \
     -d "{\"event_id\":\"thursday-unique-event-1\",\"command_id\":\"$COMMAND_ID\",\"action\":\"unlock\",\"result\":\"done\",\"reason\":null,\"occurred_at\":\"$OCCURRED_AT\"}"
   ```

4. Repeat that exact report; both responses must return `recorded:true` and
   there must be one report row. Use a fresh event ID for each new walkthrough.
5. Poll again; the command must be absent. Change the retry's reason to confirm
   `409`, and try a second device to confirm command isolation (`404`).

If the command expired during the walkthrough, reports still succeed. Repeat
with a fresh command to demonstrate disappearance due to acknowledgement
before its ten-second expiry. QR issuance can be checked separately with an
active unit UUID and user bearer token; firmware verification is separate work.
