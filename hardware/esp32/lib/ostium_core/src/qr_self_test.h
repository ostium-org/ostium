#pragma once

// The startup known-answer test for QR access (Q11). QrAccess::begin() runs it
// on the target, so the board proves its own verifier before it trusts a scan.

#include <cstdint>

#include "ed25519.h"
#include "interfaces.h"

namespace ostium {

// True only if all of these hold:
// - token verifies with publicKeyBase64Url under kQrSelfTestKeyId, with problem
//   None, jti kQrSelfTestCredentialId, unit kQrSelfTestUnitId, and exactly
//   kQrSelfTestIssuedAtMs and kQrSelfTestExpiresAtMs;
// - a copy with payload char [headerLength + 1 + 10] swapped ('A' <-> 'B',
//   otherwise to 'A') gives BadSignature;
// - a copy with signature char [length - 40] swapped the same way gives BadSignature.
// Runs the full verifyQrToken path, so it also checks the ArduinoJson parse and
// the Q6 rounding on the target.
//
// verify is the signature check under test. The firmware passes verifyEd25519;
// the tests pass broken ones, which the two tampered copies must catch. When
// clock and checkMs are both set, *checkMs gets how long the fixture's own
// verifyQrToken call took: one Ed25519 check and the claims parse, which is
// what one scan costs. The tampered copies aren't timed.
bool runQrSelfTest(const char* token, const char* publicKeyBase64Url,
                   Ed25519Verifier verify = verifyEd25519, const Clock* clock = nullptr,
                   uint32_t* checkMs = nullptr);

}  // namespace ostium
