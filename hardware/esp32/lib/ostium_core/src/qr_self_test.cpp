#include "qr_self_test.h"

#include <cstring>
#include <string>

#include "qr_test_vector.h"
#include "qr_token.h"

namespace ostium {
namespace {

// 'A' and 'B' swap, and any other character becomes 'A', so the copy always
// differs from the original in exactly one base64url character.
std::string withSwappedCharacter(const std::string& token, size_t index) {
  std::string copy = token;
  char& c = copy[index];
  c = c == 'A' ? 'B' : 'A';
  return copy;
}

bool hasBadSignature(const std::string& token, const QrKey& key, Ed25519Verifier verify) {
  return verifyQrToken(token.data(), token.size(), key, verify).problem ==
         QrTokenProblem::BadSignature;
}

}  // namespace

bool runQrSelfTest(const char* token, const char* publicKeyBase64Url, Ed25519Verifier verify,
                   const Clock* clock, uint32_t* checkMs) {
  if (token == nullptr || publicKeyBase64Url == nullptr) return false;
  QrKey key;
  if (!makeQrKey(kQrSelfTestKeyId, publicKeyBase64Url, &key, nullptr)) return false;

  // Only this call is timed. A scan runs it once, so its time is what one
  // scan costs. The tampered copies below each run a full check too.
  const std::string text(token, std::strlen(token));
  const uint32_t startedMs = clock != nullptr ? clock->nowMs() : 0;
  const QrToken result = verifyQrToken(text.data(), text.size(), key, verify);
  if (clock != nullptr && checkMs != nullptr) *checkMs = clock->nowMs() - startedMs;
  if (result.problem != QrTokenProblem::None ||
      result.claims.credentialId != kQrSelfTestCredentialId ||
      result.claims.unitId != kQrSelfTestUnitId ||
      result.claims.issuedAtMs != kQrSelfTestIssuedAtMs ||
      result.claims.expiresAtMs != kQrSelfTestExpiresAtMs) {
    return false;
  }

  // The token passed, so it has a header, a payload of at least 1 char, and an
  // 86-char signature. The payload holds every claim, so it's far longer than
  // 11 chars, but check anyway rather than change the wrong segment. These two
  // copies are what catch a verifier that ignores the message or the signature.
  const size_t payloadIndex = text.find('.') + 1 + 10;
  if (payloadIndex >= text.rfind('.')) return false;
  return hasBadSignature(withSwappedCharacter(text, payloadIndex), key, verify) &&
         hasBadSignature(withSwappedCharacter(text, text.size() - 40), key, verify);
}

}  // namespace ostium
