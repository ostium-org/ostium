#include "qr_token.h"

#include <ArduinoJson.h>

#include <cmath>
#include <cstring>

#include "base64url.h"

namespace ostium {
namespace {

constexpr size_t kPublicKeyChars = 43;

bool isTokenCharacter(char c) {
  return (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '-' ||
         c == '_' || c == '.';
}

bool isHexDigit(char c) {
  return (c >= '0' && c <= '9') || (c >= 'a' && c <= 'f') || (c >= 'A' && c <= 'F');
}

// Reads a JSON string. Rejects strings with an embedded NUL character, which
// would otherwise compare as a shorter string. A copy of the one in
// poll_parser.cpp.
bool readString(JsonVariantConst value, std::string* out) {
  if (!value.is<JsonString>()) return false;
  const JsonString text = value.as<JsonString>();
  if (std::memchr(text.c_str(), '\0', text.size()) != nullptr) return false;
  out->assign(text.c_str(), text.size());
  return true;
}

// A jti or unit_id: a string that's already a lowercase canonical UUID.
bool readUuid(JsonVariantConst value, std::string* out) {
  std::string text;
  std::string lowercase;
  if (!readString(value, &text) || !canonicalUuid(text, &lowercase) || lowercase != text) {
    return false;
  }
  *out = text;
  return true;
}

// Compares lengths too, so a key with an embedded NUL never matches.
bool isKnownClaim(JsonString key) {
  static const char* const kClaims[] = {"iss", "aud", "sub", "unit_id", "jti", "iat", "exp"};
  for (const char* claim : kClaims) {
    const size_t length = std::strlen(claim);
    if (key.size() == length && std::memcmp(key.c_str(), claim, length) == 0) return true;
  }
  return false;
}

// Why keyId can't go in the header, or nullptr. The allowed characters are
// exactly the printable ones that JSON.stringify writes unescaped, minus space.
const char* keyIdProblem(const std::string& keyId) {
  if (keyId.empty()) return "key ID is empty";
  if (keyId.size() > kMaxQrKeyIdLength) return "key ID is over 64 characters";
  for (const char c : keyId) {
    const unsigned char byte = static_cast<unsigned char>(c);
    if (byte < 0x21 || byte > 0x7E || c == '"' || c == '\\') {
      return "key ID must be printable ASCII with no space, quote, or backslash";
    }
  }
  return nullptr;
}

bool isAllZero(const std::string& bytes) {
  for (const char c : bytes) {
    if (c != '\0') return false;
  }
  return true;
}

// True when publicKey is a point of small order, in any encoding Monocypher
// accepts. Monocypher checks the cofactored equation [8]([S]B - R - [h]A) = 0.
// For such a key, [8][h]A is the identity whatever h is, so the fixed
// signature R = B (encoded 58 66 ... 66), S = 1 verifies over every message,
// and anyone could forge a token. For a key with a prime-order part, the probe
// would need h = 0 mod L, which SHA-512 can't be made to give.
bool hasSmallOrder(const Ed25519PublicKey& publicKey) {
  uint8_t probe[kEd25519SignatureBytes] = {0x58};
  for (size_t i = 1; i < 32; ++i) probe[i] = 0x66;
  probe[32] = 0x01;
  const uint8_t message = 0;
  return verifyEd25519(probe, publicKey, &message, 0);
}

QrToken fail(QrTokenProblem problem, const char* detail) {
  QrToken token;
  token.problem = problem;
  token.detail = detail;
  return token;
}

}  // namespace

bool makeQrKey(const std::string& keyId, const std::string& publicKeyBase64Url, QrKey* out,
               const char** error) {
  std::string keyBytes;
  const char* problem = keyIdProblem(keyId);
  if (problem == nullptr &&
      (publicKeyBase64Url.size() != kPublicKeyChars ||
       !decodeBase64Url(publicKeyBase64Url.data(), publicKeyBase64Url.size(), &keyBytes) ||
       keyBytes.size() != 32)) {
    problem = "public key must be 43 base64url characters (32 bytes)";
  }
  Ed25519PublicKey publicKey{};
  if (problem == nullptr) {
    std::memcpy(publicKey.data(), keyBytes.data(), publicKey.size());
    if (isAllZero(keyBytes)) {
      problem = "public key is all zero bytes";
    } else if (hasSmallOrder(publicKey)) {
      problem = "public key is a small-order point";
    }
  }
  if (problem != nullptr) {
    if (error != nullptr) *error = problem;
    return false;
  }

  QrKey key;
  key.keyId = keyId;
  key.publicKey = publicKey;
  key.expectedHeader =
      encodeBase64Url("{\"alg\":\"EdDSA\",\"typ\":\"JWT\",\"kid\":\"" + keyId + "\"}");
  *out = key;
  return true;
}

bool canonicalUuid(const std::string& text, std::string* lowercaseOut) {
  if (text.size() != 36) return false;
  std::string lowercase = text;
  for (size_t i = 0; i < lowercase.size(); ++i) {
    char& c = lowercase[i];
    if (i == 8 || i == 13 || i == 18 || i == 23) {
      if (c != '-') return false;
    } else if (!isHexDigit(c)) {
      return false;
    } else if (c >= 'A' && c <= 'F') {
      c = static_cast<char>(c - 'A' + 'a');
    }
  }
  if (lowercaseOut != nullptr) *lowercaseOut = lowercase;
  return true;
}

int64_t issuedAtMsFromSeconds(double seconds) {
  return static_cast<int64_t>(std::ceil(seconds * 1000.0)) + 1;
}

int64_t expiresAtMsFromSeconds(double seconds) {
  return static_cast<int64_t>(std::floor(seconds * 1000.0)) - 1;
}

QrToken verifyQrToken(const char* text, size_t length, const QrKey& key,
                      Ed25519Verifier verify) {
  // 1. Shape (Q2). One pass checks the characters and finds the two dots.
  if (length == 0 || text == nullptr) return fail(QrTokenProblem::Malformed, "text is empty");
  if (length > kMaxQrTokenBytes) {
    return fail(QrTokenProblem::Malformed, "text is over 768 bytes");
  }
  size_t dots = 0;
  size_t firstDot = 0;
  size_t secondDot = 0;
  for (size_t i = 0; i < length; ++i) {
    if (!isTokenCharacter(text[i])) {
      return fail(QrTokenProblem::Malformed, "text has a byte outside base64url and '.'");
    }
    if (text[i] != '.') continue;
    if (dots == 0) firstDot = i;
    if (dots == 1) secondDot = i;
    ++dots;
  }
  if (dots != 2) return fail(QrTokenProblem::Malformed, "text doesn't have exactly two dots");
  const size_t headerLength = firstDot;
  const size_t payloadLength = secondDot - firstDot - 1;
  const size_t signatureLength = length - secondDot - 1;
  if (headerLength == 0) return fail(QrTokenProblem::Malformed, "header is empty");
  if (payloadLength == 0) return fail(QrTokenProblem::Malformed, "payload is empty");
  if (payloadLength > kMaxQrPayloadChars) {
    return fail(QrTokenProblem::Malformed, "payload is over 512 characters");
  }
  if (payloadLength % 4 == 1) {
    return fail(QrTokenProblem::Malformed, "payload length isn't valid base64url");
  }
  if (signatureLength != kQrSignatureChars) {
    return fail(QrTokenProblem::Malformed, "signature isn't 86 characters");
  }

  // 2. Header (Q3). The unsigned header is compared, never parsed.
  if (key.expectedHeader.empty() || headerLength != key.expectedHeader.size() ||
      std::memcmp(text, key.expectedHeader.data(), headerLength) != 0) {
    return fail(QrTokenProblem::UnsupportedHeader, "header doesn't match the configured key");
  }

  // 3. Signature encoding (Q2).
  std::string signature;
  if (!decodeBase64Url(text + secondDot + 1, signatureLength, &signature) ||
      signature.size() != kEd25519SignatureBytes) {
    return fail(QrTokenProblem::Malformed, "signature isn't valid base64url");
  }

  // 4. Signature (Q4), over the received ASCII up to the second dot.
  if (verify == nullptr ||
      !verify(reinterpret_cast<const uint8_t*>(signature.data()), key.publicKey,
              reinterpret_cast<const uint8_t*>(text), secondDot)) {
    return fail(QrTokenProblem::BadSignature, "signature doesn't verify");
  }

  // 5. Claims (Q5), decoded only now that the signature has passed. ArduinoJson
  // merges duplicate keys, but only the backend's signed output gets here.
  std::string bytes;
  if (!decodeBase64Url(text + firstDot + 1, payloadLength, &bytes)) {
    return fail(QrTokenProblem::MalformedClaims, "claims aren't valid base64url");
  }
  JsonDocument document;
  if (deserializeJson(document, static_cast<const char*>(bytes.data()), bytes.size())) {
    return fail(QrTokenProblem::MalformedClaims, "claims aren't valid JSON");
  }
  if (!document.is<JsonObjectConst>()) {
    return fail(QrTokenProblem::MalformedClaims, "claims aren't a JSON object");
  }
  const JsonObjectConst root = document.as<JsonObjectConst>();

  QrToken token;
  QrClaims& claims = token.claims;
  if (!readUuid(root["jti"], &claims.credentialId)) {
    return fail(QrTokenProblem::MalformedClaims, "jti isn't a lowercase UUID string");
  }
  if (!readUuid(root["unit_id"], &claims.unitId)) {
    return fail(QrTokenProblem::MalformedClaims, "unit_id isn't a lowercase UUID string");
  }
  // is<double>() is true for any JSON number, and false for a bool, a string,
  // or null. as<double>() would convert a string, so the type check comes first.
  const JsonVariantConst iat = root["iat"];
  const JsonVariantConst exp = root["exp"];
  if (!iat.is<double>()) return fail(QrTokenProblem::MalformedClaims, "iat isn't a number");
  if (!exp.is<double>()) return fail(QrTokenProblem::MalformedClaims, "exp isn't a number");
  const double iatSeconds = iat.as<double>();
  const double expSeconds = exp.as<double>();
  if (!std::isfinite(iatSeconds) || iatSeconds < kMinQrEpochSeconds ||
      iatSeconds > kMaxQrEpochSeconds) {
    return fail(QrTokenProblem::MalformedClaims, "iat is outside 2020 to 2100");
  }
  if (!std::isfinite(expSeconds) || expSeconds < kMinQrEpochSeconds ||
      expSeconds > kMaxQrEpochSeconds) {
    return fail(QrTokenProblem::MalformedClaims, "exp is outside 2020 to 2100");
  }

  // 6. The claim values, for the caller's checks (Q6, Q9).
  claims.issuedAtMs = issuedAtMsFromSeconds(iatSeconds);
  claims.expiresAtMs = expiresAtMsFromSeconds(expSeconds);
  claims.hasIssuer = readString(root["iss"], &claims.issuer);
  claims.hasAudience = readString(root["aud"], &claims.audience);
  std::string subject;
  claims.hasSubject = readString(root["sub"], &subject) && !subject.empty();
  for (const JsonPairConst member : root) {
    if (!isKnownClaim(member.key())) {
      claims.hasOtherClaims = true;
      break;
    }
  }
  token.problem = QrTokenProblem::None;
  token.detail = "";
  return token;
}

}  // namespace ostium
