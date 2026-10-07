# Mobile and Web Client

Build one Expo Router application with React Native, React Native Web, and TypeScript. It is the source of truth for both native tenant use and the responsive browser experience.

Organize routes by role, for example `app/(tenant)/qr.tsx` and `app/(manager)/access.tsx`. Place reusable UI in `components/` and FastAPI calls in `services/api.ts`. Prefer React Native primitives so screens work on iOS, Android, and the web; isolate native-only capabilities such as secure storage behind a service.

The tenant view displays the current QR credential and relevant access history. The manager view manages users, units, guest access, revocations, and logs. Both use the same FastAPI JSON API.
