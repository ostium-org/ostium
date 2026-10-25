# Mobile and Web Client

## Package manager

Use Node.js 22.13 or newer and npm. Keep `package-lock.json` as the only
lockfile. Use `npm ci` after cloning or pulling dependency changes, and commit
lockfile updates with intentional dependency changes. Use `npx expo install
<package>` for SDK-compatible Expo dependencies and `npx expo-doctor` for checks.

```sh
npm ci
npm start
```

## Connect to Supabase

Follow [Supabase setup](../backend/supabase/README.md). Set `EXPO_PUBLIC_SUPABASE_URL` and
`EXPO_PUBLIC_SUPABASE_PUBLISHABLE_KEY` in this directory's `.env`, then run
`npm run start` or `npm run web`. No FastAPI server or LAN backend IP is needed.
Restart Expo after changing configuration. Use a publishable/anon key only.

The existing Supabase schema and signup trigger are reused without changes.
Confirm signup email before signing in if confirmation is enabled. Existing
legacy app sessions are not reused; sign in again. Supabase manages session
persistence and refresh. The QR screen remains a placeholder.

Explicit demo mode requires an empty Supabase URL and `EXPO_PUBLIC_MOCK_API=true`.
Real failures never fall back to demo data.

## Houses

House lists, details, and the QR selector load active units from Supabase using
the signed-in session and existing RLS policies. Both admins and managers see
active properties. Tenants see units with active access grants whose start time
has passed and whose expiry has not been reached. Null addresses display as empty
text. Screens refresh on navigation; pull down on the home list to refresh it.

Database/network errors show a retry action rather than sample houses or a false
empty result. The local sample memberships and add/remove controls are available
only in explicit demo mode; live membership editing is not implemented yet.

## Application

Build one Expo Router application with React Native, React Native Web, and TypeScript. It is the source of truth for both native tenant use and the responsive browser experience.

Organize routes by role, for example `app/(tenant)/qr.tsx` and `app/(manager)/access.tsx`. Place reusable UI in `components/` and Supabase calls in `services/api.ts`. Prefer React Native primitives so screens work on iOS, Android, and the web; isolate native-only capabilities such as secure storage behind a service.

The tenant view displays the current QR credential and relevant access history. The manager view manages users, units, guest access, revocations, and logs. Both use Supabase Auth and the same Edge Function endpoints.
