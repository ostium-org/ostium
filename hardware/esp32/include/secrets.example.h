#pragma once

// Placeholder settings for the door unit. To configure a door, copy this file
// to secrets.h in the same folder and replace every value. Git ignores
// secrets.h. Never commit a real token, password, or certificate.
// docs/firmware/lock-command-v0.md, Configuration, describes each setting.

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
