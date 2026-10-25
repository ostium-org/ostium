# Ostium

Ostium is a door-access and camera-doorbell project for rental properties,
student housing, and multi-unit residences. Tenants use QR credentials for
entry; managers administer access through a shared mobile and web application.

## Current architecture on this branch

```text
Expo application (iOS / Android / web)
    | Supabase SDK: authentication, sessions, active profiles
    v
Supabase Auth + PostgreSQL
    ^
    | Authorized database operations
Supabase Edge Functions
    ^                         ^
    | User JWT                | Device bearer token
Expo client               ESP32-CAM + lock relay
ostium-api                device-api (planned firmware integration)
```

- **Client:** one Expo Router application in `application/`, using React Native,
  React Native Web, and TypeScript. It authenticates directly with Supabase,
  loads active profiles, and uses SDK-managed sessions and token refresh.
- **Backend:** TypeScript Edge Functions in `backend/supabase/`. `ostium-api`
  handles user commands, QR issuance, and admin role changes. `device-api`
  authenticates device tokens, delivers commands, and records results.
- **Database:** Supabase PostgreSQL stores profiles, units, devices, access
  grants, credentials, and access events. The existing schema reference is in
  `backend/schema/`; this branch does not change or automatically apply it.
- **Hardware:** `hardware/` documents the planned ESP32-CAM and relay. Firmware
  source and physical integration remain to be supplied.

This branch explores Edge Functions in place of FastAPI. There is no Python API
process or laptop-hosted backend to start. Deploy the functions separately when
ready; authentication works directly with the hosted Supabase project.

## Implemented flows and integration status

**Authentication:** signup and login use the Supabase SDK. Profiles must be
active, and the client supports tenant, manager, and admin roles. New users
receive the tenant role through the existing database trigger. Sessions use
chunked SecureStore storage on native and sessionStorage on web.

**Door commands:** a manager/admin sends `lock` or `unlock` through `ostium-api`.
Commands are stored in `access_events`, expire after 10 seconds, and are polled
and acknowledged through `device-api`. Acceptance means queued; it does not
confirm physical relay movement.

**QR credentials:** the user function checks access and can issue Ed25519-signed
credentials valid for up to 60 seconds. It requires a separately provisioned
private signing key. The QR screen deliberately retains its placeholder;
turning on live QR display is separate work.

**Remaining integration:** house listings and membership administration still
use local sample data. Local membership changes do not update Supabase. Live
QR issuance requires real unit UUIDs rather than sample IDs such as `house-1`.
Firmware verification, relay execution, configuration/revocation syncing, and
camera/doorbell features remain future integration work.

## Repository layout

```text
application/       Shared Expo mobile and web client
  src/app/         Screens and layouts
  src/components/  Reusable UI
  src/services/    Supabase client, auth/session storage, API and QR services
  src/data/        Local sample houses and membership state
backend/
  schema/          Existing shared PostgreSQL schema reference
  supabase/        Edge Functions, configuration, Deno tests, setup guide
hardware/          ESP32 and wiring documentation
tests/frontend/   Vitest tests run on Node.js
docs/             API, design, testing, security, and operating guides
```

## Run the application

Use Node.js 22.13 or newer and npm. From the repository root:

```sh
cd application
npm ci
```

If `application/.env` does not exist, copy `application/.env.example` to it.
Set the hosted project configuration:

```dotenv
EXPO_PUBLIC_SUPABASE_URL=https://your-project-ref.supabase.co
EXPO_PUBLIC_SUPABASE_PUBLISHABLE_KEY=your-publishable-or-legacy-anon-key
EXPO_PUBLIC_MOCK_API=false
```

Use the project root URL, without `/rest/v1` or `/auth/v1`. Only a public
publishable/anon key belongs in client configuration. Keep service-role keys
and QR signing keys in backend secrets, out of the app and firmware.

From `application/`:

```sh
npm start
```

Use Expo Go for mobile or `npm run web` for the browser. Restart Expo after
changing environment variables. No LAN backend address or
`EXPO_PUBLIC_API_URL` is needed on this branch. Confirm signup emails if the
project requires it; existing accounts and profiles are reused, but legacy
app sessions require signing in again.

Intentional local demo mode requires an empty Supabase URL and
`EXPO_PUBLIC_MOCK_API=true`. Missing settings or real failures never silently
enable demo mode. Demo accounts do not authenticate against Supabase or devices.

See [client setup](application/README.md) and
[Supabase setup and deployment](backend/supabase/README.md).

## Edge Function endpoints

Append the route to `https://PROJECT.supabase.co/functions/v1/FUNCTION`:

| Function | Method | Route | Purpose |
| --- | --- | --- | --- |
| `ostium-api` | POST | `/api/v1/doors/{door_id}/commands` | Manager/admin lock or unlock |
| `ostium-api` | POST | `/api/v1/houses/{houseId}/qr-credentials` | Signed QR credential |
| `ostium-api` | POST | `/api/v1/auth/users/{user_id}/role` | Admin-only role change |
| `device-api` | GET | `/api/v1/devices/{device_id}/commands` | Device command polling |
| `device-api` | POST | `/api/v1/devices/{device_id}/events` | Device command-result report |

Door and device path IDs refer to `devices.device_key`, for example `front`.
`houseId` refers to a unit UUID. User functions require a user JWT and active
profile. Device functions verify a separate high-entropy bearer token against
its lowercase SHA-256 hex digest in `devices.credential_hash`. Devices must
never receive the Supabase service-role key.

Signup, login, refresh, and sign-out use the SDK rather than FastAPI endpoints.
See [API contracts](docs/api.md) for request and response details.

## Checks

From `application/`:

```sh
npm test
npm run test:coverage
npm run test:typecheck
npx tsc --noEmit
npm run lint
```

From the repository root, with Deno 2 installed:

```sh
deno task --config backend/supabase/deno.json check
deno task --config backend/supabase/deno.json test
deno task --config backend/supabase/deno.json lint
```

Frontend tests mock the SDK and network; Edge Function tests use in-memory
fixtures. Neither suite writes to live Supabase or validates a physical lock.
Keep `application/package-lock.json` as the only package-manager lockfile.
No GitLab CI configuration is checked in; the assignment's automated build,
test, and merge workflow still needs configuration.

## Further documentation

- [Backend overview](backend/readme.md)
- [Supabase setup, function deployment, and key provisioning](backend/supabase/README.md)
- [Client setup](application/README.md)
- [API contracts](docs/api.md)
- [Testing guide](docs/testing.md)
- [Architecture design](docs/architecture.md) and [security design](docs/security.md)
- [Hardware documentation](hardware/README.md)
