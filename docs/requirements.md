# Requirements

## MVP

- Tenants can sign in and display a short-lived QR credential in the Expo mobile app.
- Property managers can use the Expo web experience to manage users, units, guest access, revocations, and access logs.
- ESP32-CAM scans and locally validates a credential before controlling the relay.
- Supabase Edge Functions record door commands and device reports in the existing PostgreSQL tables.
- Devices continue to validate previously synchronized credentials during a temporary network outage.

## Constraints

Use Supabase Auth, PostgreSQL, Edge Functions, one Expo Router codebase for native and web clients, and ESP32 Arduino firmware. Devices communicate with Edge Functions through HTTPS polling and event posts. The MVP excludes MQTT, Redis, an ORM, a background worker, push notifications, and a second web frontend.
