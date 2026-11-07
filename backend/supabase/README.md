# Supabase backend

The Expo app uses **Supabase Auth directly** for signup, login, sessions, refresh,
and sign-out. The existing endpoint behavior lives in two TypeScript Edge Functions.
There is no FastAPI process or separate server to run.

The existing schema stays in `backend/schema/000_initial_schema.sql`, unchanged.
There are **no database migrations, table changes, policy changes, or seed scripts**
in this implementation. Nothing here automatically updates the shared project.

## Run the app

In `application/.env`, set:

```env
EXPO_PUBLIC_SUPABASE_URL=https://your-project.supabase.co
EXPO_PUBLIC_SUPABASE_PUBLISHABLE_KEY=your-publishable-or-legacy-anon-key
EXPO_PUBLIC_MOCK_API=false
```

Use only the publishable/anon key in the app; never a service-role or signing key.
From `application/`, run `npm ci`, then `npm run start` for
Expo Go or `npm run web` for the browser. Restart Expo after changing `.env`.
The hosted URL works across Wi-Fi networks; a local backend IP is unnecessary.

Existing accounts and profile rows are reused. Profiles must be active; signup
continues to default to tenant using the project's existing trigger. If email
confirmation is enabled, confirm the email and then sign in. Session storage uses
SecureStore on native and sessionStorage on web. Supabase owns token refresh.

The QR screen displays rotating signed credentials. Live house lists, details, and the
QR selector read Supabase units. Managers/admins see active units; tenants see
units covered by their current access grants. There are no hardcoded placeholder
houses or seeded memberships; demo mode shows an empty list. Missing configuration or a
real request failure never silently enables demo mode. Intentional development demo
mode requires an empty Supabase URL and `EXPO_PUBLIC_MOCK_API=true`.

## Endpoints

| Function | Routes |
| --- | --- |
| `ostium-api` | `POST /api/v1/doors/{door_id}/commands` |
| `ostium-api` | `POST /api/v1/houses/{houseId}/qr-credentials` |
| `ostium-api` | `GET /api/v1/houses/{houseId}/qr-credentials/{credentialId}/status` |
| `ostium-api` | `POST /api/v1/auth/users/{user_id}/role` |
| `device-api` | `GET /api/v1/devices/{device_id}/commands` |
| `device-api` | `POST /api/v1/devices/{device_id}/events` |
| `device-api` | `POST /api/v1/devices/{device_id}/qr-events` |

Append the route to `https://PROJECT.supabase.co/functions/v1/FUNCTION`.
Signup/login use the Supabase SDK rather than replacement HTTP endpoints.
Contracts, authorization, report retries, JWT claims, and the existing table formats
are preserved; see [API details](../../docs/api.md). Device credentials remain lowercase
SHA-256 hex digests of high-entropy bearer tokens. Devices receive only their own
token, never the Supabase service key.

The functions are supplied as code for this branch. **Deploy them separately when
ready**; deploy the updated functions before enabling the app's live QR flow.
The app's auth works independently of QR signing configuration.

```sh
npx supabase functions deploy ostium-api device-api --workdir backend --project-ref YOUR_PROJECT_REF --use-api
```

`ostium-api` requires a user JWT and active profile. `device-api` accepts an opaque
device bearer token and authenticates it itself; its platform JWT check is disabled.
The hosted runtime supplies its service key for authorized database operations.
For QR issuance, configure `QR_SIGNING_PRIVATE_KEY` (Ed25519 PKCS8 PEM), `QR_ISSUER`
(default `ostium`), `QR_AUDIENCE` (default `ostium-devices`), and `QR_KEY_ID` (default
`ostium-qr-1`) as function secrets. Literal `\n` escapes in the PEM are accepted.
Never use the key in `tests/python_compatibility.json`; it is test-only. Distribute
only the matching public key to firmware through a trusted provisioning channel.
Missing/invalid signing configuration returns 503; auth remains usable.

## Local verification

From the repository root, with Deno 2 installed:

```sh
deno task --config backend/supabase/deno.json check
deno task --config backend/supabase/deno.json test
deno task --config backend/supabase/deno.json lint
```

Tests use in-memory fixtures and a Python-generated compatibility fixture; they do
not touch Supabase. From `application/`, run `npm test`, `npm run test:typecheck`,
`npx tsc --noEmit`, and `npm run lint`. Existing unrelated frontend lint failures
remain outside this change. Physical QR reader firmware and live membership
administration remain separate work.

## QR screen shows a server error for every house

House lists use database reads directly; QR issuance calls `ostium-api` and also
requires its own Ed25519 signing secret. A working house list does not establish
that QR signing is configured. A key in a local env file is not automatically
uploaded to hosted Edge Functions. Check the failed request's status/response in
`ostium-api` invocation logs to distinguish missing signing configuration (503)
from database/service failures (502) or a function startup failure.

If `backend/supabase/.env.local` contains your provisioned signing configuration,
upload it from the repository root, replacing `YOUR_PROJECT_REF` with the project
used by the app:

```sh
npx supabase secrets set --env-file backend/supabase/.env.local --project-ref YOUR_PROJECT_REF
npx supabase functions deploy ostium-api device-api --workdir backend --project-ref YOUR_PROJECT_REF --use-api
```

Keep that file private and use your existing key; generating a replacement would
also require updating the public key provisioned on readers. Never put the private
key in an `EXPO_PUBLIC_` variable. Secret updates apply without redeployment; the
deploy command ensures the rotating-QR/status endpoints match the application.
After setup, tap **Retry QR code**. The app distinguishes known setup errors from
other HTTP errors without displaying arbitrary backend details.
