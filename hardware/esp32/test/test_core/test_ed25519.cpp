// Ed25519 through the vendored Monocypher: Q4.

#include <monocypher-ed25519.h>
#include <unity.h>

#include <algorithm>
#include <array>
#include <string>
#include <vector>

#include "qr_fakes.h"
#include "tests.h"

using ostium::Ed25519PublicKey;
using ostium::verifyEd25519;
using qrfakes::fromHex;

namespace {

struct Rfc8032Vector {
  const char* secretKey;
  const char* publicKey;
  const char* message;
  const char* signature;
};

// RFC 8032 section 7.1, TEST 1, TEST 2, and TEST 3, in hex.
const Rfc8032Vector kRfc8032Vectors[] = {
    {"9d61b19deffd5a60ba844af492ec2cc44449c5697b326919703bac031cae7f60",
     "d75a980182b10ab7d54bfed3c964073a0ee172f3daa62325af021a68f707511a", "",
     "e5564300c360ac729086e2cc806e828a84877f1eb8e5d974d873e065224901555fb8821590a33bacc61e39701cf9b4"
     "6bd25bf5f0595bbe24655141438e7a100b"},
    {"4ccd089b28ff96da9db6c346ec114e0f5b8a319f35aba624da8cf6ed4fb8a6fb",
     "3d4017c3e843895a92b70aa74d1b7ebc9c982ccf2ec4968cc0cd55f12af4660c", "72",
     "92a009a9f0d4cab8720e820b5f642540a2b27b5416503f8fb3762223ebdb69da085ac1e43e15996e458f3613d0f11d"
     "8c387b2eaeb4302aeeb00d291612bb0c00"},
    {"c5aa8df43f9f837bedb7442f31dcb7b166d38535076f094b85ce3a2e0b4458f7",
     "fc51cd8e6218a1a38da47ed00230f0580816ed13ba3303ac5deb911548908025", "af82",
     "6291d657deec24024827e69c3abe01a30ce548a284743a445e3680d7db5ac3ac18ff9b538d16f290ae67f760984dc6"
     "594a7c15e9716ed28dc027beceea1ec40a"},
};

Ed25519PublicKey publicKeyFrom(const std::vector<uint8_t>& bytes) {
  Ed25519PublicKey key{};
  std::copy(bytes.begin(), bytes.end(), key.begin());
  return key;
}

bool verifies(const std::vector<uint8_t>& signature, const Ed25519PublicKey& key,
              const std::vector<uint8_t>& message) {
  return verifyEd25519(signature.data(), key, message.data(), message.size());
}

}  // namespace

void test_ed25519_matches_rfc8032_vectors() {
  for (const Rfc8032Vector& vector : kRfc8032Vectors) {
    std::vector<uint8_t> seed = fromHex(vector.secretKey);
    const std::vector<uint8_t> expectedKey = fromHex(vector.publicKey);
    const std::vector<uint8_t> message = fromHex(vector.message);
    const std::vector<uint8_t> expectedSignature = fromHex(vector.signature);
    TEST_ASSERT_EQUAL_UINT32(32, seed.size());
    TEST_ASSERT_EQUAL_UINT32(64, expectedSignature.size());

    // The RFC's secret key is the 32-byte seed. Monocypher wipes the seed it's
    // given, so it gets a copy.
    std::vector<uint8_t> seedCopy = seed;
    uint8_t secretKey[64];
    Ed25519PublicKey publicKey{};
    crypto_ed25519_key_pair(secretKey, publicKey.data(), seedCopy.data());
    TEST_ASSERT_EQUAL_HEX8_ARRAY(expectedKey.data(), publicKey.data(), 32);

    uint8_t signature[64];
    crypto_ed25519_sign(signature, secretKey, message.data(), message.size());
    TEST_ASSERT_EQUAL_HEX8_ARRAY(expectedSignature.data(), signature, 64);
    TEST_ASSERT_TRUE(verifies(expectedSignature, publicKey, message));

    // One flipped bit in the message, the signature, or the key fails. TEST 1
    // has an empty message, so a byte is added instead.
    std::vector<uint8_t> otherMessage = message;
    if (otherMessage.empty()) {
      otherMessage.push_back(0x00);
    } else {
      otherMessage[0] ^= 0x01;
    }
    TEST_ASSERT_FALSE(verifies(expectedSignature, publicKey, otherMessage));
    for (size_t bit : {0u, 255u, 256u, 511u}) {
      std::vector<uint8_t> flipped = expectedSignature;
      flipped[bit / 8] ^= static_cast<uint8_t>(1u << (bit % 8));
      TEST_ASSERT_FALSE(verifies(flipped, publicKey, message));
    }
    for (size_t bit : {0u, 100u, 254u}) {
      Ed25519PublicKey flipped = publicKey;
      flipped[bit / 8] ^= static_cast<uint8_t>(1u << (bit % 8));
      TEST_ASSERT_FALSE(verifies(expectedSignature, flipped, message));
    }
  }
}

void test_ed25519_rejects_non_canonical_signature() {
  // The group order L, little-endian.
  const uint8_t kGroupOrder[32] = {0xed, 0xd3, 0xf5, 0x5c, 0x1a, 0x63, 0x12, 0x58,
                                   0xd6, 0x9c, 0xf7, 0xa2, 0xde, 0xf9, 0xde, 0x14,
                                   0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
                                   0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x10};
  for (const Rfc8032Vector& vector : kRfc8032Vectors) {
    const Ed25519PublicKey key = publicKeyFrom(fromHex(vector.publicKey));
    const std::vector<uint8_t> message = fromHex(vector.message);
    const std::vector<uint8_t> signature = fromHex(vector.signature);
    TEST_ASSERT_TRUE(verifies(signature, key, message));

    // S + L satisfies the verification equation like S does, so only an
    // S < L check rejects it. S < L < 2^253, so the sum fits in 32 bytes.
    std::vector<uint8_t> malleable = signature;
    unsigned carry = 0;
    for (size_t i = 0; i < 32; ++i) {
      const unsigned sum = malleable[32 + i] + kGroupOrder[i] + carry;
      malleable[32 + i] = static_cast<uint8_t>(sum & 0xFF);
      carry = sum >> 8;
    }
    TEST_ASSERT_EQUAL_UINT32(0, carry);
    TEST_ASSERT_FALSE(verifies(malleable, key, message));
  }
}

void test_ed25519_test_seed_derives_fixture_public_key() {
  // Python's cryptography wrote the fixture's public key from the same private
  // key, so this cross-checks Monocypher's key derivation against it.
  std::array<uint8_t, 32> seed = qrfakes::testSeed();
  uint8_t secretKey[64];
  uint8_t publicKey[32];
  crypto_ed25519_key_pair(secretKey, publicKey, seed.data());
  TEST_ASSERT_EQUAL_STRING(ostium::kTestOnlyQrPublicKey,
                           ostium::encodeBase64Url(publicKey, sizeof(publicKey)).c_str());
  // crypto_ed25519_key_pair wipes the seed it's given, which is why
  // publicKeyFor() and the token signer pass a copy.
  TEST_ASSERT_EACH_EQUAL_HEX8(0, seed.data(), seed.size());
  TEST_ASSERT_EQUAL_STRING(ostium::kTestOnlyQrPublicKey,
                           qrfakes::publicKeyFor(qrfakes::testSeed()).c_str());
  TEST_ASSERT_FALSE(qrfakes::publicKeyFor(qrfakes::otherSeed()) == ostium::kTestOnlyQrPublicKey);
}
