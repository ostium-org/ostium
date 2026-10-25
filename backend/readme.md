# Ostium Backend

FastAPI service that fronts **Supabase** (Auth + Postgres). It handles user
authentication, door commands, device command polling, access events, and QR
credential issuance.

> The original design docs mention SQLite + a monolith; that has been
> superseded. We use Supabase for the database and auth, reached through this
> FastAPI layer.

## Stack

- **FastAPI** + **Uvicorn** — HTTP API
- **httpx** — async calls to the Supabase Auth / REST endpoints
- **Supabase** — Postgres (schema in `supabase/`) + Auth

## Layout

```
backend/
  app/
    main.py          # FastAPI app, CORS, /health, router wiring
    config.py        # settings from env / .env
    schemas.py       # Pydantic request/response models
    supabase.py      # async Supabase Auth + REST client (service role)
    routers/
      auth.py        # /api/v1/auth/* (signup, login, me, role)
      access.py      # door commands, device polling/events, QR credentials
  supabase/
    000_initial_schema.sql   # tables, RLS, triggers, helper functions
  requirements.txt
  .env.example
```

## Setup

Run the commands below from `backend/`. From the repository root:

```sh
cd backend
```

1. **Create the database schema.** Run `supabase/000_initial_schema.sql` in the
   Supabase SQL editor (or `supabase db push`). It is idempotent and safe to
   re-run.

2. **Install dependencies.**

   ```sh
   python3 -m venv .venv
   source .venv/bin/activate
   python -m pip install -r requirements.txt
   ```

   On Windows PowerShell, use `python -m venv .venv` and activate with
   `.venv\Scripts\Activate.ps1`.

3. **Configure environment.** If `.env` does not already exist, copy
   `.env.example` to `.env`. Preserve existing settings when updating it.
   The application reads `backend/.env` when launched from `backend/`;
   `.env.local` is not loaded by the current settings configuration. Fill in:
   - `SUPABASE_URL` — the project root, `https://<project-ref>.supabase.co`;
     do not append `/rest/v1` or `/auth/v1` (the backend adds these paths)
   - `SUPABASE_SECRET_KEY` — the **service role** key (Supabase's "secret key")
   - `CORS_ORIGINS` — allowed browser origins (`*` for local dev)

   `SUPABASE_PUBLISHABLE_KEY` is optional. Keep the secret/service role key
   server-side; do not commit it or share it in group chats or firmware.

   Restart FastAPI after changing `.env`; settings are cached, and the reload
   watcher does not necessarily detect environment-file changes.

4. **Run.**
   ```sh
   python -m uvicorn app.main:app --reload --host 0.0.0.0 --port 8000
   ```
   Open http://localhost:8000/docs for the interactive API docs, including
   `POST /api/v1/doors/{door_id}/commands`.

   Check http://localhost:8000/health. It should return `"status": "ok"`.
   `"supabase_configured": true` means the URL and secret key are present;
   it does not verify connectivity, credentials, or database schema.

### Subsequent starts

From the repository root on macOS/Linux:

```sh
cd backend
source .venv/bin/activate
python -m uvicorn app.main:app --reload --host 0.0.0.0 --port 8000
```

Press **Ctrl+C** to stop the server. `--reload` is for local development.

### Board bench test

With the laptop and board on the same network, configure the board's backend
address as `http://<laptop-LAN-IP>:8000`, using the laptop's actual LAN address.
`localhost` on the board refers to the board itself; `0.0.0.0` is the server's
bind address, not a client destination. Allow inbound connections to port 8000
in the laptop firewall if needed.

Use plain HTTP only on a trusted, isolated bench network; use HTTPS for
deployment. The demo also requires a provisioned active device with its token
hash, the associated door records, and a manager/admin account authorized to
send commands. See [API examples](../docs/api.md) for the request flow.

## Auth endpoints

| Method | Path                           | Description                                   |
| ------ | ------------------------------ | --------------------------------------------- |
| POST   | `/api/v1/auth/signup`          | Create an account (always a `tenant`)         |
| POST   | `/api/v1/auth/login`           | Sign in with email + password                 |
| GET    | `/api/v1/auth/me`              | Return the signed-in user (validates token)   |
| POST   | `/api/v1/auth/users/{id}/role` | Set a user's role (authenticated admin only) |
| GET    | `/health`                      | Liveness + Supabase configuration status      |

All auth responses use the shape the Expo client already parses:
`{ access_token, expires_in, user: { id, email, name?, role } }`.

## Security notes

- **New users are always `tenant`.** The DB trigger `handle_new_user` ignores
  any role supplied at signup, so a client cannot self-promote.
- **Role changes are admin-only.** `public.set_user_role` (and the
  `prevent_role_change` trigger) allow only an admin or the service role.
- **The service role key stays server-side.** It is never sent to the client.
- **RLS is enabled on all public tables** and governs the Expo app's access.


## Commands, reports, and QR setup

See [API examples and Thursday walkthrough](../docs/api.md). This change does
not provision records or alter tables, SQL functions, policies, or frontend
behavior. The QR placeholder remains enabled.

Existing `devices.credential_hash` values must be the **lowercase 64-character
hex SHA-256 digest of the exact UTF-8 bearer token** (no prefix or whitespace).
Use independently generated high-entropy tokens, such as 32 random bytes from
`secrets.token_urlsafe(32)`. Firmware stores its own plaintext token securely;
the database stores only the digest. A token from another device is not valid
unless the same token was incorrectly provisioned on both. Never use the
Supabase service key on firmware. Existing hashes using another format must be
reprovisioned manually outside this implementation.

Generate an Ed25519 PKCS8 private key outside the repository:

```sh
openssl genpkey -algorithm ED25519 -out /secure/path/ostium-qr.pem
openssl pkey -in /secure/path/ostium-qr.pem -pubout -out /secure/path/ostium-qr-public.pem
```

Set backend-only `QR_SIGNING_PRIVATE_KEY` to the PEM text (literal `\n` escapes
are accepted). Optional settings: `QR_ISSUER=ostium`,
`QR_AUDIENCE=ostium-devices`, `QR_KEY_ID=ostium-qr-1`. Keep the private key in a
backend secret store, never in a frontend `EXPO_PUBLIC_` variable, firmware,
committed `.env`, or logs. Distribute only the public key to firmware through a
trusted provisioning/configuration channel. Pin the matching key ID, issuer,
and audience there. During rotation, retain old public keys until their issued
credentials expire. Firmware must enforce EdDSA signature verification, trusted
`kid`, issuer/audience, unit binding, issuance/expiry times, JTI and its local
revocation rules. This backend does not implement firmware verification.

Run backend checks from `backend/`:

```sh
.venv/bin/python -m unittest discover -s tests -v
```

Tests use an in-memory database stand-in plus mocked HTTP transport; they do not
write to live Supabase. The concurrency test forces duplicate insert races and
checks the primary-key collision path. A live firmware/database walkthrough
still requires the existing provisioned test records and configured keys.
