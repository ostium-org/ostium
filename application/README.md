# Mobile and Web Client

## Connect to Supabase

Follow [Supabase setup](../backend/supabase/README.md). Set `EXPO_PUBLIC_SUPABASE_URL` and
`EXPO_PUBLIC_SUPABASE_PUBLISHABLE_KEY` in this directory's `.env`, then run
`bun run start` or `bun run web`. No FastAPI server or LAN backend IP is needed.
Restart Expo after changing configuration. Use a publishable/anon key only.

The existing Supabase schema and signup trigger are reused without changes.
Confirm signup email before signing in if confirmation is enabled. Existing
legacy app sessions are not reused; sign in again. Supabase manages session
persistence and refresh. The QR screen remains a placeholder.

Explicit demo mode requires an empty Supabase URL and `EXPO_PUBLIC_MOCK_API=true`.
Real failures never fall back to demo data.

## Application

Build one Expo Router application with React Native, React Native Web, and TypeScript. It is the source of truth for both native tenant use and the responsive browser experience.

Organize routes by role, for example `app/(tenant)/qr.tsx` and `app/(manager)/access.tsx`. Place reusable UI in `components/` and Supabase calls in `services/api.ts`. Prefer React Native primitives so screens work on iOS, Android, and the web; isolate native-only capabilities such as secure storage behind a service.

The tenant view displays the current QR credential and relevant access history. The manager view manages users, units, guest access, revocations, and logs. Both use Supabase Auth and the same Edge Function endpoints.
