#include "qr_access.h"

#include <algorithm>
#include <cstring>

#include "base64url.h"
#include "qr_self_test.h"
#include "qr_test_vector.h"
#include "timing.h"

namespace ostium {
namespace {

constexpr size_t kMaxIssuerLength = 64;

bool isTestKey(const Ed25519PublicKey& publicKey) {
  std::string testKey;
  return decodeBase64Url(kTestOnlyQrPublicKey, std::strlen(kTestOnlyQrPublicKey), &testKey) &&
         testKey.size() == publicKey.size() &&
         std::memcmp(testKey.data(), publicKey.data(), publicKey.size()) == 0;
}

const char* reasonFor(QrTokenProblem problem) {
  switch (problem) {
    case QrTokenProblem::None: break;
    case QrTokenProblem::Malformed: return qr_reason::kMalformedQr;
    case QrTokenProblem::UnsupportedHeader: return qr_reason::kUnsupportedHeader;
    case QrTokenProblem::BadSignature: return qr_reason::kBadSignature;
    case QrTokenProblem::MalformedClaims: return qr_reason::kMalformedClaims;
  }
  return "";
}

}  // namespace

QrAccess::QrAccess(LockController& lock, EventOutbox& reports, const ServerClock& serverClock,
                   const Clock& clock, Log& log, Ed25519Verifier verify)
    : lock_(lock),
      reports_(reports),
      serverClock_(serverClock),
      clock_(clock),
      log_(log),
      verify_(verify) {}

bool QrAccess::begin(const QrSettings& settings) {
  enabled_ = false;
  const auto off = [this](const std::string& reason) {
    disabledReason_ = reason;
    log_.write("QR access off: " + reason);
    return false;
  };

  // The known-answer test runs the full verifyQrToken path on this target,
  // with the verifier that every scan uses. It times only the fixture's own
  // check, which is what one scan costs, so the logged time compares directly
  // with the bench limit for one check.
  uint32_t checkMs = 0;
  if (!runQrSelfTest(kQrSelfTestToken, kTestOnlyQrPublicKey, verify_, &clock_, &checkMs)) {
    return off("self-test failed");
  }
  log_.write("QR self-test passed; Ed25519 check took " + std::to_string(checkMs) + " ms");

  std::string unitId;
  if (settings.unitId.empty()) return off("OSTIUM_UNIT_ID is empty");
  if (!canonicalUuid(settings.unitId, &unitId)) {
    return off("OSTIUM_UNIT_ID must be the unit's UUID");
  }

  QrKey key;
  const char* error = "";
  if (settings.publicKey.empty()) return off("OSTIUM_QR_PUBLIC_KEY is empty");
  if (!makeQrKey(settings.keyId, settings.publicKey, &key, &error)) {
    return off(std::string("OSTIUM_QR_KEY_ID or OSTIUM_QR_PUBLIC_KEY: ") + error);
  }

  if (settings.issuer.empty() || settings.issuer.size() > kMaxIssuerLength ||
      settings.audience.empty() || settings.audience.size() > kMaxIssuerLength) {
    return off("OSTIUM_QR_ISSUER and OSTIUM_QR_AUDIENCE must be set");
  }

  // The fixture's key signs the test tokens, so only the bench build trusts it.
  if (!settings.allowTestKey && isTestKey(key.publicKey)) {
    return off("test-only QR public key outside the bench build");
  }

  unitId_ = unitId;
  issuer_ = settings.issuer;
  audience_ = settings.audience;
  key_ = key;
  disabledReason_.clear();
  enabled_ = true;
  log_.write("QR access on for unit " + unitId_ + ", key " + key_.keyId);
  return true;
}

QrDecision QrAccess::handleScan(const char* data, size_t length, uint32_t decodedAtMs) {
  const char* text = data != nullptr ? data : "";
  if (data == nullptr) length = 0;
  // When the scan arrived. The stale and repeat filters use this reading, and
  // evaluate() reads the clock again for the decision.
  const uint32_t nowMs = clock_.nowMs();

  // Q1. A decode that waited too long is ignored, and the repeat memory
  // doesn't change. One stamped later than now wraps to a huge age, so it's
  // ignored too, and logged with a negative age.
  const uint32_t ageMs = nowMs - decodedAtMs;
  if (ageMs > kMaxScanAgeMs) {
    log_.write("ignored stale QR scan (decoded " +
               std::to_string(static_cast<int32_t>(ageMs)) + " ms ago)");
    QrDecision decision;
    decision.reason = qr_reason::kStaleScan;
    return decision;
  }

  // Q1. The same text again, with no quiet gap of 3 s since it was last seen
  // after a final decision. The gap slides with each repeat. Texts over 768
  // bytes are always malformed, so comparing their first 768 bytes is enough.
  const size_t compared = std::min(length, kMaxQrTokenBytes);
  if (hasLast_ && length == lastLength_ &&
      lastText_.compare(0, std::string::npos, text, compared) == 0 &&
      !reached(nowMs, lastSeenMs_ + kRepeatQuietMs)) {
    lastSeenMs_ = nowMs;
    QrDecision decision;
    decision.reason = qr_reason::kRepeat;
    return decision;
  }

  const char* detail = "";
  const QrDecision decision = evaluate(text, length, &detail);
  // Transient denials aren't remembered, so the next decode is checked again.
  if (decision.isFinal) {
    lastText_.assign(text, compared);
    lastLength_ = length;
    lastSeenMs_ = nowMs;
    hasLast_ = true;
  }
  logDecision(decision, detail, length);
  return decision;
}

QrDecision QrAccess::evaluate(const char* data, size_t length, const char** detail) {
  // The checks run in the contract's order, and the first failure wins (Q17).
  QrDecision decision;
  decision.outcome = QrOutcome::Denied;
  decision.isFinal = true;

  // Q11: cached configuration, and whether the backend still accepts this device.
  if (!enabled_) {
    decision.reason = qr_reason::kQrDisabled;
    *detail = disabledReason_.c_str();
    return decision;
  }
  if (serverClock_.deviceRefused()) {
    decision.reason = qr_reason::kDeviceRefused;
    *detail = "the last poll got 401";
    return decision;
  }

  // Q2 to Q5: shape, header, signature, and only then the claims.
  const QrToken token = verifyQrToken(data, length, key_, verify_);
  if (token.problem != QrTokenProblem::None) {
    decision.reason = reasonFor(token.problem);
    *detail = token.detail;
    return decision;
  }
  // The signature check is the slow step, so the clock is read again after
  // it. Every time check from here on, and the report, use this reading, and
  // nothing slow runs between it and the unlock, so latest still bounds the
  // server's time when the relay switches. Only the stale and repeat filters
  // use the earlier reading.
  const uint32_t decisionMs = clock_.nowMs();
  const QrClaims& claims = token.claims;
  decision.credentialId = claims.credentialId;

  if (claims.unitId != unitId_) {  // Q7
    decision.reason = qr_reason::kWrongUnit;
    return decision;
  }
  // Q10. The bounds are read at the decision time, never the decode time,
  // which makes latest stricter.
  ServerTimeBounds bounds;
  if (!serverClock_.bounds(decisionMs, &bounds)) {
    decision.reason = qr_reason::kClockUnsynced;
    decision.isFinal = false;
    return decision;
  }

  // Q8: from here on, a denial is reported. occurred_at is a time that every
  // device API instance has reached.
  const int64_t occurredAtMs = bounds.earliestMs - kOccurredAtMarginMs;
  // Q12, Q13: a used or revoked jti is forgotten only once occurred_at passes
  // its expiry and kListHoldMs has passed on the door's own clock since it was
  // added. The bounds can restart backward, after a step in the server's clock
  // or a poll from a slower instance, so earliest alone could forget a jti
  // that the next poll makes unexpired again. The hold outlasts every code's
  // lifetime, and occurred_at covers a slower instance after a step back. The
  // purge goes by the bounds the door holds, so a step that it first sees
  // after it forgot a jti can still reopen the code (Known limits).
  used_.removeExpired(occurredAtMs, decisionMs);
  revoked_.removeExpired(occurredAtMs, decisionMs);
  const auto deny = [&](const char* reason) {
    return denyReported(claims, reason, occurredAtMs, bounds.earliestMs, decisionMs);
  };

  if (!claims.hasIssuer || claims.issuer != issuer_ || !claims.hasAudience ||
      claims.audience != audience_ || !claims.hasSubject || claims.hasOtherClaims ||
      claims.expiresAtMs <= claims.issuedAtMs ||
      claims.expiresAtMs - claims.issuedAtMs > kMaxLifetimeMs) {  // Q9
    return deny(qr_reason::kInvalidClaims);
  }
  if (revoked_.contains(claims.credentialId)) return deny(qr_reason::kRevoked);  // Q12
  if (used_.contains(claims.credentialId)) return deny(qr_reason::kAlreadyUsed);  // Q13
  if (claims.issuedAtMs < serverClock_.bootTimeLatestMs()) {  // Q14
    return deny(qr_reason::kIssuedBeforeBoot);
  }
  if (bounds.latestMs >= claims.expiresAtMs) return deny(qr_reason::kExpired);  // Q15
  if (occurredAtMs < claims.issuedAtMs) {  // Q16
    decision.reason = qr_reason::kNotYetValid;
    decision.isFinal = false;
    return decision;
  }
  // Q13. The list never drops an unexpired jti to make room.
  if (used_.size() >= DuplicateList::kCapacity) return deny(qr_reason::kUsedListFull);
  // Q17. A failed unlock doesn't use up the credential.
  if (!lock_.unlock()) return deny(qr_reason::kUnlockFailed);

  used_.add(claims.credentialId, claims.expiresAtMs, occurredAtMs, decisionMs, kListHoldMs);
  QrReport report;
  report.credentialId = claims.credentialId;
  report.granted = true;
  report.occurredAtMs = occurredAtMs;
  decision.outcome = QrOutcome::Granted;
  decision.reason = "";
  decision.reported =
      reports_.push(report.eventId(), serializeQrReport(report), decisionMs, false);
  return decision;
}

QrDecision QrAccess::denyReported(const QrClaims& claims, const char* reason,
                                  int64_t occurredAtMs, int64_t earliestMs, uint32_t nowMs) {
  QrDecision decision;
  decision.outcome = QrOutcome::Denied;
  decision.reason = reason;
  decision.isFinal = true;
  decision.credentialId = claims.credentialId;

  // Q18: at most one denied report per credential, with the first reason.
  const std::string eventId = qrEventIdFor(claims.credentialId, false);
  if (deniedReported_.contains(claims.credentialId) || reports_.contains(eventId)) {
    return decision;
  }
  if (!deniedReported_.add(claims.credentialId, earliestMs + kDenialMemoryMs, earliestMs)) {
    log_.write("QR denial list full; not reporting " + claims.credentialId);
    return decision;
  }
  QrReport report;
  report.credentialId = claims.credentialId;
  report.granted = false;
  report.reason = reason;
  report.occurredAtMs = occurredAtMs;
  decision.reported = reports_.push(eventId, serializeQrReport(report), nowMs, true);
  return decision;
}

void QrAccess::logDecision(const QrDecision& decision, const char* detail, size_t length) {
  // Q20: never the token text, a segment, a signature, or sub.
  if (decision.outcome == QrOutcome::Granted) {
    log_.write("QR granted credential " + decision.credentialId);
  } else if (!decision.credentialId.empty()) {
    log_.write(std::string("QR denied (") + decision.reason + ") credential " +
               decision.credentialId + (decision.reported ? ", reported" : ""));
  } else {
    log_.write(std::string("QR denied (") + decision.reason + ": " + detail + "), " +
               std::to_string(length) + " bytes");
  }
}

bool QrAccess::revoke(const std::string& credentialId, int64_t retainUntilEpochMs,
                      int64_t nowEpochMs) {
  return revoked_.add(credentialId, retainUntilEpochMs, nowEpochMs - kOccurredAtMarginMs,
                      clock_.nowMs(), kListHoldMs);
}

QrSignal qrSignalFor(const QrDecision& decision) {
  switch (decision.outcome) {
    case QrOutcome::Granted:
      return QrSignal::Granted;
    case QrOutcome::Denied:
      return decision.isFinal ? QrSignal::Denied : QrSignal::Wait;
    case QrOutcome::Ignored:
      break;
  }
  return QrSignal::None;
}

size_t drainScans(ScanSource& source, QrAccess& access, size_t limit, QrFeedback* feedback) {
  size_t handled = 0;
  QrScan scan;
  while (handled < limit && source.next(&scan)) {
    const QrSignal signal = qrSignalFor(access.handleScan(scan));
    if (feedback != nullptr && signal != QrSignal::None) feedback->show(signal);
    ++handled;
  }
  return handled;
}

}  // namespace ostium
