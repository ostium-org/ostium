# Architecture

For remote commands, the door unit works differently from this page: the backend is in the unlock path, and the door polls for Lock and Unlock commands. QR access follows this page's trust boundary. For the door unit's design and diagrams, see [Door unit design](firmware/design.md).

## Components

The system has three runtime components:

```text
Expo app -> Supabase Auth + PostgreSQL
Expo app / ESP32-S3 door unit -> Supabase Edge Functions -> PostgreSQL
```

Supabase Auth handles accounts and sessions. Edge Functions handle QR issuance, remote door commands, device polling, reports, and admin role changes using the existing PostgreSQL tables. The shared schema in `backend/schema/000_initial_schema.sql` is unchanged.

The Expo Router application serves both tenant and manager roles. It uses React Native Web for the browser dashboard and React Native for iOS and Android, sharing routes, components, and the Supabase client.

The ESP32-S3 door firmware uses Arduino. It controls the relay, checks QR credentials locally against configuration set at build time, and uses HTTPS rather than MQTT. Camera scanning is SCRUM-68.

## Trust Boundary

The ESP32 is the enforcement point. It locally checks QR expiry, signature, unit binding, single use, and cached revocation state before unlocking the relay. A trusted Edge Function signs QR credentials, and the door firmware verifies them locally (SCRUM-38), as the [QR access contract](firmware/qr-access-v0.md) defines. Revocation sync is future work, so the cached revocation list stays empty. Remote manager commands use a separate polling flow.

## Device Synchronization

Each device has a unique ID and credential. The implemented device endpoints poll `/api/v1/devices/{device_id}/commands`, post command reports to `/api/v1/devices/{device_id}/events`, and post QR scan reports to `/api/v1/devices/{device_id}/qr-events` beneath the `device-api` function URL. Configuration/revocation sync and general doorbell events remain future integration work. The backend authenticates and authorizes device calls independently from user calls. Network loss must not prevent a valid locally verified credential from unlocking: offline validation lasts up to 72 hours after the last successful poll, and never works after a reboot until a poll succeeds.
