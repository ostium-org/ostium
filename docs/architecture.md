# Architecture

## Components

The system has three runtime components:

```text
Expo app -> Supabase Auth + PostgreSQL
Expo app / ESP32-CAM -> Supabase Edge Functions -> PostgreSQL
```

Supabase Auth handles accounts and sessions. Edge Functions handle QR issuance, remote door commands, device polling, reports, and admin role changes using the existing PostgreSQL tables. The shared schema in `backend/schema/000_initial_schema.sql` is unchanged.

The Expo Router application serves both tenant and manager roles. It uses React Native Web for the browser dashboard and React Native for iOS and Android, sharing routes, components, and the Supabase client.

ESP32-CAM firmware uses Arduino. It controls the camera and relay, stores configuration and revocation data locally, and uses HTTPS rather than MQTT.

## Trust Boundary

The ESP32 is the enforcement point. It locally checks QR expiry, signature, device/unit binding, and cached revocation state before unlocking the relay. A trusted Edge Function signs QR credentials; planned firmware verifies them locally. Remote manager commands use a separate polling flow.

## Device Synchronization

Each device has a unique ID and credential. The implemented device endpoints poll `/api/v1/devices/{device_id}/commands` and post command reports to `/api/v1/devices/{device_id}/events` beneath the `device-api` function URL. Configuration/revocation sync and general doorbell events remain future integration work. The backend authenticates and authorizes device calls independently from user calls. Network loss may delay configuration updates but must not prevent a valid locally verified credential from unlocking.
