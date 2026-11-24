#pragma once

#include <cstdint>
#include <string>

namespace ostium {

// Why the door denied or ignored a QR scan. Only scans that pass the signature
// and unit checks with known server time can be reported (Q8).
namespace qr_reason {
constexpr const char* kQrDisabled = "qr_disabled";
constexpr const char* kDeviceRefused = "device_refused";
constexpr const char* kMalformedQr = "malformed_qr";
constexpr const char* kUnsupportedHeader = "unsupported_header";
constexpr const char* kBadSignature = "bad_signature";
constexpr const char* kMalformedClaims = "malformed_claims";
constexpr const char* kWrongUnit = "wrong_unit";
constexpr const char* kClockUnsynced = "clock_unsynced";
constexpr const char* kInvalidClaims = "invalid_claims";
constexpr const char* kRevoked = "revoked";
constexpr const char* kAlreadyUsed = "already_used";
constexpr const char* kIssuedBeforeBoot = "issued_before_boot";
constexpr const char* kExpired = "expired";
constexpr const char* kNotYetValid = "not_yet_valid";
constexpr const char* kUsedListFull = "used_list_full";
constexpr const char* kUnlockFailed = "unlock_failed";
constexpr const char* kStaleScan = "stale_scan";  // Ignored, never reported.
constexpr const char* kRepeat = "repeat";         // Ignored, never reported.
}  // namespace qr_reason

// The event ID for a QR report. Derived from the credential ID, so it's the
// same on every retry and after a reset (Q18). The "qr-" prefix keeps it apart
// from evt-<command_id>, which shares the backend's per-device namespace. At
// most 47 characters.
inline std::string qrEventIdFor(const std::string& credentialId, bool granted) {
  return "qr-" + credentialId + (granted ? "-granted" : "-denied");
}

// One report for POST /devices/{device_id}/qr-events.
struct QrReport {
  std::string credentialId;
  bool granted = false;
  std::string reason;        // A qr_reason code when denied.
  int64_t occurredAtMs = 0;  // ServerClock earliest - 500 ms.

  std::string eventId() const { return qrEventIdFor(credentialId, granted); }
};

// Serializes the report as the compact JSON request body, with exactly these
// keys in this order: event_id, credential_id, result ("granted" or "denied"),
// reason (null when granted), and occurred_at (YYYY-MM-DDTHH:MM:SS.mmmZ).
// Always under 250 bytes.
std::string serializeQrReport(const QrReport& report);

}  // namespace ostium
