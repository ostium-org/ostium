# Architecture

The Assignment 1 MVP door unit works differently from this page: the backend is in the unlock path, and the door polls for Lock and Unlock commands. For the door unit's design and diagrams, see [Door unit design](firmware/design.md).

## Components

The system has three runtime components:

```text
Expo app (native + web) -> HTTPS -> FastAPI + SQLite <- HTTPS <- ESP32-CAM
```

FastAPI is a modular monolith. It owns user authentication, roles, units, QR issuance, guest access, revocations, device configuration, and audit logs. SQLite is the initial database; use parameterized `sqlite3` queries. PostgreSQL is a later replacement when concurrency or deployment requirements justify it.

The Expo Router application serves both tenant and manager roles. It uses React Native Web for the browser dashboard and React Native for iOS and Android, sharing routes, components, and the FastAPI client.

ESP32-CAM firmware uses Arduino. It controls the camera and relay, stores configuration and revocation data locally, and uses HTTPS rather than MQTT.

## Trust Boundary

The ESP32 is the enforcement point. It locally checks QR expiry, signature, device/unit binding, and cached revocation state before unlocking the relay. FastAPI issues credentials and distributes configuration but is not in the unlock path.

## Device Synchronization

Each device has a unique ID and credential. It polls `GET /devices/{device_id}/config` on a fixed interval and posts access or doorbell events to `POST /devices/{device_id}/events`. The backend authenticates and authorizes device calls independently from user calls. Network loss may delay configuration updates but must not prevent a valid locally verified credential from unlocking.
