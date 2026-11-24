#include "ed25519.h"

// The only core file that includes Monocypher. The board uses nothing else
// from it.
#include <monocypher-ed25519.h>

namespace ostium {

bool verifyEd25519(const uint8_t signature[kEd25519SignatureBytes],
                   const Ed25519PublicKey& publicKey, const uint8_t* message, size_t length) {
  return crypto_ed25519_check(signature, publicKey.data(), message, length) == 0;
}

}  // namespace ostium
