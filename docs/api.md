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

## Device API

```text
GET  /devices/{device_id}/config
POST /devices/{device_id}/events
POST /devices/{device_id}/snapshots
```

`config` returns the current device configuration and revocation data. `events` accepts an access result or doorbell event. Snapshot upload is optional in the MVP. Device calls must be authenticated, scoped to the requested device ID, and accepted only over HTTPS.
