#pragma once

// QR access (Q1 to Q20): decides each decoded QR code on the door, unlocks
// through LockController, and queues at most one report per decision.
// docs/firmware/qr-access-v0.md defines the behavior.

#include <cstddef>
#include <cstdint>
#include <string>

#include "duplicate_list.h"
#include "event_outbox.h"
#include "interfaces.h"
#include "lock_controller.h"
#include "qr_event.h"
#include "qr_token.h"
#include "server_clock.h"

namespace ostium {

// The QR settings from secrets.h and board_config.h. begin() checks them.
struct QrSettings {
  std::string unitId;                       // OSTIUM_UNIT_ID; any hex case, stored lowercase
  std::string publicKey;                    // OSTIUM_QR_PUBLIC_KEY; 32 bytes, base64url, 43 chars
  std::string keyId = "ostium-qr-1";        // OSTIUM_QR_KEY_ID
  std::string issuer = "ostium";            // OSTIUM_QR_ISSUER
  std::string audience = "ostium-devices";  // OSTIUM_QR_AUDIENCE
  bool allowTestKey = false;                // true only with OSTIUM_BENCH_TOOLS
};

enum class QrOutcome { Granted, Denied, Ignored };

struct QrDecision {
  QrOutcome outcome = QrOutcome::Ignored;
  const char* reason = "";   // A qr_reason code; "" when granted.
  bool isFinal = false;      // Granted and final denials; false for transient and Ignored.
  bool reported = false;     // This decision queued a report.
  std::string credentialId;  // Set once the signature passed and the claims parsed.
};

// The stateful QR decision maker. It trusts a token only after the signature
// and unit checks, and takes time only from ServerClock's bounds, so a forged
// or foreign token never names a credential in a report (Q8). Single-threaded:
// call it only from the loop task.
class QrAccess {
 public:
  static constexpr uint32_t kMaxScanAgeMs = 2000;
  static constexpr uint32_t kRepeatQuietMs = 3000;
  static constexpr int64_t kOccurredAtMarginMs = 500;
  static constexpr int64_t kMaxLifetimeMs = 60000;
  static constexpr int64_t kDenialMemoryMs = 120000;
  // Used and revoked IDs stay at least this long on the door's own clock,
  // however the server's clock moves, and until occurred_at passes their
  // expiry (Q12, Q13): the longest lifetime, plus 1,000 ms for the 500 ms skew
  // between device API instances and for drift.
  static constexpr uint32_t kListHoldMs = 61000;

  // reports is the QR outbox (ReportKind::Qr). verify is the signature check
  // that begin()'s self-test proves and every scan then runs. The firmware
  // always leaves it at verifyEd25519. Only the tests pass another one: a
  // broken one, to prove that a failed self-test leaves QR access off, or a
  // slow one. No verifier skips the self-test, which runs on every begin().
  QrAccess(LockController& lock, EventOutbox& reports, const ServerClock& serverClock,
           const Clock& clock, Log& log, Ed25519Verifier verify = verifyEd25519);

  // Runs the self-test with the verifier, validates settings, applies the
  // test-key guard, and logs the result. Until it returns true, every scan is
  // denied with qr_disabled.
  bool begin(const QrSettings& settings);
  bool enabled() const { return enabled_; }
  const std::string& disabledReason() const { return disabledReason_; }

  // The single entry point for a decoded QR. Call it only from the loop task.
  QrDecision handleScan(const char* data, size_t length, uint32_t decodedAtMs);
  QrDecision handleScan(const QrScan& scan) {
    return handleScan(scan.text.data(), scan.text.size(), scan.decodedAtMs);
  }

  // Q12 revocation cache. credentialId (a lowercase jti) is denied with
  // "revoked" until occurred_at (earliest - kOccurredAtMarginMs) passes
  // retainUntilEpochMs and kListHoldMs has passed on the door's clock since
  // this call. nowEpochMs is earliest at the call. Returns false when the list
  // holds 32 entries it can't forget yet. Nothing on the board calls it yet.
  // It's the hook for a future backend feed, and the tests use it.
  bool revoke(const std::string& credentialId, int64_t retainUntilEpochMs, int64_t nowEpochMs);

 private:
  // Runs the decision order after the Q1 filters. It reads the clock again
  // after the signature check, for the time checks, the report, and the unlock.
  QrDecision evaluate(const char* data, size_t length, const char** detail);
  QrDecision denyReported(const QrClaims& claims, const char* reason, int64_t occurredAtMs,
                          int64_t earliestMs, uint32_t nowMs);
  void logDecision(const QrDecision& decision, const char* detail, size_t length);

  LockController& lock_;
  EventOutbox& reports_;
  const ServerClock& serverClock_;
  const Clock& clock_;
  Log& log_;
  Ed25519Verifier verify_;
  bool enabled_ = false;
  std::string disabledReason_ = "begin() not called";
  std::string unitId_, issuer_, audience_;
  QrKey key_;
  DuplicateList used_;            // Granted jti until earliest passes exp, held (Q13).
  DuplicateList revoked_;         // Q12, held.
  DuplicateList deniedReported_;  // jti with a queued or sent denial (Q18).
  bool hasLast_ = false;          // The last FINAL decision's text (Q1).
  std::string lastText_;          // Its first min(length, kMaxQrTokenBytes) bytes.
  size_t lastLength_ = 0;
  uint32_t lastSeenMs_ = 0;
};

// Hands up to `limit` waiting scans from source to access, in order. Returns
// how many it handled. Decisions are logged by QrAccess. SCRUM-68 can write its
// own loop instead to read each QrDecision for LCD feedback.
size_t drainScans(ScanSource& source, QrAccess& access, size_t limit = 4);

}  // namespace ostium
