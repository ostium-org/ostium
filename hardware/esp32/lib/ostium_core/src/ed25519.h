#pragma once

// Ed25519 signature checks for QR credentials (Q4). The code is the vendored
// Monocypher 4.0.2 in lib/monocypher, so the board and the native tests run
// the same verifier.

#include <array>
#include <cstddef>
#include <cstdint>

namespace ostium {

using Ed25519PublicKey = std::array<uint8_t, 32>;
constexpr size_t kEd25519SignatureBytes = 64;

// Checks an RFC 8032 Ed25519 (SHA-512) signature over message, with
// crypto_ed25519_check(...) == 0. Returns false for a non-canonical signature
// (S >= L), which test_ed25519_rejects_non_canonical_signature proves. If that
// test fails against a newer Monocypher release, add an explicit little-endian
// S < L comparison here before calling Monocypher.
bool verifyEd25519(const uint8_t signature[kEd25519SignatureBytes],
                   const Ed25519PublicKey& publicKey, const uint8_t* message, size_t length);

// The signature check that verifyQrToken, the startup self-test, and QrAccess
// run. The firmware always uses verifyEd25519. Only the tests pass others:
// broken ones, to prove that the self-test catches them and that QR access
// then stays off (Q11), and a slow one, to prove that the door checks expiry
// against the time after the check (Q15).
using Ed25519Verifier = bool (*)(const uint8_t* signature, const Ed25519PublicKey& publicKey,
                                 const uint8_t* message, size_t length);

}  // namespace ostium
