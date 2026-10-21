# Mobile and Web Client

## Connect signup to Supabase

1. Follow [backend setup](../backend/readme.md), including running the SQL schema
   that creates the `auth.users` → `public.profiles` trigger.
2. Copy `.env.example` to `.env` in this directory. Set `EXPO_PUBLIC_API_URL` to
   the FastAPI server address. For a physical phone, use the computer's LAN IP
   and start the backend with `uvicorn app.main:app --reload --host 0.0.0.0`
   from `backend/`.
3. Restart the Expo server after changing environment variables. Sign out of any
   existing demo session and create the account again. Demo accounts are local
   and cannot be transferred to Supabase.

Signup creates an Auth user and, through the SQL trigger, a `public.profiles`
row. Units, grants, and the other app tables are populated by later actions.
If email confirmation is enabled, confirm the email before signing in.

Local demo mode requires an empty `EXPO_PUBLIC_API_URL` and
`EXPO_PUBLIC_MOCK_API=true`; it does not write to Supabase. Without either a
backend URL or explicit demo mode, authentication reports missing configuration.

## Application

Build one Expo Router application with React Native, React Native Web, and TypeScript. It is the source of truth for both native tenant use and the responsive browser experience.

Organize routes by role, for example `app/(tenant)/qr.tsx` and `app/(manager)/access.tsx`. Place reusable UI in `components/` and FastAPI calls in `services/api.ts`. Prefer React Native primitives so screens work on iOS, Android, and the web; isolate native-only capabilities such as secure storage behind a service.

The tenant view displays the current QR credential and relevant access history. The manager view manages users, units, guest access, revocations, and logs. Both use the same FastAPI JSON API.
