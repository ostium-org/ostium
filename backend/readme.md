# Ostium Backend

FastAPI service that fronts **Supabase** (Auth + Postgres). It owns user
authentication and will later own units, devices, access grants, credentials,
and access events.

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
  supabase/
    000_initial_schema.sql   # tables, RLS, triggers, helper functions
  requirements.txt
  .env.example
```

## Setup

1. **Create the database schema.** Run `supabase/000_initial_schema.sql` in the
   Supabase SQL editor (or `supabase db push`). It is idempotent and safe to
   re-run.

2. **Install dependencies.**

   ```
   python -m venv .venv
   .venv\Scripts\activate        # Windows
   # source .venv/bin/activate   # macOS / Linux
   pip install -r requirements.txt
   ```

3. **Configure environment.** Copy `.env.example` to `.env` and fill in:
   - `SUPABASE_URL` — your project URL
   - `SUPABASE_SECRET_KEY` — the **service role** key (Supabase's "secret key")
   - `CORS_ORIGINS` — allowed browser origins (`*` for local dev)

   > Supabase renamed the old keys: _anon key_ → **publishable key**,
   > _service role key_ → **secret key**. The backend only needs the secret key.

4. **Run.**
   ```
   uvicorn app.main:app --reload
   ```
   Open http://localhost:8000/docs for the interactive API docs.

## Auth endpoints

| Method | Path                           | Description                                   |
| ------ | ------------------------------ | --------------------------------------------- |
| POST   | `/api/v1/auth/signup`          | Create an account (always a `tenant`)         |
| POST   | `/api/v1/auth/login`           | Sign in with email + password                 |
| GET    | `/api/v1/auth/me`              | Return the signed-in user (validates token)   |
| POST   | `/api/v1/auth/users/{id}/role` | Set a user's role (admin / service role only) |
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
