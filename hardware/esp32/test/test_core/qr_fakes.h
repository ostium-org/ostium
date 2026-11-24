#pragma once

// QR helpers for the ostium_core tests: the test keys, the fixture's values,
// token builders that sign the way the backend's signJWT does, and QR access
// wired to fakes. Nothing here touches hardware or the network.

#include <monocypher-ed25519.h>

#include <array>
#include <cstdint>
#include <deque>
#include <string>
#include <utility>
#include <vector>

#include "fakes.h"
#include "qr_backend_responses.h"

namespace qrfakes {

constexpr const char* kUnit = "22222222-2222-4222-8222-222222222222";
constexpr const char* kOtherUnit = "44444444-4444-4444-8444-444444444444";
constexpr const char* kUser = "11111111-1111-4111-8111-111111111111";
constexpr const char* kJti = "55555555-5555-4555-8555-555555555555";
// The fixture's iat, 2026-10-22T12:00:00.123456Z: in microseconds, truncated
// to ms, and as Q6 rounds it.
constexpr int64_t kIatUs = 1792670400123456LL;
constexpr int64_t kIatFloorMs = 1792670400123LL;
constexpr int64_t kIatMs = 1792670400125LL;

// Decodes lowercase or uppercase hex. Ignores a trailing odd digit.
inline std::vector<uint8_t> fromHex(const std::string& hex) {
  std::vector<uint8_t> bytes;
  for (size_t i = 0; i + 1 < hex.size(); i += 2) {
    bytes.push_back(static_cast<uint8_t>(std::stoi(hex.substr(i, 2), nullptr, 16)));
  }
  return bytes;
}

// TEST ONLY: the Ed25519 seed of the backend fixture's private key.
inline std::array<uint8_t, 32> testSeed() {
  const std::vector<uint8_t> bytes = fromHex(qr_backend::kTestOnlySeedHex);
  std::array<uint8_t, 32> seed{};
  for (size_t i = 0; i < seed.size() && i < bytes.size(); ++i) seed[i] = bytes[i];
  return seed;
}

// A second key, which nothing trusts.
inline std::array<uint8_t, 32> otherSeed() {
  std::array<uint8_t, 32> seed{};
  seed.fill(0x42);
  return seed;
}

// The base64url public key for seed. Monocypher wipes the seed it's given, so
// it gets a copy.
inline std::string publicKeyFor(const std::array<uint8_t, 32>& seed) {
  std::array<uint8_t, 32> copy = seed;
  uint8_t secretKey[64];
  uint8_t publicKey[32];
  crypto_ed25519_key_pair(secretKey, publicKey, copy.data());
  return ostium::encodeBase64Url(publicKey, sizeof(publicKey));
}

// A distinct lowercase jti, "00000000-0000-4000-8000-0000000000NN", for n
// from 0 to 99.
inline std::string jtiFor(int n) {
  const std::string digits = std::to_string(n % 100);
  return "00000000-0000-4000-8000-0000000000" + std::string(2 - digits.size(), '0') + digits;
}

// Microseconds since the epoch as JSON seconds, "<s>.<6 digits>".
inline std::string epochSeconds(int64_t us) {
  const std::string fraction = std::to_string(us % 1000000);
  return std::to_string(us / 1000000) + "." + std::string(6 - fraction.size(), '0') + fraction;
}

// The header bytes signJWT writes: JSON.stringify({alg, typ, kid}).
inline std::string headerJson(const std::string& kid = "ostium-qr-1") {
  return "{\"alg\":\"EdDSA\",\"typ\":\"JWT\",\"kid\":\"" + kid + "\"}";
}

// Claims in signJWT's key order. extraMembers, when set, goes after exp.
inline std::string claimsJson(const std::string& jti, int64_t iatUs, int64_t expUs,
                              const std::string& unit = kUnit,
                              const std::string& extraMembers = "") {
  return "{\"iss\":\"ostium\",\"aud\":\"ostium-devices\",\"sub\":\"" + std::string(kUser) +
         "\",\"unit_id\":\"" + unit + "\",\"jti\":\"" + jti + "\",\"iat\":" + epochSeconds(iatUs) +
         ",\"exp\":" + epochSeconds(expUs) + (extraMembers.empty() ? "" : "," + extraMembers) +
         "}";
}

// Claims with exactly these members, for type and missing-claim tests.
inline std::string rawClaims(const std::string& members) { return "{" + members + "}"; }

// Signs header.payload, given as base64url segments, and returns the compact
// token. For segments that encodeBase64Url would never write.
inline std::string signSegments(const std::string& header, const std::string& payload,
                                const std::array<uint8_t, 32>& seed = testSeed()) {
  const std::string signingInput = header + "." + payload;
  std::array<uint8_t, 32> copy = seed;
  uint8_t secretKey[64];
  uint8_t publicKey[32];
  crypto_ed25519_key_pair(secretKey, publicKey, copy.data());
  uint8_t signature[64];
  crypto_ed25519_sign(signature, secretKey, reinterpret_cast<const uint8_t*>(signingInput.data()),
                      signingInput.size());
  return signingInput + "." + ostium::encodeBase64Url(signature, sizeof(signature));
}

// Encodes and signs a header and claims, like the backend's signJWT.
inline std::string mint(const std::string& header, const std::string& claims,
                        const std::array<uint8_t, 32>& seed = testSeed()) {
  return signSegments(ostium::encodeBase64Url(header), ostium::encodeBase64Url(claims), seed);
}

// A token for this unit with the backend's header and claims.
inline std::string token(const std::string& jti, int64_t iatUs, int64_t lifetimeMs = 60000,
                         const std::string& unit = kUnit) {
  return mint(headerJson(), claimsJson(jti, iatUs, iatUs + lifetimeMs * 1000, unit));
}

// This unit, the fixture's key under ostium-qr-1, and the test key allowed,
// as in the bench build.
inline ostium::QrSettings testQrSettings() {
  ostium::QrSettings settings;
  settings.unitId = kUnit;
  settings.publicKey = ostium::kTestOnlyQrPublicKey;
  settings.keyId = "ostium-qr-1";
  settings.allowTestKey = true;
  return settings;
}

struct SentQrReport {
  std::string eventId;
  std::string credentialId;
  std::string result;
  std::string reason;  // "null" when the reason is null.
  std::string occurredAt;
};

inline SentQrReport parseQrReportBody(const std::string& body) {
  JsonDocument document;
  deserializeJson(document, body);
  JsonObjectConst root = document.as<JsonObjectConst>();
  return {fakes::stringOrNull(root["event_id"]), fakes::stringOrNull(root["credential_id"]),
          fakes::stringOrNull(root["result"]), fakes::stringOrNull(root["reason"]),
          fakes::stringOrNull(root["occurred_at"])};
}

// Scans waiting in order, like the camera's queue.
class FakeScanSource : public ostium::ScanSource {
 public:
  bool next(ostium::QrScan* scan) override {
    if (scans.empty()) return false;
    *scan = std::move(scans.front());
    scans.pop_front();
    return true;
  }

  std::deque<ostium::QrScan> scans;
};

// QR access wired to fakes. The door has been up for uptimeMs when the test
// starts. With the 120 s default, the first sync puts the boot watermark
// about 119 s before the server time, so tokens issued shortly before a sync
// pass Q14. verify is the signature check that begin()'s self-test and every
// scan run.
struct QrHarness {
  static constexpr uint32_t kDefaultUptimeMs = 120000;

  explicit QrHarness(const ostium::QrSettings& settings = testQrSettings(),
                     uint32_t uptimeMs = kDefaultUptimeMs,
                     ostium::Ed25519Verifier verify = ostium::verifyEd25519)
      : qr{lock, reports, serverClock, clock, log, verify} {
    clock.set(uptimeMs);
    lock.begin();
    qr.begin(settings);
  }

  // A good poll whose response arrives now, rttMs after its request left.
  void sync(int64_t serverTimeMs, uint32_t rttMs = 0) {
    serverClock.onPollSucceeded(serverTimeMs, clock.nowMs() - rttMs, clock.nowMs());
  }

  // A scan decoded just now.
  ostium::QrDecision scan(const std::string& text) {
    return qr.handleScan(text.data(), text.size(), clock.nowMs());
  }

  // Posts every queued report with a 200 response and returns them in order.
  std::vector<SentQrReport> flushReports() {
    fakes::FakeSender sender;
    for (int guard = 0; reports.size() > 0 && guard < 100; ++guard) {
      reports.service(sender, clock);
    }
    std::vector<SentQrReport> sent;
    for (const std::string& body : sender.bodies) sent.push_back(parseQrReportBody(body));
    return sent;
  }

  fakes::FakeClock clock;
  fakes::FakeTimer timer{clock};
  fakes::FakeLockOutput output{clock};
  fakes::FakeLog log;
  ostium::EventOutbox reports{log, ostium::ReportKind::Qr};
  ostium::LockController lock{output, timer};
  ostium::ServerClock serverClock{log};
  ostium::QrAccess qr;
};

}  // namespace qrfakes
