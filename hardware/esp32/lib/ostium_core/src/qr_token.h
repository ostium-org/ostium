#pragma once

// Signed QR credential tokens (Q2 to Q6): the compact JWT text that the
// backend's signJWT writes, checked against one configured Ed25519 key.

#include <cstddef>
#include <cstdint>
#include <string>

#include "ed25519.h"

namespace ostium {

constexpr size_t kMaxQrTokenBytes = 768;
constexpr size_t kMaxQrPayloadChars = 512;
constexpr size_t kQrSignatureChars = 86;
constexpr size_t kMaxQrKeyIdLength = 64;
constexpr double kMinQrEpochSeconds = 1577836800.0;  // 2020-01-01T00:00:00Z
constexpr double kMaxQrEpochSeconds = 4102444800.0;  // 2100-01-01T00:00:00Z

struct QrKey {
  std::string keyId;
  Ed25519PublicKey publicKey{};
  // base64url of {"alg":"EdDSA","typ":"JWT","kid":"<keyId>"}: the bytes
  // JSON.stringify produces in signJWT.
  std::string expectedHeader;
};

// Builds a key. Returns false, with *error set to a static message and *out
// unchanged, when:
// - keyId is empty, over 64 chars, or has a char outside 0x21-0x7E, or '"' or '\\';
// - publicKeyBase64Url isn't exactly 43 chars that strictly decode to 32 bytes;
// - the key is all zero bytes, or any other point of small order. Under such a
//   key, one fixed signature verifies over every message, so anyone could
//   forge a token. Finding out costs one Ed25519 check.
// The keyId character set is exactly what JSON.stringify leaves unescaped, so
// expectedHeader equals the backend's bytes.
bool makeQrKey(const std::string& keyId, const std::string& publicKeyBase64Url, QrKey* out,
               const char** error);

// True for 36 chars in the 8-4-4-4-12 layout with hex digits of either case.
// Writes the lowercase form to *lowercaseOut, only on success.
bool canonicalUuid(const std::string& text, std::string* lowercaseOut);

// Q6. Inward rounding, so expiry is never rounded up and issue time is never
// rounded down. 1 ms beyond the ceil and floor covers ArduinoJson's float parse
// error, which is under 1 µs at these magnitudes.
int64_t issuedAtMsFromSeconds(double seconds);   // ceil(seconds * 1000) + 1
int64_t expiresAtMsFromSeconds(double seconds);  // floor(seconds * 1000) - 1

enum class QrTokenProblem { None, Malformed, UnsupportedHeader, BadSignature, MalformedClaims };

struct QrClaims {
  std::string credentialId;     // jti, lowercase canonical UUID
  std::string unitId;           // unit_id, lowercase canonical UUID
  int64_t issuedAtMs = 0;       // issuedAtMsFromSeconds(iat)
  int64_t expiresAtMs = 0;      // expiresAtMsFromSeconds(exp)
  bool hasIssuer = false;       // iss is a JSON string with no NUL
  std::string issuer;
  bool hasAudience = false;     // aud is a JSON string with no NUL (an array leaves it false)
  std::string audience;
  bool hasSubject = false;      // sub is a non-empty JSON string with no NUL
  bool hasOtherClaims = false;  // a member other than the seven
};

struct QrToken {
  QrTokenProblem problem = QrTokenProblem::Malformed;
  const char* detail = "";  // static text for the log; never token content
  QrClaims claims;          // meaningful only when problem == None
};

// Checks the text's shape (Q2), pins the header byte for byte (Q3), verifies
// the Ed25519 signature over the received header.payload text (Q4), and only
// then decodes and parses the claims (Q5, Q6). The first failure wins. The
// caller checks the claim values, the unit, and the time. verify is the
// signature check; a null one fails every token with BadSignature.
QrToken verifyQrToken(const char* text, size_t length, const QrKey& key,
                      Ed25519Verifier verify = verifyEd25519);

}  // namespace ostium
