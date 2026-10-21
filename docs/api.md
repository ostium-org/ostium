# API

FastAPI exposes JSON over HTTPS. User endpoints use a user access token; device endpoints use a device-specific credential. Version routes before public use, for example `/api/v1/...`.

## User API

Initial user routes cover login, profile, current QR credential, guest credentials, revocation, users, units, and access logs. Enforce roles server-side for every manager action; client route visibility is not authorization.

### QR credential contract (proposed; backend not implemented)

The frontend requests a new credential with authenticated `POST /api/v1/houses/{house_id}/qr-credentials`, using its existing bearer access token and no request body. The server must authorize access to the selected house and issue a short-lived, signed credential suitable for device validation.

Expected JSON response:

```json
{
  "house_id": "house-1",
  "payload": "opaque-signed-door-credential",
  "expires_at": "2026-10-20T12:01:00Z"
}
```

`house_id` is a string matching the requested house. `payload` is a nonempty string encoded into the QR code exactly as returned, without JSON wrapping or modification. `expires_at` is a future ISO timestamp including a timezone; it drives the countdown and refresh. Payload contents and signature format remain the responsibility of the future backend/device integration.

The frontend requests credentials on house selection, manual refresh, or expiry while the QR screen is active. It hides replaced/expired credentials and shows a retry action on failure. Development without `EXPO_PUBLIC_API_URL` uses local mock responses with 60-second expiry and a visible demo label. With an API URL configured, requests use the endpoint above and errors never fall back to demo credentials. The current house selector still uses sample data; real house IDs must be connected before production use.

## Device API (proposed; backend not implemented)

For the MVP, a manager sends Lock or Unlock from the app, the door unit polls for the command, and the door reports one event per command. These routes come from SCRUM-60. For the full contract, including request and response bodies and the rules the door follows, see the [lock command contract](firmware/lock-command-v0.md).

```text
POST /api/v1/doors/{door_id}/commands      manager app, user access token
GET  /api/v1/devices/{device_id}/commands  door unit, device token
POST /api/v1/devices/{device_id}/events    door unit, device token
```

Device calls must be authenticated with the door unit's own credential, scoped to the requested device ID, and accepted only over HTTPS. Configuration sync, revocation data, doorbell events, and snapshot upload (`/devices/{device_id}/config` and `/devices/{device_id}/snapshots`) move to the final product along with signed QR credentials.

The command route is an online unlock path, so it departs from the no-online-unlock rule in [Security](security.md) and the trust boundary in [Architecture](architecture.md). Those documents still describe local QR validation, which returns with signed credentials in the final product (SCRUM-38).
