#pragma once

// Placeholder settings for the door unit. To configure a door, copy this file
// to secrets.h in the same folder and replace every value. Git ignores
// secrets.h. Never commit a real token, password, or certificate.
// docs/firmware/lock-command-v0.md, Configuration, describes each setting, and
// docs/firmware/qr-access-v0.md, Configuration, describes the QR settings.

#define OSTIUM_WIFI_SSID "bench-network"
#define OSTIUM_WIFI_PASSWORD "change-me"

// The full device API root, with no trailing slash. An http:// root works
// only in the esp32s3-bench build.
#define OSTIUM_API_ROOT "https://api.example.com/api/v1"

// This door unit's devices.device_key. The backend also uses it as door_id.
#define OSTIUM_DEVICE_ID "door-unit-01"

// This door unit's own token. The backend stores only its SHA-256 digest.
#define OSTIUM_DEVICE_TOKEN "replace-with-the-device-token"

// PEM root certificate of the backend host, for an https:// root.
#define OSTIUM_ROOT_CA ""

// Signed QR access (SCRUM-38). Leave either value empty to turn QR access off.
// This door's units.id (UUID). devices.unit_id for OSTIUM_DEVICE_ID must be the
// same, or the backend answers every QR report with 403.
#define OSTIUM_UNIT_ID ""
// The backend's QR signing public key: the raw 32-byte Ed25519 key matching
// QR_SIGNING_PRIVATE_KEY, base64url with no padding (43 characters). Never the
// test-only key from python_compatibility.json.
#define OSTIUM_QR_PUBLIC_KEY ""
