# Ostium

Ostium is a door-access and camera-doorbell project for rental properties,
student housing, and multi-unit residences. Tenants use QR credentials for
entry; managers administer access through a shared mobile and web application.

## Current architecture

```text
Expo application (iOS / Android / web)
                  | JSON API + user bearer token
                  v
             FastAPI backend
                  | Supabase Auth + Postgres REST API
                  v
                Supabase

ESP32-CAM + lock relay (planned firmware integration)
                  | Device bearer token
                  | Poll commands / report results
                  v
             FastAPI backend
```

- **Client:** one Expo Router application in `application/`, using React Native,
  React Native Web, and TypeScript. Authentication and QR credential requests
  go through FastAPI.
- **Backend:** one FastAPI service in `backend/`. It validates users and roles,
  issues door commands and signed QR credentials, authenticates devices, and
  records command results. This is currently a single service, not multiple
  independently deployed microservices.
- **Database and identity:** Supabase provides Auth and PostgreSQL. The schema
  defines profiles, units, devices, access grants, credentials, and access
  events. The backend accesses Supabase using its server-side secret key.
- **Hardware:** `hardware/` currently contains documentation for an ESP32-CAM
  and relay. Firmware source and physical integration remain to be supplied.

SQLite is not the current backend database. Earlier design documents may
still describe the original architecture or planned functionality; the
implemented routes are documented in [the API guide](docs/api.md).

## Implemented flows and integration status

**Authentication:** the client calls FastAPI signup/login endpoints. FastAPI
uses Supabase Auth and the user's profile. New accounts receive the tenant
role; changing roles requires an admin. The backend supports tenant, manager,
and admin roles, while the client currently parses tenant and manager roles.

**Remote door commands:** a manager/admin sends `lock` or `unlock`. FastAPI
queues a command in `access_events`; the device polls for pending commands and
posts its result. Commands expire after 10 seconds. Accepting a command does
not confirm that the physical lock has moved.

**QR credentials:** FastAPI checks unit access and issues an Ed25519-signed
credential valid for up to 60 seconds. Signing requires a separately configured
private key. The client has QR fetching/display logic, but house listings and
membership administration still use local sample data. Sample IDs such as
`house-1` must be replaced with provisioned unit UUIDs for live QR issuance.

**Remaining integration:** local membership changes do not update Supabase.
Firmware QR verification, relay execution, configuration/revocation syncing,
and camera/doorbell features are not implemented in this checkout. These
require further work before the full access flow can be demonstrated.

## Repository layout

```text
application/       Shared Expo mobile and web client
  src/app/         Expo Router screens and layouts
  src/components/  Reusable UI
  src/services/    API requests, sessions, token storage, QR credentials
  src/data/        Local sample houses and membership state
backend/
  app/             FastAPI application, settings, Supabase client, routers
  supabase/        PostgreSQL schema, RLS policies, triggers, functions
  tests/           Backend automated tests
hardware/          ESP32 and wiring documentation
tests/frontend/   Client tests run with Vitest on Node.js
docs/             API, design, testing, security, and operating guides
```

## Run locally

### Backend

From the repository root:

```sh
cd backend
python3 -m venv .venv
source .venv/bin/activate
python -m pip install -r requirements.txt
```

If `backend/.env` does not exist, copy `backend/.env.example` to it. Configure
`SUPABASE_URL` and `SUPABASE_SECRET_KEY`, and apply
[the database schema](backend/supabase/000_initial_schema.sql) to your Supabase
project. Configure the QR signing key separately when testing QR issuance.
See [backend setup](backend/readme.md) for details, including Windows setup.

From `backend/`, start the server:

```sh
python -m uvicorn app.main:app --reload --host 0.0.0.0 --port 8000
```

- [Interactive API documentation](http://localhost:8000/docs)
- [Health check](http://localhost:8000/health)

The health check reports whether Supabase settings are present; it does not
verify database connectivity. Press **Ctrl+C** to stop the server.

### Mobile and web application

Use Node.js 22.13 or newer and npm. Commit `application/package-lock.json`
with dependency changes; `npm ci` installs the versions recorded there.

In another terminal, from the repository root:

```sh
cd application
npm ci
```

If `application/.env` does not exist, copy `application/.env.example` to it.
Set `EXPO_PUBLIC_API_URL` to the FastAPI address:

- Same-computer browser or iOS simulator: `http://localhost:8000`
- Physical phone or board: `http://<laptop-LAN-IP>:8000`
- Android emulator: `http://10.0.2.2:8000`

Use the same network for physical devices and allow inbound port 8000 on the
laptop. Restart Expo after changing environment variables. Configure backend
`CORS_ORIGINS` for the browser origin (`*` is available for local development).
Use HTTP only for trusted local testing; deployed connections should use HTTPS.

From `application/`:

```sh
npm run start
```

Use `npm run web` to open the web client, or the Expo development server's
platform options for mobile.

For an intentional local UI demo, leave `EXPO_PUBLIC_API_URL` empty and set
`EXPO_PUBLIC_MOCK_API=true`. Demo authentication does not create Supabase users
or authenticate real devices. See [client setup](application/README.md).

## Backend endpoints

| Method | Path | Purpose |
| --- | --- | --- |
| GET | `/health` | Liveness and settings presence |
| POST | `/api/v1/auth/signup` | Register a tenant |
| POST | `/api/v1/auth/login` | Sign in |
| GET | `/api/v1/auth/me` | Validate the current user |
| POST | `/api/v1/auth/users/{user_id}/role` | Admin-only role change |
| POST | `/api/v1/doors/{door_id}/commands` | Manager/admin lock or unlock |
| GET | `/api/v1/devices/{device_id}/commands` | Device command polling |
| POST | `/api/v1/devices/{device_id}/events` | Device command-result report |
| POST | `/api/v1/houses/{houseId}/qr-credentials` | Issue a signed QR credential |

Door and device path IDs refer to `devices.device_key` (for example, `front`);
`houseId` refers to a unit UUID. User endpoints use Supabase user bearer tokens.
Device endpoints use separate device bearer tokens whose SHA-256 hashes are
stored in `devices.credential_hash`. Keep Supabase secret keys and QR private
keys on the backend, out of client configuration and firmware.

See [API examples](docs/api.md) for payloads, authorization, provisioning, and
the bench-test walkthrough.

## Checks

Backend, from `backend/` after activating its virtual environment:

```sh
python -m unittest discover -s tests -v
```

Frontend, from `application/`:

```sh
npm run test
npm run test:typecheck
npm run lint
```

Backend tests use mocked Supabase transport and an in-memory stand-in; they do
not demonstrate a live database or physical lock. No GitLab CI configuration
is currently checked into this repository; automated builds, tests, and merging
to main still need to be configured for the assignment.

## Further documentation

- [Backend setup and key provisioning](backend/readme.md)
- [Client setup](application/README.md)
- [API contracts and walkthrough](docs/api.md)
- [Testing guide](docs/testing.md)
- [Architecture design](docs/architecture.md) and [security design](docs/security.md)
- [Hardware documentation](hardware/README.md)
