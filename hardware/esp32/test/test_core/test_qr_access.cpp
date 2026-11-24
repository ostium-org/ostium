// QR access decisions, reports, and scan handling: Q1 and Q7 to Q20.

#include <unity.h>

#include <cstddef>
#include <cstdint>
#include <cstring>
#include <string>
#include <utility>
#include <vector>

#include "qr_fakes.h"
#include "tests.h"

using ostium::QrDecision;
using ostium::QrOutcome;
using ostium::QrSettings;
using ostium::ServerTimeBounds;
using qrfakes::claimsJson;
using qrfakes::epochSeconds;
using qrfakes::headerJson;
using qrfakes::jtiFor;
using qrfakes::kIatFloorMs;
using qrfakes::kIatUs;
using qrfakes::kJti;
using qrfakes::kOtherUnit;
using qrfakes::kUnit;
using qrfakes::kUser;
using qrfakes::mint;
using qrfakes::QrHarness;
using qrfakes::SentQrReport;
using qrfakes::testQrSettings;
using qrfakes::token;

namespace qr_reason = ostium::qr_reason;

namespace {

// "Sync": a poll with a 0 ms round trip, 1 s after token T's iat. That gives
// earliest = S, latest = S + 1, and occurred_at 2026-10-22T12:00:00.623Z.
constexpr int64_t kSyncMs = kIatFloorMs + 1000;
// Token T's expiry after Q6: exp 1792670460.123456, rounded down, minus 1 ms.
constexpr int64_t kExpMs = 1792670460122LL;
// 61 s before T's iat. With a 60 s lifetime, a token issued then expired
// before the sync, but after the default harness's boot.
constexpr int64_t kExpiredIatUs = kIatUs - 61000000;

constexpr const char* kIss = "\"ostium\"";
constexpr const char* kAud = "\"ostium-devices\"";

// Token T: the fixture's claims, with a 60 s lifetime.
std::string tokenT() { return token(kJti, kIatUs); }

std::string subJson() { return "\"" + std::string(kUser) + "\""; }

// Claims for kUnit in signJWT's order, with raw JSON values for iss, aud, and
// sub. An empty value leaves that member out. extra, when set, goes last.
std::string claimsWith(const std::string& jti, const std::string& iss, const std::string& aud,
                       const std::string& sub, int64_t iatUs = kIatUs,
                       int64_t expUs = kIatUs + 60000000, const std::string& extra = "") {
  std::string members;
  const auto add = [&members](const std::string& name, const std::string& value) {
    if (value.empty()) return;
    if (!members.empty()) members += ",";
    members += "\"" + name + "\":" + value;
  };
  add("iss", iss);
  add("aud", aud);
  add("sub", sub);
  add("unit_id", "\"" + std::string(kUnit) + "\"");
  add("jti", "\"" + jti + "\"");
  add("iat", epochSeconds(iatUs));
  add("exp", epochSeconds(expUs));
  if (!extra.empty()) members += "," + extra;
  return qrfakes::rawClaims(members);
}

struct Segments {
  std::string header;
  std::string payload;
  std::string signature;
};

Segments split(const std::string& token) {
  const size_t first = token.find('.');
  const size_t second = token.find('.', first + 1);
  return {token.substr(0, first), token.substr(first + 1, second - first - 1),
          token.substr(second + 1)};
}

// Changes one character: 'A' and 'B' swap, and any other becomes 'A'.
std::string withSwappedCharacter(std::string text, size_t index) {
  text[index] = text[index] == 'A' ? 'B' : 'A';
  return text;
}

// The token with one middle signature character changed, so it no longer
// verifies.
std::string forgedFrom(const std::string& token) {
  return withSwappedCharacter(token, token.size() - 20);
}

const char* outcomeName(QrOutcome outcome) {
  switch (outcome) {
    case QrOutcome::Granted: return "Granted";
    case QrOutcome::Denied: return "Denied";
    case QrOutcome::Ignored: return "Ignored";
  }
  return "unknown";
}

void expectDecision(const QrDecision& decision, QrOutcome outcome, const char* reason,
                    bool isFinal, bool reported, const std::string& label) {
  TEST_ASSERT_EQUAL_STRING_MESSAGE(outcomeName(outcome), outcomeName(decision.outcome),
                                   label.c_str());
  TEST_ASSERT_EQUAL_STRING_MESSAGE(reason, decision.reason, label.c_str());
  TEST_ASSERT_TRUE_MESSAGE(decision.isFinal == isFinal, (label + ": isFinal").c_str());
  TEST_ASSERT_TRUE_MESSAGE(decision.reported == reported, (label + ": reported").c_str());
}

void expectGranted(const QrDecision& decision, const std::string& label) {
  expectDecision(decision, QrOutcome::Granted, "", true, true, label);
}

// A final denial.
void expectDenied(const QrDecision& decision, const char* reason, bool reported,
                  const std::string& label) {
  expectDecision(decision, QrOutcome::Denied, reason, true, reported, label);
}

// A transient denial: not final, so not remembered, and never reported.
void expectWaiting(const QrDecision& decision, const char* reason, const std::string& label) {
  expectDecision(decision, QrOutcome::Denied, reason, false, false, label);
}

void expectIgnored(const QrDecision& decision, const char* reason, const std::string& label) {
  expectDecision(decision, QrOutcome::Ignored, reason, false, false, label);
}

// reason is nullptr for a granted report.
void expectReport(const SentQrReport& report, const std::string& credentialId,
                  const char* reason, const std::string& label) {
  const bool granted = reason == nullptr;
  TEST_ASSERT_EQUAL_STRING_MESSAGE(ostium::qrEventIdFor(credentialId, granted).c_str(),
                                   report.eventId.c_str(), label.c_str());
  TEST_ASSERT_EQUAL_STRING_MESSAGE(credentialId.c_str(), report.credentialId.c_str(),
                                   label.c_str());
  TEST_ASSERT_EQUAL_STRING_MESSAGE(granted ? "granted" : "denied", report.result.c_str(),
                                   label.c_str());
  TEST_ASSERT_EQUAL_STRING_MESSAGE(granted ? "null" : reason, report.reason.c_str(),
                                   label.c_str());
}

bool neverUnlocked(const QrHarness& h) {
  for (const fakes::OutputWrite& write : h.output.writes()) {
    if (write.unlocked) return false;
  }
  return true;
}

// Broken verifiers for the startup self-test, each a way the board's Ed25519
// build could fail. Function pointers can't capture, so the ones that need
// state read it from this file.
bool acceptsAll(const uint8_t*, const ostium::Ed25519PublicKey&, const uint8_t*, size_t) {
  return true;
}

bool rejectsAll(const uint8_t*, const ostium::Ed25519PublicKey&, const uint8_t*, size_t) {
  return false;
}

// The 64 signature bytes of the self-test fixture.
std::string fixtureSignature() {
  const std::string fixture = ostium::kQrSelfTestToken;
  const std::string encoded = fixture.substr(fixture.rfind('.') + 1);
  std::string bytes;
  ostium::decodeBase64Url(encoded.data(), encoded.size(), &bytes);
  return bytes;
}

// Compares the signature with the fixture's and never reads the message, so a
// changed payload passes.
bool ignoresMessage(const uint8_t* signature, const ostium::Ed25519PublicKey&, const uint8_t*,
                    size_t) {
  const std::string expected = fixtureSignature();
  return std::memcmp(signature, expected.data(), expected.size()) == 0;
}

// Checks the message against the fixture's signature and never reads the one
// it's given, so a changed signature passes.
bool ignoresSignature(const uint8_t*, const ostium::Ed25519PublicKey& publicKey,
                      const uint8_t* message, size_t length) {
  const std::string expected = fixtureSignature();
  return ostium::verifyEd25519(reinterpret_cast<const uint8_t*>(expected.data()), publicKey,
                               message, length);
}

// The real verifier, taking 40 ms of fake time per check, like a slow board.
fakes::FakeClock* slowVerifierClock = nullptr;

bool slowVerifier(const uint8_t* signature, const ostium::Ed25519PublicKey& publicKey,
                  const uint8_t* message, size_t length) {
  if (slowVerifierClock != nullptr) slowVerifierClock->advance(40);
  return ostium::verifyEd25519(signature, publicKey, message, length);
}

}  // namespace

void test_valid_qr_unlocks_and_reports_granted() {
  QrHarness h;
  h.sync(kSyncMs);
  const QrDecision decision = h.scan(tokenT());

  expectGranted(decision, "token T");
  TEST_ASSERT_EQUAL_STRING(kJti, decision.credentialId.c_str());
  TEST_ASSERT_TRUE(h.log.contains("QR granted credential " + std::string(kJti)));

  // The grant goes through LockController, so the relock fires 3 s later.
  TEST_ASSERT_TRUE(h.output.unlocked());
  h.clock.advance(3000);
  TEST_ASSERT_FALSE(h.output.unlocked());
  const std::vector<fakes::OutputWrite>& writes = h.output.writes();
  TEST_ASSERT_EQUAL_UINT32(3, writes.size());
  TEST_ASSERT_TRUE(writes[1].unlocked);
  TEST_ASSERT_EQUAL_UINT32(120000, writes[1].atMs);
  TEST_ASSERT_FALSE(writes[2].unlocked);
  TEST_ASSERT_EQUAL_UINT32(123000, writes[2].atMs);

  const std::vector<SentQrReport> sent = h.flushReports();
  TEST_ASSERT_EQUAL_UINT32(1, sent.size());
  TEST_ASSERT_EQUAL_STRING("qr-55555555-5555-4555-8555-555555555555-granted",
                           sent[0].eventId.c_str());
  expectReport(sent[0], kJti, nullptr, "granted report");
  TEST_ASSERT_EQUAL_STRING("2026-10-22T12:00:00.623Z", sent[0].occurredAt.c_str());
}

void test_qr_for_other_unit_is_rejected() {
  QrHarness h;
  h.sync(kSyncMs);
  const QrDecision decision = h.scan(token(kJti, kIatUs, 60000, kOtherUnit));

  expectDenied(decision, qr_reason::kWrongUnit, false, "other unit");
  TEST_ASSERT_EQUAL_STRING(kJti, decision.credentialId.c_str());
  TEST_ASSERT_EQUAL_UINT32(0, h.reports.size());
  TEST_ASSERT_TRUE(neverUnlocked(h));
  TEST_ASSERT_TRUE(h.log.contains("QR denied (wrong_unit) credential " + std::string(kJti)));
  TEST_ASSERT_FALSE(h.log.contains(", reported"));
}

void test_unverified_qr_never_reports() {
  const std::string valid = tokenT();
  const Segments parts = split(valid);
  // The real jti and signature, with the claims re-encoded to expire an hour later.
  const std::string longerLife =
      parts.header + "." +
      ostium::encodeBase64Url(claimsJson(kJti, kIatUs, kIatUs + 3660000000LL)) + "." +
      parts.signature;

  struct Case {
    std::string text;
    const char* reason;
    bool namesCredential;
    const char* label;
  };
  const Case cases[] = {
      {"not a token", qr_reason::kMalformedQr, false, "malformed"},
      {mint(headerJson("ostium-qr-2"), claimsJson(kJti, kIatUs, kIatUs + 60000000)),
       qr_reason::kUnsupportedHeader, false, "bad header"},
      {longerLife, qr_reason::kBadSignature, false, "forged with the real jti"},
      {mint(headerJson(), claimsJson(kJti, kIatUs, kIatUs + 60000000), qrfakes::otherSeed()),
       qr_reason::kBadSignature, false, "signed by another key"},
      {mint(headerJson(), qrfakes::rawClaims("\"jti\":\"" + std::string(kJti) + "\"")),
       qr_reason::kMalformedClaims, false, "malformed claims"},
      {token(kJti, kIatUs, 60000, kOtherUnit), qr_reason::kWrongUnit, true, "another unit"},
  };
  for (const Case& scanCase : cases) {
    QrHarness h;
    h.sync(kSyncMs);
    const QrDecision decision = h.scan(scanCase.text);
    expectDenied(decision, scanCase.reason, false, scanCase.label);
    TEST_ASSERT_EQUAL_STRING_MESSAGE(scanCase.namesCredential ? kJti : "",
                                     decision.credentialId.c_str(), scanCase.label);
    TEST_ASSERT_EQUAL_MESSAGE(0, h.reports.size(), scanCase.label);
    TEST_ASSERT_TRUE_MESSAGE(neverUnlocked(h), scanCase.label);
  }

  // The valid token while QR access is off, while the backend refuses this
  // device, and before the server time is known.
  QrSettings noUnit = testQrSettings();
  noUnit.unitId = "";
  QrHarness disabled(noUnit);
  disabled.sync(kSyncMs);
  QrHarness refused;
  refused.sync(kSyncMs);
  refused.serverClock.onPollRefused();
  QrHarness unsynced;
  expectDenied(disabled.scan(valid), qr_reason::kQrDisabled, false, "qr_disabled");
  expectDenied(refused.scan(valid), qr_reason::kDeviceRefused, false, "device_refused");
  expectWaiting(unsynced.scan(valid), qr_reason::kClockUnsynced, "clock_unsynced");
  for (const QrHarness* h : {&disabled, &refused, &unsynced}) {
    TEST_ASSERT_EQUAL_UINT32(0, h->reports.size());
    TEST_ASSERT_TRUE(neverUnlocked(*h));
  }
}

void test_qr_with_invalid_claims_is_rejected() {
  // With the backend's values, the helper writes the backend's claims.
  TEST_ASSERT_EQUAL_STRING(claimsJson(kJti, kIatUs, kIatUs + 60000000).c_str(),
                           claimsWith(kJti, kIss, kAud, subJson()).c_str());

  struct Case {
    int n;  // The case's jti is jtiFor(n).
    std::string iss;
    std::string aud;
    std::string sub;
    int64_t lifetimeUs;
    std::string extra;
    const char* label;
  };
  const std::string sub = subJson();
  const Case cases[] = {
      {1, "\"evil\"", kAud, sub, 60000000, "", "iss evil"},
      {2, kIss, "[\"ostium-devices\"]", sub, 60000000, "", "aud as an array"},
      {3, kIss, "\"other\"", sub, 60000000, "", "aud other"},
      {4, kIss, kAud, "", 60000000, "", "sub missing"},
      {5, kIss, kAud, "12345", 60000000, "", "sub a number"},
      {6, kIss, kAud, "\"\"", 60000000, "", "sub empty"},
      {7, kIss, kAud, sub, 60000000, "\"device_id\":\"door-unit-01\"", "extra device_id claim"},
      {8, kIss, kAud, sub, 60010000, "", "raw lifetime 60.010 s"},
      {9, kIss, kAud, sub, 60004000, "", "raw lifetime 60.004 s, 60,001 ms after Q6"},
      {10, kIss, kAud, sub, 0, "", "exp equal to iat"},
      {11, kIss, kAud, sub, 2000, "", "exp 2 ms after iat, before it after Q6"},
  };
  QrHarness h;
  h.sync(kSyncMs);
  for (const Case& claimsCase : cases) {
    const std::string jti = jtiFor(claimsCase.n);
    const std::string text =
        mint(headerJson(), claimsWith(jti, claimsCase.iss, claimsCase.aud, claimsCase.sub, kIatUs,
                                      kIatUs + claimsCase.lifetimeUs, claimsCase.extra));
    expectDenied(h.scan(text), qr_reason::kInvalidClaims, true, claimsCase.label);
    const std::vector<SentQrReport> sent = h.flushReports();
    TEST_ASSERT_EQUAL_MESSAGE(1, sent.size(), claimsCase.label);
    expectReport(sent[0], jti, qr_reason::kInvalidClaims, claimsCase.label);

    // Shown again after the repeat gap, it's denied the same way, but not
    // reported again.
    h.clock.advance(3100);
    expectDenied(h.scan(text), qr_reason::kInvalidClaims, false, claimsCase.label);
    TEST_ASSERT_EQUAL_MESSAGE(0, h.flushReports().size(), claimsCase.label);
  }
  TEST_ASSERT_TRUE(neverUnlocked(h));

  // A raw lifetime of exactly 60 s is 59,997 ms after Q6, and 60.003 s is
  // exactly 60,000 ms. Both pass.
  expectGranted(h.scan(mint(headerJson(), claimsWith(jtiFor(20), kIss, kAud, sub))),
                "raw lifetime 60 s");
  expectGranted(h.scan(mint(headerJson(),
                            claimsWith(jtiFor(21), kIss, kAud, sub, kIatUs, kIatUs + 60003000))),
                "raw lifetime 60.003 s");
}

void test_qr_denied_before_first_sync() {
  QrHarness h;
  const std::string text = tokenT();
  const QrDecision decision = h.scan(text);
  expectWaiting(decision, qr_reason::kClockUnsynced, "no sync");
  TEST_ASSERT_EQUAL_STRING(kJti, decision.credentialId.c_str());
  TEST_ASSERT_EQUAL_UINT32(0, h.reports.size());
  TEST_ASSERT_TRUE(neverUnlocked(h));

  // The denial wasn't final, so the same text right after a sync is checked
  // again rather than ignored as a repeat.
  h.sync(kSyncMs);
  expectGranted(h.scan(text), "after the sync");
}

void test_qr_offline_validation_stops_at_limit() {
  constexpr uint32_t kHourMs = 3600000;
  QrHarness h;
  h.sync(kSyncMs, 100);  // The server's clock is S to S + 102 ms at the arrival.
  const uint32_t syncedAtMs = h.clock.nowMs();
  // A token issued agoMs before the server time now, assuming no drift.
  const auto issuedBefore = [&h, syncedAtMs](int n, int64_t agoMs) {
    const int64_t elapsedMs = h.clock.nowMs() - syncedAtMs;
    return token(jtiFor(n), (kSyncMs + elapsedMs - agoMs) * 1000 + 456);
  };

  h.clock.advance(kHourMs);
  expectGranted(h.scan(issuedBefore(1, 2000)), "1 h offline, issued 2 s earlier");

  // At 71 h, 12,780 ms of drift each way puts occurred_at about 13.3 s behind
  // the server, so a code waits that long before it's valid.
  h.clock.advance(70 * kHourMs);
  expectWaiting(h.scan(issuedBefore(2, 2000)), qr_reason::kNotYetValid,
                "71 h offline, issued 2 s earlier");
  expectGranted(h.scan(issuedBefore(3, 20000)), "71 h offline, issued 20 s earlier");

  h.clock.advance(kHourMs);
  expectGranted(h.scan(issuedBefore(4, 20000)), "72 h offline, issued 20 s earlier");

  h.clock.advance(1);
  expectWaiting(h.scan(issuedBefore(5, 2000)), qr_reason::kClockUnsynced, "72 h + 1 ms offline");
  TEST_ASSERT_EQUAL_UINT32(3, h.reports.size());
}

void test_qr_disabled_without_valid_configuration() {
  const std::string text = tokenT();
  const auto expectDisabled = [&text](const QrSettings& settings, const std::string& reason,
                                      const char* label,
                                      ostium::Ed25519Verifier verify = ostium::verifyEd25519) {
    QrHarness h(settings, QrHarness::kDefaultUptimeMs, verify);
    TEST_ASSERT_FALSE_MESSAGE(h.qr.enabled(), label);
    TEST_ASSERT_EQUAL_STRING_MESSAGE(reason.c_str(), h.qr.disabledReason().c_str(), label);
    TEST_ASSERT_TRUE_MESSAGE(h.log.contains("QR access off: " + reason), label);
    TEST_ASSERT_FALSE_MESSAGE(h.log.contains("QR access on"), label);

    h.sync(kSyncMs);
    const QrDecision decision = h.scan(text);
    expectDenied(decision, qr_reason::kQrDisabled, false, label);
    TEST_ASSERT_TRUE_MESSAGE(decision.credentialId.empty(), label);
    TEST_ASSERT_TRUE_MESSAGE(h.log.contains("QR denied (qr_disabled: " + reason + "), " +
                                            std::to_string(text.size()) + " bytes"),
                             label);
    TEST_ASSERT_EQUAL_MESSAGE(0, h.reports.size(), label);
    TEST_ASSERT_TRUE_MESSAGE(neverUnlocked(h), label);
  };
  const std::string keyProblem = "OSTIUM_QR_KEY_ID or OSTIUM_QR_PUBLIC_KEY: ";
  const std::string testKey = ostium::kTestOnlyQrPublicKey;

  // Good settings, but the board's verifier is broken, so the self-test fails
  // and begin() never reaches the settings. Accepting every signature is the
  // dangerous case: those scans would otherwise unlock for any token.
  const std::pair<ostium::Ed25519Verifier, const char*> brokenVerifiers[] = {
      {acceptsAll, "accepts all"}, {rejectsAll, "rejects all"}};
  for (const auto& broken : brokenVerifiers) {
    expectDisabled(testQrSettings(), "self-test failed", broken.second, broken.first);
    QrHarness h(testQrSettings(), QrHarness::kDefaultUptimeMs, broken.first);
    TEST_ASSERT_FALSE_MESSAGE(h.log.contains("QR self-test passed"), broken.second);
  }

  QrSettings settings = testQrSettings();
  settings.unitId = "";
  expectDisabled(settings, "OSTIUM_UNIT_ID is empty", "empty unit");
  settings.unitId = std::string(kUnit).substr(0, 35);
  expectDisabled(settings, "OSTIUM_UNIT_ID must be the unit's UUID", "35-char unit");
  settings.unitId = "2222222g-2222-4222-8222-222222222222";
  expectDisabled(settings, "OSTIUM_UNIT_ID must be the unit's UUID", "non-hex unit");

  settings = testQrSettings();
  settings.publicKey = "";
  expectDisabled(settings, "OSTIUM_QR_PUBLIC_KEY is empty", "empty key");
  settings.publicKey = testKey.substr(0, 42);
  expectDisabled(settings, keyProblem + "public key must be 43 base64url characters (32 bytes)",
                 "42-char key");
  settings.publicKey = testKey.substr(0, 10) + "+" + testKey.substr(11);
  expectDisabled(settings, keyProblem + "public key must be 43 base64url characters (32 bytes)",
                 "non-base64url key");
  settings.publicKey = std::string(43, 'A');
  expectDisabled(settings, keyProblem + "public key is all zero bytes", "all-zero key");
  settings.publicKey = "JuiVj8KyJ7BFw_SJ8u-Y8NXfrAXTxjM5sTgCiG1T_AU";  // A point of order 8.
  expectDisabled(settings, keyProblem + "public key is a small-order point", "order-8 key");

  settings = testQrSettings();
  settings.keyId = "ostium\"qr-1";
  expectDisabled(settings,
                 keyProblem + "key ID must be printable ASCII with no space, quote, or backslash",
                 "kid with a quote");
  settings.keyId = "";
  expectDisabled(settings, keyProblem + "key ID is empty", "empty kid");

  settings = testQrSettings();
  settings.issuer = "";
  expectDisabled(settings, "OSTIUM_QR_ISSUER and OSTIUM_QR_AUDIENCE must be set", "empty issuer");
  settings.issuer = std::string(65, 'i');
  expectDisabled(settings, "OSTIUM_QR_ISSUER and OSTIUM_QR_AUDIENCE must be set",
                 "65-char issuer");
  settings = testQrSettings();
  settings.audience = "";
  expectDisabled(settings, "OSTIUM_QR_ISSUER and OSTIUM_QR_AUDIENCE must be set",
                 "empty audience");

  // Before begin() runs, every scan is denied too.
  QrHarness h;
  ostium::QrAccess fresh(h.lock, h.reports, h.serverClock, h.clock, h.log);
  h.sync(kSyncMs);
  TEST_ASSERT_FALSE(fresh.enabled());
  expectDenied(fresh.handleScan(text.data(), text.size(), h.clock.nowMs()),
               qr_reason::kQrDisabled, false, "begin() not called");
  TEST_ASSERT_TRUE(h.log.contains("QR denied (qr_disabled: begin() not called)"));

  // An uppercase unit UUID is stored lowercase, so it matches the token's.
  const std::string letterUnit = "abcdef01-2345-4678-89ab-cdef01234567";
  settings = testQrSettings();
  settings.unitId = "ABCDEF01-2345-4678-89AB-CDEF01234567";
  QrHarness upper(settings);
  TEST_ASSERT_TRUE(upper.qr.enabled());
  TEST_ASSERT_TRUE(upper.qr.disabledReason().empty());
  TEST_ASSERT_TRUE(upper.log.contains("QR access on for unit " + letterUnit + ", key ostium-qr-1"));
  upper.sync(kSyncMs);
  expectGranted(upper.scan(token(kJti, kIatUs, 60000, letterUnit)), "uppercase unit");
}

void test_test_only_key_disabled_outside_bench() {
  QrSettings settings = testQrSettings();
  settings.allowTestKey = false;
  QrHarness h(settings);
  TEST_ASSERT_FALSE(h.qr.enabled());
  TEST_ASSERT_TRUE(h.qr.disabledReason().find("test-only") != std::string::npos);
  TEST_ASSERT_TRUE(
      h.log.contains("QR access off: test-only QR public key outside the bench build"));
  // The self-test still ran and passed: it always uses the test key.
  TEST_ASSERT_TRUE(h.log.contains("QR self-test passed"));
  h.sync(kSyncMs);
  expectDenied(h.scan(tokenT()), qr_reason::kQrDisabled, false, "test key outside bench");
  TEST_ASSERT_EQUAL_UINT32(0, h.reports.size());
  TEST_ASSERT_TRUE(neverUnlocked(h));

  // The guard is only for the test key. Another key works outside the bench.
  settings.publicKey = qrfakes::publicKeyFor(qrfakes::otherSeed());
  QrHarness other(settings);
  TEST_ASSERT_TRUE(other.qr.enabled());
  other.sync(kSyncMs);
  expectGranted(other.scan(mint(headerJson(), claimsJson(kJti, kIatUs, kIatUs + 60000000),
                                qrfakes::otherSeed())),
                "another key");
  // That door doesn't trust the test key's tokens.
  expectDenied(other.scan(token(jtiFor(1), kIatUs)), qr_reason::kBadSignature, false,
               "test key's token");
}

void test_qr_denied_while_backend_refuses_device() {
  QrHarness h;
  h.sync(kSyncMs);
  h.serverClock.onPollRefused();
  const std::string text = tokenT();
  const QrDecision decision = h.scan(text);
  expectDenied(decision, qr_reason::kDeviceRefused, false, "refused");
  TEST_ASSERT_TRUE(decision.credentialId.empty());
  TEST_ASSERT_TRUE(h.log.contains("QR denied (device_refused: "));
  TEST_ASSERT_EQUAL_UINT32(0, h.reports.size());
  TEST_ASSERT_TRUE(neverUnlocked(h));

  // A good poll lifts the refusal. The denial was final, so the same text is
  // a repeat until 3 s pass with no decode.
  h.clock.advance(1000);
  h.sync(kSyncMs + 1000);
  TEST_ASSERT_FALSE(h.serverClock.deviceRefused());
  expectIgnored(h.scan(text), qr_reason::kRepeat, "within 3 s");
  h.clock.advance(3100);
  expectGranted(h.scan(text), "after the poll succeeded");
}

void test_qr_self_test_detects_bad_verifier() {
  TEST_ASSERT_TRUE(ostium::runQrSelfTest(ostium::kQrSelfTestToken, ostium::kTestOnlyQrPublicKey));

  const std::string fixture = ostium::kQrSelfTestToken;
  const Segments parts = split(fixture);
  const std::string signatureChanged = withSwappedCharacter(fixture, fixture.size() - 20);
  const std::string payloadChanged = withSwappedCharacter(fixture, parts.header.size() + 1 + 30);
  TEST_ASSERT_FALSE(ostium::runQrSelfTest(signatureChanged.c_str(), ostium::kTestOnlyQrPublicKey));
  TEST_ASSERT_FALSE(ostium::runQrSelfTest(payloadChanged.c_str(), ostium::kTestOnlyQrPublicKey));
  const std::string otherKey = qrfakes::publicKeyFor(qrfakes::otherSeed());
  TEST_ASSERT_FALSE(ostium::runQrSelfTest(ostium::kQrSelfTestToken, otherKey.c_str()));
  TEST_ASSERT_FALSE(ostium::runQrSelfTest(nullptr, ostium::kTestOnlyQrPublicKey));
  // A token that verifies but carries other claim values fails too. The last
  // two have the fixture's jti and unit, and differ only in iat or only in
  // exp, so the self-test checks the parse and the Q6 rounding of each.
  TEST_ASSERT_FALSE(
      ostium::runQrSelfTest(token(jtiFor(1), kIatUs).c_str(), ostium::kTestOnlyQrPublicKey));
  const std::string laterIssue =
      mint(headerJson(), claimsJson(kJti, kIatUs + 1000, kIatUs + 60000000));
  const std::string earlierExpiry = token(kJti, kIatUs, 30000);
  TEST_ASSERT_FALSE(ostium::runQrSelfTest(laterIssue.c_str(), ostium::kTestOnlyQrPublicKey));
  TEST_ASSERT_FALSE(ostium::runQrSelfTest(earlierExpiry.c_str(), ostium::kTestOnlyQrPublicKey));

  // A broken verifier fails the self-test, whichever way it's broken. Only
  // the tampered copies catch the first three, because each accepts the
  // fixture.
  const char* key = ostium::kTestOnlyQrPublicKey;
  TEST_ASSERT_TRUE(ostium::runQrSelfTest(ostium::kQrSelfTestToken, key, ostium::verifyEd25519));
  TEST_ASSERT_FALSE(ostium::runQrSelfTest(ostium::kQrSelfTestToken, key, acceptsAll));
  TEST_ASSERT_FALSE(ostium::runQrSelfTest(ostium::kQrSelfTestToken, key, ignoresMessage));
  TEST_ASSERT_FALSE(ostium::runQrSelfTest(ostium::kQrSelfTestToken, key, ignoresSignature));
  TEST_ASSERT_FALSE(ostium::runQrSelfTest(ostium::kQrSelfTestToken, key, rejectsAll));
  TEST_ASSERT_FALSE(ostium::runQrSelfTest(ostium::kQrSelfTestToken, key, nullptr));

  // The self-test runs three checks, but reports the time of one, the
  // fixture's own, because the bench limit is for one check.
  fakes::FakeClock clock;
  slowVerifierClock = &clock;
  uint32_t checkMs = 0;
  const bool passed =
      ostium::runQrSelfTest(ostium::kQrSelfTestToken, key, slowVerifier, &clock, &checkMs);
  slowVerifierClock = nullptr;
  TEST_ASSERT_TRUE(passed);
  TEST_ASSERT_EQUAL_UINT32(120, clock.nowMs());
  TEST_ASSERT_EQUAL_UINT32(40, checkMs);

  QrHarness h;
  TEST_ASSERT_TRUE(h.log.contains("QR self-test passed; Ed25519 check took 0 ms"));
}

void test_revoked_qr_is_rejected() {
  QrHarness h;
  h.sync(kSyncMs);
  TEST_ASSERT_TRUE(h.qr.revoke(kJti, kExpMs, kSyncMs));
  expectDenied(h.scan(tokenT()), qr_reason::kRevoked, true, "revoked");
  TEST_ASSERT_TRUE(neverUnlocked(h));
  const std::vector<SentQrReport> sent = h.flushReports();
  TEST_ASSERT_EQUAL_UINT32(1, sent.size());
  expectReport(sent[0], kJti, qr_reason::kRevoked, "revoked report");

  expectGranted(h.scan(token(jtiFor(1), kIatUs)), "another jti");

  // A revocation lasts until earliest passes its retain time, and at least
  // 61 s on the door's clock after revoke().
  TEST_ASSERT_TRUE(h.qr.revoke(jtiFor(2), kSyncMs + 5000, kSyncMs));
  h.clock.advance(4000);
  expectDenied(h.scan(token(jtiFor(2), kIatUs)), qr_reason::kRevoked, true, "while revoked");
  h.clock.advance(3100);  // earliest is S + 7,099, past the retain time.
  expectDenied(h.scan(token(jtiFor(2), kIatUs + 1)), qr_reason::kRevoked, false,
               "past the retain time, within the hold");
  h.clock.advance(61000 - 7100);
  expectGranted(h.scan(token(jtiFor(2), kIatUs + 30000000)), "after the revocation lapsed");

  // After the hold, occurred_at, not just earliest, must pass the retain time.
  // earliest is S + 60,996 at this revoke().
  TEST_ASSERT_TRUE(h.qr.revoke(jtiFor(36), kSyncMs + 121743, kSyncMs + 60996));
  h.clock.advance(61249);  // earliest is S + 122,242, past the retain time.
  expectDenied(h.scan(token(jtiFor(36), kIatUs + 100000000)), qr_reason::kRevoked, true,
               "earliest past the retain time");
  h.clock.advance(1);  // occurred_at is the retain time.
  expectGranted(h.scan(token(jtiFor(36), kIatUs + 100000001)), "occurred_at at the retain time");

  // The list holds 32 IDs it can't forget yet. One makes room only once its
  // hold has passed and occurred_at, 500 ms before earliest, has passed its
  // retain time.
  for (int i = 3; i <= 34; ++i) TEST_ASSERT_TRUE(h.qr.revoke(jtiFor(i), kExpMs, kSyncMs));
  TEST_ASSERT_FALSE(h.qr.revoke(jtiFor(35), kExpMs, kSyncMs));
  TEST_ASSERT_FALSE(h.qr.revoke(jtiFor(35), kExpMs + 1000, kExpMs + 500));
  h.clock.advance(61000);
  TEST_ASSERT_FALSE(h.qr.revoke(jtiFor(35), kExpMs + 1000, kExpMs + 499));
  TEST_ASSERT_TRUE(h.qr.revoke(jtiFor(35), kExpMs + 1000, kExpMs + 500));
}

void test_qr_is_single_use() {
  QrHarness h;
  h.sync(kSyncMs);
  const std::string text = tokenT();
  expectGranted(h.scan(text), "first scan");

  h.clock.advance(3100);
  expectDenied(h.scan(text), qr_reason::kAlreadyUsed, true, "second scan");
  h.clock.advance(3100);
  expectDenied(h.scan(text), qr_reason::kAlreadyUsed, false, "third scan");
  const std::vector<SentQrReport> sent = h.flushReports();
  TEST_ASSERT_EQUAL_UINT32(2, sent.size());
  expectReport(sent[0], kJti, nullptr, "granted report");
  expectReport(sent[1], kJti, qr_reason::kAlreadyUsed, "denied report");

  // The jti stays used until earliest reaches its expiry, S + 58,999 ms, and
  // 61 s have passed on the door's clock since the grant. 59,002 ms after the
  // sync, earliest reaches the expiry, but the hold keeps the jti.
  h.clock.advance(59002 - 6200);
  expectDenied(h.scan(text), qr_reason::kAlreadyUsed, false, "earliest at expiry");
  // 1 ms before the hold ends, the jti is still used. Other texts with the
  // same jti and times show this inside the repeat gap.
  h.clock.advance(61000 - 1 - 59002);
  expectDenied(h.scan(token(kJti, kIatUs + 1)), qr_reason::kAlreadyUsed, false,
               "just before the hold ends");
  // Then the jti leaves the list, and latest is past its expiry.
  h.clock.advance(1);
  expectDenied(h.scan(token(kJti, kIatUs + 2)), qr_reason::kExpired, false, "after the hold");
  TEST_ASSERT_EQUAL_UINT32(0, h.reports.size());
  TEST_ASSERT_EQUAL_UINT32(3, h.output.writes().size());  // One unlock and its relock.
}

void test_used_list_full_denies_new_credentials() {
  QrHarness h;
  h.sync(kSyncMs);
  for (int i = 0; i < 32; ++i) {
    expectGranted(h.scan(token(jtiFor(i), kIatUs, 30000)), "grant " + std::to_string(i));
    h.flushReports();
  }

  const std::string extra = token(jtiFor(32), kIatUs);
  expectDenied(h.scan(extra), qr_reason::kUsedListFull, true, "33rd credential");
  const std::vector<SentQrReport> sent = h.flushReports();
  TEST_ASSERT_EQUAL_UINT32(1, sent.size());
  expectReport(sent[0], jtiFor(32), qr_reason::kUsedListFull, "used_list_full report");

  // The door never drops an unexpired jti to make room.
  h.clock.advance(3100);
  expectDenied(h.scan(token(jtiFor(0), kIatUs, 30000)), qr_reason::kAlreadyUsed, true,
               "first credential again");

  // 31 s after the sync, the 30 s credentials have expired, but the 61 s hold
  // still keeps their IDs.
  h.clock.advance(31000 - 3100);
  expectDenied(h.scan(token(jtiFor(33), kIatUs)), qr_reason::kUsedListFull, true,
               "after the others expired, within the hold");
  // 61 s after the grants, their IDs leave the list. extra has expired by
  // then, so a code issued 30 s later stands in for it.
  h.clock.advance(61000 - 31000);
  expectGranted(h.scan(token(jtiFor(32), kIatUs + 30000000)),
                "33rd credential after the others were forgotten");
}

void test_used_qr_stays_used_when_server_clock_moves_back() {
  const std::string text = tokenT();

  // Two device API instances 500 ms apart, the most that Time model
  // assumption 5 allows, answer the polls. The true time is kSyncMs at the
  // first poll, which the fast instance answers.
  {
    QrHarness h;
    const uint32_t startMs = h.clock.nowMs();
    const auto trueMs = [&h, startMs]() {
      return kSyncMs + static_cast<int64_t>(h.clock.nowMs() - startMs);
    };
    h.sync(trueMs() + 250);
    expectGranted(h.scan(text), "first scan");
    // 199 ms before the true expiry, the fast instance puts earliest past expMs.
    h.clock.advance(static_cast<uint32_t>(kExpMs - kSyncMs - 199));
    h.sync(trueMs() + 250);
    expectDenied(h.scan(text), qr_reason::kAlreadyUsed, true, "fast instance, past expMs");
    // Another final decision clears the repeat memory.
    expectDenied(h.scan("not a token"), qr_reason::kMalformedQr, false, "other text");
    // The slow instance answers the next poll, so the bounds restart 500 ms
    // back, and latest is before expMs again.
    h.clock.advance(100);
    h.sync(trueMs() - 250);
    TEST_ASSERT_TRUE(h.log.contains("QR time bounds restarted from this poll"));
    expectDenied(h.scan(text), qr_reason::kAlreadyUsed, false, "slow instance, before expMs");
    const std::vector<SentQrReport> sent = h.flushReports();
    TEST_ASSERT_EQUAL_UINT32(2, sent.size());
    expectReport(sent[0], kJti, nullptr, "granted report");
    expectReport(sent[1], kJti, qr_reason::kAlreadyUsed, "denied report");
    TEST_ASSERT_EQUAL_UINT32(3, h.output.writes().size());  // One unlock and its relock.
  }

  // One poll answers 70 s ahead, and the next is correct again.
  {
    QrHarness h;
    h.sync(kSyncMs);
    expectGranted(h.scan(text), "first scan");
    h.clock.advance(3100);
    h.sync(kSyncMs + 3100 + 70000);
    TEST_ASSERT_TRUE(h.log.contains("server clock moved by about 70001 ms"));
    expectDenied(h.scan(text), qr_reason::kAlreadyUsed, true, "while the server clock is ahead");
    h.clock.advance(1500);
    h.sync(kSyncMs + 4600);
    TEST_ASSERT_TRUE(h.log.contains("server clock moved by about -69999 ms"));
    h.clock.advance(3100);
    expectDenied(h.scan(text), qr_reason::kAlreadyUsed, false, "after the clock came back");
    TEST_ASSERT_EQUAL_UINT32(3, h.output.writes().size());
  }

  // The server's clock steps back 5 s during the code's life. 61 s after the
  // grant, earliest is still before expMs, so the door keeps the jti. After
  // earliest passes expMs, an instance 500 ms slower can restart the bounds
  // before expMs again, so the door keeps the jti until occurred_at
  // (earliest - 500 ms) passes expMs.
  {
    QrHarness h;
    h.sync(kSyncMs);
    expectGranted(h.scan(text), "first scan");
    h.clock.advance(30000);
    h.sync(kSyncMs + 30000 - 5000);
    h.clock.advance(31000);  // earliest is S + 55,998.
    expectDenied(h.scan(text), qr_reason::kAlreadyUsed, true, "61 s after the grant");
    h.clock.advance(3100);  // earliest is S + 59,098, past expMs; occurred_at isn't.
    expectDenied(h.scan(text), qr_reason::kAlreadyUsed, false, "earliest past expMs");
    expectDenied(h.scan("not a token"), qr_reason::kMalformedQr, false, "other text");
    // The server's time is now S + 59,200, and the slow instance answers.
    h.clock.advance(100);
    h.sync(kSyncMs + 59200 - 500);
    ServerTimeBounds bounds;
    TEST_ASSERT_TRUE(h.serverClock.bounds(h.clock.nowMs(), &bounds));
    TEST_ASSERT_TRUE(bounds.latestMs < kExpMs);
    expectDenied(h.scan(text), qr_reason::kAlreadyUsed, false, "slow instance, before expMs");
    expectDenied(h.scan("not a token"), qr_reason::kMalformedQr, false, "other text again");
    h.clock.advance(799);  // occurred_at is expMs - 1.
    expectDenied(h.scan(text), qr_reason::kAlreadyUsed, false, "occurred_at before expMs");
    expectDenied(h.scan("not a token"), qr_reason::kMalformedQr, false, "other text a third time");
    h.clock.advance(1);  // occurred_at is expMs, so the door forgets the jti.
    expectDenied(h.scan(text), qr_reason::kExpired, false, "occurred_at at expMs");
    const std::vector<SentQrReport> sent = h.flushReports();
    TEST_ASSERT_EQUAL_UINT32(2, sent.size());
    expectReport(sent[0], kJti, nullptr, "granted report");
    expectReport(sent[1], kJti, qr_reason::kAlreadyUsed, "denied report");
    TEST_ASSERT_EQUAL_UINT32(3, h.output.writes().size());  // One unlock and its relock.
  }

  // The limit in Known limits: what counts is when a poll shows the door the
  // step, not when the step happens. The server's clock steps back 45 s during
  // the code's life, while no poll succeeds. 70 s after the grant, under the
  // old bounds, occurred_at is past expMs and the hold has passed, so a scan
  // makes the door forget the jti. The next poll shows the step and moves
  // latest back before expMs, and the used code opens the door again.
  {
    QrHarness h;
    h.sync(kSyncMs);
    expectGranted(h.scan(text), "first scan");
    const std::vector<SentQrReport> first = h.flushReports();
    TEST_ASSERT_EQUAL_UINT32(1, first.size());
    h.clock.advance(70000);
    expectDenied(h.scan(text), qr_reason::kExpired, true, "offline, after the hold");
    h.clock.advance(1000);
    h.sync(kSyncMs + 71000 - 45000);
    TEST_ASSERT_TRUE(h.log.contains("QR time bounds restarted from this poll"));
    h.clock.advance(3100);  // Past the repeat filter.
    expectGranted(h.scan(text), "after a step the door saw late");
    // The second granted report reuses the event ID with another occurred_at,
    // so the backend answers it with 409.
    const std::vector<SentQrReport> sent = h.flushReports();
    TEST_ASSERT_EQUAL_UINT32(2, sent.size());
    expectReport(sent[0], kJti, qr_reason::kExpired, "denied report");
    expectReport(sent[1], kJti, nullptr, "second granted report");
    TEST_ASSERT_TRUE(sent[1].occurredAt != first[0].occurredAt);
    size_t unlocks = 0;
    for (const fakes::OutputWrite& write : h.output.writes()) unlocks += write.unlocked ? 1 : 0;
    TEST_ASSERT_EQUAL_UINT32(2, unlocks);
  }
}

void test_qr_issued_before_boot_is_rejected() {
  QrHarness h(testQrSettings(), 5000);
  h.sync(kIatFloorMs + 10000);
  TEST_ASSERT_EQUAL_INT64(1792670406125LL, h.serverClock.bootTimeLatestMs());

  expectDenied(h.scan(tokenT()), qr_reason::kIssuedBeforeBoot, true, "token T");
  const std::vector<SentQrReport> sent = h.flushReports();
  TEST_ASSERT_EQUAL_UINT32(1, sent.size());
  expectReport(sent[0], kJti, qr_reason::kIssuedBeforeBoot, "issued_before_boot report");
  TEST_ASSERT_EQUAL_STRING("2026-10-22T12:00:09.623Z", sent[0].occurredAt.c_str());

  expectGranted(h.scan(token(jtiFor(1), 1792670406623456LL)), "issued at 12:00:06.623456");
  // The door booted at about 12:00:05.123. A code issued 500 ms after that
  // is still denied, because of the 1,000 ms margin.
  expectDenied(h.scan(token(jtiFor(2), 1792670405623456LL)), qr_reason::kIssuedBeforeBoot, true,
               "issued at 12:00:05.623456");
}

void test_power_cycle_cannot_replay_used_qr() {
  const std::string text = tokenT();
  QrHarness before;
  before.sync(kSyncMs);
  expectGranted(before.scan(text), "before the reset");

  // The reset loses the used list. The door restarts, and its first poll
  // succeeds 10 s later.
  QrHarness after(testQrSettings(), 10000);
  expectWaiting(after.scan(text), qr_reason::kClockUnsynced, "after the reset, before a poll");
  after.sync(kIatFloorMs + 20000);
  TEST_ASSERT_EQUAL_INT64(1792670411125LL, after.serverClock.bootTimeLatestMs());
  expectDenied(after.scan(text), qr_reason::kIssuedBeforeBoot, true, "after the first poll");
  TEST_ASSERT_TRUE(neverUnlocked(after));
}

void test_expired_qr_is_rejected() {
  // Token E: exp 1792670430.500000, so expMs is 1792670430499.
  const std::string text = mint(headerJson(), claimsJson(kJti, kIatUs, 1792670430500000LL));
  constexpr int64_t kExpiresMs = 1792670430499LL;
  {
    QrHarness h;
    h.sync(kExpiresMs - 2);  // latest = expMs - 1.
    expectGranted(h.scan(text), "latest just before expMs");
  }
  {
    QrHarness h;
    h.sync(kExpiresMs - 1);  // latest = expMs.
    expectDenied(h.scan(text), qr_reason::kExpired, true, "latest at expMs");
    TEST_ASSERT_TRUE(neverUnlocked(h));
    const std::vector<SentQrReport> sent = h.flushReports();
    TEST_ASSERT_EQUAL_UINT32(1, sent.size());
    expectReport(sent[0], kJti, qr_reason::kExpired, "expired report");
    TEST_ASSERT_EQUAL_STRING("2026-10-22T12:00:29.998Z", sent[0].occurredAt.c_str());
  }
  // A 2,000 ms round trip puts latest 2,002 ms after the stamp.
  {
    QrHarness h;
    h.sync(kExpiresMs - 2002, 2000);
    expectDenied(h.scan(text), qr_reason::kExpired, true, "slow poll, latest at expMs");
  }
  {
    QrHarness h;
    h.sync(kExpiresMs - 2003, 2000);
    expectGranted(h.scan(text), "slow poll, latest just before expMs");
  }

  // The signature check takes 40 ms, as it might on the board. The door reads
  // its clock again after the check, so expiry, occurred_at, and the unlock
  // all use the later time, and a code that expires during the check is
  // expired. 40 ms carries latest forward by 41 ms, with 1 ms of drift.
  {
    QrHarness h(testQrSettings(), QrHarness::kDefaultUptimeMs, slowVerifier);
    h.sync(kExpiresMs - 3);  // latest = expMs - 2 when the scan arrives.
    slowVerifierClock = &h.clock;
    const QrDecision expired = h.scan(text);
    const uint32_t checkedAtMs = h.clock.nowMs();
    // The repeat filter counts from when the scan arrived, 40 ms before the
    // check ended, so 2,960 ms later the same text is checked again.
    h.clock.advance(2960);
    const QrDecision again = h.scan(text);
    slowVerifierClock = nullptr;
    expectDenied(expired, qr_reason::kExpired, true, "expires during the check");
    TEST_ASSERT_TRUE(neverUnlocked(h));
    TEST_ASSERT_EQUAL_UINT32(QrHarness::kDefaultUptimeMs + 40, checkedAtMs);
    expectDenied(again, qr_reason::kExpired, false, "3 s after the first scan arrived");
    const std::vector<SentQrReport> sent = h.flushReports();
    TEST_ASSERT_EQUAL_UINT32(1, sent.size());
    // earliest = expMs - 3 + 40 - 1, less 500 ms.
    TEST_ASSERT_EQUAL_STRING("2026-10-22T12:00:30.035Z", sent[0].occurredAt.c_str());
  }
  {
    QrHarness h(testQrSettings(), QrHarness::kDefaultUptimeMs, slowVerifier);
    h.sync(kExpiresMs - 42);  // latest = expMs after the check.
    slowVerifierClock = &h.clock;
    const QrDecision decision = h.scan(text);
    slowVerifierClock = nullptr;
    expectDenied(decision, qr_reason::kExpired, true, "slow check, latest at expMs");
  }
  {
    QrHarness h(testQrSettings(), QrHarness::kDefaultUptimeMs, slowVerifier);
    h.sync(kExpiresMs - 43);  // latest = expMs - 1 after the check.
    slowVerifierClock = &h.clock;
    const QrDecision decision = h.scan(text);
    slowVerifierClock = nullptr;
    expectGranted(decision, "slow check, latest just before expMs");
    const std::vector<SentQrReport> sent = h.flushReports();
    TEST_ASSERT_EQUAL_UINT32(1, sent.size());
    // earliest = expMs - 43 + 40 - 1, less 500 ms.
    TEST_ASSERT_EQUAL_STRING("2026-10-22T12:00:29.995Z", sent[0].occurredAt.c_str());
  }
}

void test_fresh_qr_waits_until_issue_time_has_passed() {
  QrHarness h;
  h.sync(kIatFloorMs + 200);
  const std::string text = tokenT();
  const QrDecision decision = h.scan(text);
  expectWaiting(decision, qr_reason::kNotYetValid, "200 ms after iat");
  TEST_ASSERT_EQUAL_STRING(kJti, decision.credentialId.c_str());
  TEST_ASSERT_TRUE(h.log.contains("QR denied (not_yet_valid) credential " + std::string(kJti)));
  TEST_ASSERT_EQUAL_UINT32(0, h.reports.size());
  TEST_ASSERT_TRUE(neverUnlocked(h));

  // The next decode is checked again: occurred_at would be 12:00:00.122,
  // still before iatMs.
  h.clock.advance(300);
  expectWaiting(h.scan(text), qr_reason::kNotYetValid, "300 ms later");
  h.clock.advance(100);
  expectGranted(h.scan(text), "400 ms later");
  const std::vector<SentQrReport> sent = h.flushReports();
  TEST_ASSERT_EQUAL_UINT32(1, sent.size());
  TEST_ASSERT_EQUAL_STRING("2026-10-22T12:00:00.222Z", sent[0].occurredAt.c_str());
}

void test_qr_unlock_failure_is_reported() {
  QrHarness h;
  h.sync(kSyncMs);
  const std::string text = tokenT();
  h.timer.failStart = true;
  expectDenied(h.scan(text), qr_reason::kUnlockFailed, true, "timer can't arm");
  TEST_ASSERT_TRUE(neverUnlocked(h));

  // The credential wasn't used up, so it works once the timer can arm.
  h.timer.failStart = false;
  h.clock.advance(3100);
  expectGranted(h.scan(text), "timer arms");
  TEST_ASSERT_TRUE(h.output.unlocked());

  const std::vector<SentQrReport> sent = h.flushReports();
  TEST_ASSERT_EQUAL_UINT32(2, sent.size());
  expectReport(sent[0], kJti, qr_reason::kUnlockFailed, "unlock_failed report");
  expectReport(sent[1], kJti, nullptr, "granted report");
}

void test_qr_grant_does_not_extend_unlock() {
  QrHarness h;
  h.sync(kSyncMs);
  expectGranted(h.scan(token(jtiFor(1), kIatUs)), "T1");
  h.clock.advance(1000);
  expectGranted(h.scan(token(jtiFor(2), kIatUs)), "T2 while unlocked");
  TEST_ASSERT_EQUAL_INT(1, h.timer.starts());
  TEST_ASSERT_EQUAL_UINT32(123000, h.timer.deadlineMs());

  h.clock.advance(1999);
  TEST_ASSERT_TRUE(h.output.unlocked());
  h.clock.advance(1);
  TEST_ASSERT_FALSE(h.output.unlocked());
  const std::vector<fakes::OutputWrite>& writes = h.output.writes();
  TEST_ASSERT_EQUAL_UINT32(3, writes.size());
  TEST_ASSERT_EQUAL_UINT32(123000, writes[2].atMs);

  const std::vector<SentQrReport> sent = h.flushReports();
  TEST_ASSERT_EQUAL_UINT32(2, sent.size());
  expectReport(sent[0], jtiFor(1), nullptr, "T1 report");
  expectReport(sent[1], jtiFor(2), nullptr, "T2 report");
}

void test_qr_checks_report_first_failure_in_order() {
  const std::string sub = subJson();
  {
    QrHarness h;
    h.sync(kSyncMs);
    h.serverClock.onPollRefused();
    expectDenied(h.scan("not a token"), qr_reason::kDeviceRefused, false,
                 "device_refused + malformed");
  }
  {
    QrSettings settings = testQrSettings();
    settings.unitId = "";
    QrHarness h(settings);
    h.sync(kSyncMs);
    h.serverClock.onPollRefused();
    expectDenied(h.scan("not a token"), qr_reason::kQrDisabled, false,
                 "qr_disabled + device_refused");
  }
  {
    QrHarness h;
    h.sync(kSyncMs);
    expectDenied(h.scan(forgedFrom(token(jtiFor(1), kIatUs, 60000, kOtherUnit))),
                 qr_reason::kBadSignature, false, "bad signature + other unit");
  }
  {
    QrHarness h;
    h.sync(kSyncMs);
    expectDenied(h.scan(token(jtiFor(2), kExpiredIatUs, 60000, kOtherUnit)),
                 qr_reason::kWrongUnit, false, "other unit + expired");
  }
  {
    QrHarness h;
    expectDenied(h.scan(token(jtiFor(3), kIatUs, 60000, kOtherUnit)), qr_reason::kWrongUnit,
                 false, "other unit + no sync");
    expectWaiting(h.scan(mint(headerJson(), claimsWith(jtiFor(4), "\"evil\"", kAud, sub))),
                  qr_reason::kClockUnsynced, "invalid claims + no sync");
  }
  {
    QrHarness h;
    h.sync(kSyncMs);
    expectDenied(h.scan(mint(headerJson(), claimsWith(jtiFor(5), "\"evil\"", kAud, sub,
                                                      kExpiredIatUs, kExpiredIatUs + 60000000))),
                 qr_reason::kInvalidClaims, true, "invalid claims + expired");
  }
  {
    QrHarness h;
    h.sync(kSyncMs);
    const std::string text = token(jtiFor(6), kIatUs);
    expectGranted(h.scan(text), "grant before revoking");
    TEST_ASSERT_TRUE(h.qr.revoke(jtiFor(6), kExpMs, kSyncMs));
    h.clock.advance(3100);
    expectDenied(h.scan(text), qr_reason::kRevoked, true, "revoked + used");
  }
  {
    QrHarness h(testQrSettings(), 5000);
    h.sync(kIatFloorMs + 70000);  // Boot watermark S - 3,998; latest past T's expiry.
    expectDenied(h.scan(tokenT()), qr_reason::kIssuedBeforeBoot, true, "before boot + expired");
  }
  {
    QrHarness h;
    h.sync(kSyncMs);
    const std::string text = token(jtiFor(7), kIatUs, 30000);
    expectGranted(h.scan(text), "grant a 30 s credential");
    h.clock.advance(31000);
    expectDenied(h.scan(text), qr_reason::kAlreadyUsed, true, "used + earliest past exp");
    h.clock.advance(30000);  // 61 s after the grant, the door forgets the jti.
    expectDenied(h.scan(text), qr_reason::kExpired, false, "forgotten + expired");
  }
}

void test_qr_report_time_is_inside_credential_window() {
  // The server's true time at millis() == 0, with a sub-ms part. Its clock
  // runs at the millis() rate, and it stamps server_time halfway through each
  // request.
  constexpr int64_t kStartUs = 1792670280123456LL;
  const auto trueUsAt = [](uint32_t localMs) {
    return kStartUs + static_cast<int64_t>(localMs) * 1000;
  };
  const uint32_t kRoundTripsMs[] = {0, 300, 3000};
  const uint32_t kScanDelaysMs[] = {0, 1000, 20000};  // After the poll's arrival.
  const int64_t kIssuedAgoMs[] = {600, 1000, 30000, 59000};

  size_t grants = 0;
  size_t denials = 0;
  int n = 0;
  for (const uint32_t roundTripMs : kRoundTripsMs) {
    QrHarness h;
    const uint32_t arrivedMs = h.clock.nowMs();
    h.sync(trueUsAt(arrivedMs - roundTripMs / 2) / 1000, roundTripMs);
    for (const uint32_t delayMs : kScanDelaysMs) {
      h.clock.advance(arrivedMs + delayMs - h.clock.nowMs());
      const int64_t nowUs = trueUsAt(h.clock.nowMs());
      for (const int64_t agoMs : kIssuedAgoMs) {
        const std::string jti = jtiFor(n++);
        const int64_t iatUs = nowUs - agoMs * 1000;
        const int64_t expUs = iatUs + 60000000;
        h.scan(token(jti, iatUs));
        const std::string label = "round trip " + std::to_string(roundTripMs) + " ms, scan " +
                                  std::to_string(delayMs) + " ms after, issued " +
                                  std::to_string(agoMs) + " ms before";
        for (const SentQrReport& report : h.flushReports()) {
          int64_t occurredMs = 0;
          TEST_ASSERT_TRUE_MESSAGE(ostium::parseTimestamp(report.occurredAt.c_str(), &occurredMs),
                                   label.c_str());
          TEST_ASSERT_EQUAL_STRING_MESSAGE(jti.c_str(), report.credentialId.c_str(),
                                           label.c_str());
          TEST_ASSERT_TRUE_MESSAGE(occurredMs * 1000 <= nowUs - 500000, label.c_str());
          if (report.result == "granted") {
            ++grants;
            TEST_ASSERT_TRUE_MESSAGE(occurredMs * 1000 >= iatUs, label.c_str());
            TEST_ASSERT_TRUE_MESSAGE(occurredMs * 1000 < expUs, label.c_str());
          } else {
            ++denials;
          }
        }
      }
    }
  }
  // Both kinds of report were checked.
  TEST_ASSERT_TRUE(grants > 0);
  TEST_ASSERT_TRUE(denials > 0);
}

void test_qr_denial_reported_once_per_credential() {
  {
    QrHarness h;
    h.sync(kSyncMs);
    const std::string expired = token(jtiFor(1), kExpiredIatUs);
    for (int i = 0; i < 5; ++i) {
      expectDenied(h.scan(expired), qr_reason::kExpired, i == 0,
                   "presentation " + std::to_string(i + 1));
      h.clock.advance(3100);
    }
    std::vector<SentQrReport> sent = h.flushReports();
    TEST_ASSERT_EQUAL_UINT32(1, sent.size());
    expectReport(sent[0], jtiFor(1), qr_reason::kExpired, "first credential");

    expectDenied(h.scan(token(jtiFor(2), kExpiredIatUs)), qr_reason::kExpired, true,
                 "second credential");
    sent = h.flushReports();
    TEST_ASSERT_EQUAL_UINT32(1, sent.size());
    expectReport(sent[0], jtiFor(2), qr_reason::kExpired, "second credential");
  }

  // The denial list holds 32 credentials for 120 s each. A 33rd isn't
  // reported until one of them expires.
  QrHarness h;
  h.sync(kSyncMs);
  for (int i = 0; i < 32; ++i) {
    expectDenied(h.scan(token(jtiFor(i), kExpiredIatUs)), qr_reason::kExpired, true,
                 "denial " + std::to_string(i));
    h.flushReports();
  }
  const std::string extra = token(jtiFor(32), kExpiredIatUs);
  expectDenied(h.scan(extra), qr_reason::kExpired, false, "denial list full");
  TEST_ASSERT_TRUE(h.log.contains("QR denial list full; not reporting " + jtiFor(32)));
  TEST_ASSERT_EQUAL_UINT32(0, h.reports.size());
  h.clock.advance(121000);
  expectDenied(h.scan(extra), qr_reason::kExpired, true, "after the list's entries expired");

  // A denial still waiting in the queue isn't queued again, even after the
  // denial list forgets its credential. The list forgets expired entries when
  // it adds one, here for another credential's denial.
  fakes::FakeSender failing;
  failing.fallbackStatus = 503;
  h.reports.service(failing, h.clock);
  TEST_ASSERT_EQUAL_UINT32(1, h.reports.size());
  h.clock.advance(121000);
  expectDenied(h.scan(token(jtiFor(40), kExpiredIatUs)), qr_reason::kExpired, true,
               "another credential");
  expectDenied(h.scan(extra), qr_reason::kExpired, false, "while its report is queued");
  TEST_ASSERT_EQUAL_UINT32(2, h.reports.size());
}

void test_repeated_decodes_are_ignored() {
  {
    QrHarness h;
    h.sync(kSyncMs);
    const std::string text = tokenT();
    expectGranted(h.scan(text), "grant");
    const size_t logLines = h.log.messages.size();
    // The camera decodes about every 300 ms while the code stays in view.
    for (int i = 0; i < 20; ++i) {
      h.clock.advance(300);
      expectIgnored(h.scan(text), qr_reason::kRepeat, "decode " + std::to_string(i + 1));
    }
    TEST_ASSERT_EQUAL_UINT32(logLines, h.log.messages.size());
    TEST_ASSERT_EQUAL_UINT32(1, h.reports.size());
    // The quiet gap slides, so only a 3 s gap with no decode ends it.
    h.clock.advance(3100);
    expectDenied(h.scan(text), qr_reason::kAlreadyUsed, true, "after a 3.1 s gap");
  }
  {
    // Another text in between ends the repeat.
    QrHarness h;
    h.sync(kSyncMs);
    const std::string t = tokenT();
    expectGranted(h.scan(t), "T");
    h.clock.advance(300);
    expectGranted(h.scan(token(jtiFor(1), kIatUs)), "U");
    h.clock.advance(300);
    expectDenied(h.scan(t), qr_reason::kAlreadyUsed, true, "T after U");
  }
  {
    // 2,999 ms is still a repeat. A full 3,000 ms gap ends it.
    QrHarness h;
    h.sync(kSyncMs);
    const std::string text = tokenT();
    expectGranted(h.scan(text), "grant");
    h.clock.advance(2999);
    expectIgnored(h.scan(text), qr_reason::kRepeat, "2,999 ms later");
    h.clock.advance(3000);
    expectDenied(h.scan(text), qr_reason::kAlreadyUsed, true, "3,000 ms after that");
  }
  {
    // Texts over 768 bytes are remembered by their length and first 768
    // bytes. Each is malformed, so that's enough.
    QrHarness h;
    h.sync(kSyncMs);
    const std::string longText(1000, 'A');
    expectDenied(h.scan(longText), qr_reason::kMalformedQr, false, "1,000 bytes");
    expectIgnored(h.scan(longText), qr_reason::kRepeat, "1,000 bytes again");
    expectIgnored(h.scan(longText.substr(0, 768) + std::string(232, 'B')), qr_reason::kRepeat,
                  "same length and first 768 bytes");
    expectDenied(h.scan(longText.substr(0, 900)), qr_reason::kMalformedQr, false, "900 bytes");
    TEST_ASSERT_TRUE(h.log.contains("), 900 bytes"));
  }
}

void test_stale_scan_is_ignored() {
  QrHarness h;
  h.sync(kSyncMs);
  const std::string text = tokenT();
  const uint32_t nowMs = h.clock.nowMs();

  expectIgnored(h.qr.handleScan(text.data(), text.size(), nowMs - 2001), qr_reason::kStaleScan,
                "2,001 ms old");
  TEST_ASSERT_TRUE(h.log.contains("ignored stale QR scan (decoded 2001 ms ago)"));
  expectIgnored(h.qr.handleScan(text.data(), text.size(), nowMs + 1), qr_reason::kStaleScan,
                "stamped after now");
  TEST_ASSERT_TRUE(h.log.contains("ignored stale QR scan (decoded -1 ms ago)"));
  TEST_ASSERT_TRUE(neverUnlocked(h));
  TEST_ASSERT_EQUAL_UINT32(0, h.reports.size());

  expectGranted(h.qr.handleScan(text.data(), text.size(), nowMs - 2000), "2,000 ms old");

  // A stale decode doesn't touch the repeat memory, so it can't stretch the
  // quiet gap.
  h.clock.advance(3100);
  expectIgnored(h.qr.handleScan(text.data(), text.size(), h.clock.nowMs() - 2500),
                qr_reason::kStaleScan, "stale after the gap");
  expectDenied(h.scan(text), qr_reason::kAlreadyUsed, true, "fresh after the gap");
}

void test_qr_scan_drain_is_bounded() {
  QrHarness h;
  h.sync(kSyncMs);
  qrfakes::FakeScanSource source;
  for (size_t length = 1; length <= 10; ++length) {
    source.scans.push_back({std::string(length, '!'), h.clock.nowMs()});
  }
  const size_t logLines = h.log.messages.size();

  TEST_ASSERT_EQUAL_UINT32(4, ostium::drainScans(source, h.qr, 4));
  TEST_ASSERT_EQUAL_UINT32(6, source.scans.size());
  TEST_ASSERT_EQUAL_UINT32(logLines + 4, h.log.messages.size());

  // The default limit is 4 too, and later drains take the rest.
  TEST_ASSERT_EQUAL_UINT32(4, ostium::drainScans(source, h.qr));
  TEST_ASSERT_EQUAL_UINT32(2, ostium::drainScans(source, h.qr));
  TEST_ASSERT_EQUAL_UINT32(0, ostium::drainScans(source, h.qr));

  // Each scan was decided in order, one log line each.
  TEST_ASSERT_EQUAL_UINT32(logLines + 10, h.log.messages.size());
  for (size_t i = 0; i < 10; ++i) {
    const std::string& message = h.log.messages[logLines + i];
    const std::string ending = "), " + std::to_string(i + 1) + " bytes";
    TEST_ASSERT_EQUAL_UINT32_MESSAGE(0, message.find("QR denied (malformed_qr: "),
                                     message.c_str());
    TEST_ASSERT_TRUE_MESSAGE(message.size() > ending.size() &&
                                 message.compare(message.size() - ending.size(), ending.size(),
                                                 ending) == 0,
                             message.c_str());
  }
}

void test_qr_failed_report_does_not_block_scans() {
  QrHarness h;
  h.sync(kSyncMs);
  expectGranted(h.scan(token(jtiFor(1), kIatUs)), "T1");

  // The report post blocks for 10 s, then fails.
  fakes::BlockingSender blocking(h.clock, 10000);
  TEST_ASSERT_TRUE(h.reports.service(blocking, h.clock));
  TEST_ASSERT_EQUAL_UINT32(130000, h.clock.nowMs());
  TEST_ASSERT_EQUAL_UINT32(1, h.reports.size());

  // The relock fired on time while the post blocked.
  const std::vector<fakes::OutputWrite>& writes = h.output.writes();
  TEST_ASSERT_EQUAL_UINT32(3, writes.size());
  TEST_ASSERT_FALSE(writes[2].unlocked);
  TEST_ASSERT_EQUAL_UINT32(123000, writes[2].atMs);

  // A scan right after the failed post is decided at once.
  expectGranted(h.scan(token(jtiFor(2), kIatUs)), "T2 after the failed post");
  TEST_ASSERT_TRUE(h.output.unlocked());
  TEST_ASSERT_EQUAL_UINT32(130000, h.output.writes()[3].atMs);
  TEST_ASSERT_EQUAL_UINT32(2, h.reports.size());

  // While the backend stays down, grants fill the queue, and a denial never
  // evicts one.
  for (int i = 3; i <= 8; ++i) {
    expectGranted(h.scan(token(jtiFor(i), kIatUs)), "grant " + std::to_string(i));
  }
  TEST_ASSERT_EQUAL_UINT32(ostium::EventOutbox::kCapacity, h.reports.size());
  expectDenied(h.scan(token(jtiFor(9), kExpiredIatUs)), qr_reason::kExpired, false,
               "denial with a queue full of grants");
  TEST_ASSERT_TRUE(h.log.contains("didn't queue " + ostium::qrEventIdFor(jtiFor(9), false)));
  for (int i = 1; i <= 8; ++i) {
    TEST_ASSERT_TRUE(h.reports.contains(ostium::qrEventIdFor(jtiFor(i), true)));
  }
}

void test_qr_log_never_contains_token() {
  QrHarness h;
  h.sync(kSyncMs);
  const std::string valid = tokenT();
  const std::string forged = forgedFrom(token(jtiFor(1), kIatUs));
  const std::string malformed = token(jtiFor(2), kIatUs);
  const std::string otherUnit = token(jtiFor(3), kIatUs, 60000, kOtherUnit);
  const std::string invalid =
      mint(headerJson(), claimsWith(jtiFor(4), "\"evil\"", kAud, subJson()));
  const std::string expired = token(jtiFor(5), kExpiredIatUs);

  h.scan(valid);
  h.scan(forged);
  h.scan(malformed + "\n");
  h.scan(otherUnit);
  h.scan(invalid);
  h.scan(expired);
  h.qr.handleScan(valid.data(), valid.size(), h.clock.nowMs() - 5000);
  h.clock.advance(3100);
  h.scan(valid);
  h.flushReports();

  // The scans were logged.
  TEST_ASSERT_TRUE(h.log.contains("QR granted credential"));
  TEST_ASSERT_TRUE(h.log.contains("QR denied (bad_signature"));
  TEST_ASSERT_TRUE(h.log.contains("QR denied (malformed_qr"));
  TEST_ASSERT_TRUE(h.log.contains("QR denied (expired) credential"));
  TEST_ASSERT_TRUE(h.log.contains("ignored stale QR scan"));

  for (const std::string& text : {valid, forged, malformed, otherUnit, invalid, expired}) {
    const Segments parts = split(text);
    for (const std::string& segment : {parts.payload, parts.signature}) {
      const std::string middle = segment.substr(segment.size() / 2 - 8, 16);
      for (const std::string& message : h.log.messages) {
        TEST_ASSERT_TRUE_MESSAGE(message.find(segment) == std::string::npos, message.c_str());
        TEST_ASSERT_TRUE_MESSAGE(message.find(middle) == std::string::npos, message.c_str());
      }
    }
  }
  for (const std::string& message : h.log.messages) {
    TEST_ASSERT_TRUE_MESSAGE(message.find(kUser) == std::string::npos, message.c_str());
  }
}
