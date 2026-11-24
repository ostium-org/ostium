// QR token text, header, signature, and claims: Q2 to Q6.

#include <ArduinoJson.h>
#include <unity.h>

#include <algorithm>
#include <array>
#include <cstdint>
#include <cstring>
#include <string>
#include <utility>
#include <vector>

#include "qr_fakes.h"
#include "tests.h"

using ostium::decodeBase64Url;
using ostium::encodeBase64Url;
using ostium::QrKey;
using ostium::QrToken;
using ostium::QrTokenProblem;
using qrfakes::claimsJson;
using qrfakes::headerJson;
using qrfakes::kIatUs;
using qrfakes::kJti;
using qrfakes::kOtherUnit;
using qrfakes::kUnit;
using qrfakes::kUser;
using qrfakes::mint;

namespace {

bool decodes(const std::string& text, std::string* out) {
  return decodeBase64Url(text.data(), text.size(), out);
}

std::string decoded(const std::string& text) {
  std::string bytes;
  TEST_ASSERT_TRUE_MESSAGE(decodes(text, &bytes), text.c_str());
  return bytes;
}

const char* problemName(QrTokenProblem problem) {
  switch (problem) {
    case QrTokenProblem::None: return "None";
    case QrTokenProblem::Malformed: return "Malformed";
    case QrTokenProblem::UnsupportedHeader: return "UnsupportedHeader";
    case QrTokenProblem::BadSignature: return "BadSignature";
    case QrTokenProblem::MalformedClaims: return "MalformedClaims";
  }
  return "unknown";
}

QrKey keyFor(const std::string& publicKey, const std::string& keyId = ostium::kQrSelfTestKeyId) {
  QrKey key;
  const char* error = "";
  TEST_ASSERT_TRUE_MESSAGE(ostium::makeQrKey(keyId, publicKey, &key, &error), error);
  return key;
}

// The door's key: the fixture's public key under ostium-qr-1.
QrKey testKey() { return keyFor(ostium::kTestOnlyQrPublicKey); }

QrToken verify(const std::string& text, const QrKey& key = testKey()) {
  return ostium::verifyQrToken(text.data(), text.size(), key);
}

// Checks the problem and that the detail names the failed check.
void expectProblem(QrTokenProblem expected, const std::string& text, const char* detail,
                   const char* label, const QrKey& key = testKey()) {
  const QrToken result = verify(text, key);
  TEST_ASSERT_EQUAL_STRING_MESSAGE(problemName(expected), problemName(result.problem), label);
  TEST_ASSERT_NOT_NULL_MESSAGE(std::strstr(result.detail, detail), label);
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

std::string join(const std::string& header, const std::string& payload,
                 const std::string& signature) {
  return header + "." + payload + "." + signature;
}

// Changes one character: 'A' and 'B' swap, and any other becomes 'A'.
std::string withSwappedCharacter(std::string text, size_t index) {
  text[index] = text[index] == 'A' ? 'B' : 'A';
  return text;
}

// Sets the lowest unused bit of the segment's last character. The segment's
// length must not be a multiple of 4, so that character has unused bits.
std::string withTrailingBitSet(std::string segment) {
  const std::string alphabet = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789-_";
  segment.back() = alphabet[alphabet.find(segment.back()) | 1];
  return segment;
}

// The fixture's claims in signJWT's order, with one member's raw JSON value
// replaced, or the member left out when value is nullptr.
std::string claimsWith(const char* name, const char* value) {
  const std::pair<const char*, std::string> members[] = {
      {"iss", "\"ostium\""},
      {"aud", "\"ostium-devices\""},
      {"sub", "\"" + std::string(kUser) + "\""},
      {"unit_id", "\"" + std::string(kUnit) + "\""},
      {"jti", "\"" + std::string(kJti) + "\""},
      {"iat", "1792670400.123456"},
      {"exp", "1792670460.123456"},
  };
  std::string list;
  for (const auto& member : members) {
    std::string text = member.second;
    if (std::strcmp(member.first, name) == 0) {
      if (value == nullptr) continue;
      text = value;
    }
    if (!list.empty()) list += ",";
    list += "\"" + std::string(member.first) + "\":" + text;
  }
  return qrfakes::rawClaims(list);
}

// A JSON number as ArduinoJson parses it on the door.
double parsedNumber(const std::string& text) {
  JsonDocument document;
  TEST_ASSERT_FALSE_MESSAGE(deserializeJson(document, text), text.c_str());
  TEST_ASSERT_TRUE_MESSAGE(document.is<double>(), text.c_str());
  return document.as<double>();
}

// Microseconds as JavaScript prints Number(us) / 1e6, which is what signJWT
// writes: the shortest text, so trailing zeros and a bare dot are dropped.
std::string javascriptSeconds(int64_t us) {
  std::string text = qrfakes::epochSeconds(us);
  text.erase(text.find_last_not_of('0') + 1);
  if (text.back() == '.') text.pop_back();
  return text;
}

}  // namespace

void test_base64url_decoding_is_strict() {
  std::string bytes;

  // Each segment of the fixture token, and the fixture's public key, decodes
  // and encodes back to the same text.
  const std::string token = ostium::kQrSelfTestToken;
  const size_t firstDot = token.find('.');
  const size_t secondDot = token.find('.', firstDot + 1);
  const std::string header = token.substr(0, firstDot);
  const std::string payload = token.substr(firstDot + 1, secondDot - firstDot - 1);
  const std::string signature = token.substr(secondDot + 1);
  TEST_ASSERT_TRUE(decodes(header, &bytes));
  TEST_ASSERT_EQUAL_STRING("{\"alg\":\"EdDSA\",\"typ\":\"JWT\",\"kid\":\"ostium-qr-1\"}",
                           bytes.c_str());
  TEST_ASSERT_EQUAL_STRING(header.c_str(), encodeBase64Url(bytes).c_str());
  TEST_ASSERT_TRUE(decodes(payload, &bytes));
  TEST_ASSERT_EQUAL_UINT32(0, bytes.find("{\"iss\":\"ostium\","));
  TEST_ASSERT_EQUAL_STRING(payload.c_str(), encodeBase64Url(bytes).c_str());
  TEST_ASSERT_TRUE(decodes(signature, &bytes));
  TEST_ASSERT_EQUAL_UINT32(64, bytes.size());
  TEST_ASSERT_EQUAL_STRING(signature.c_str(), encodeBase64Url(bytes).c_str());
  const std::string publicKey = ostium::kTestOnlyQrPublicKey;
  TEST_ASSERT_TRUE(decodes(publicKey, &bytes));
  TEST_ASSERT_EQUAL_UINT32(32, bytes.size());
  TEST_ASSERT_EQUAL_STRING(publicKey.c_str(), encodeBase64Url(bytes).c_str());

  TEST_ASSERT_TRUE(decodes("", &bytes));
  TEST_ASSERT_EQUAL_UINT32(0, bytes.size());
  TEST_ASSERT_TRUE(decodes("-_8", &bytes));
  TEST_ASSERT_EQUAL_STRING("\xfb\xff", bytes.c_str());

  const std::string rejected[] = {
      "AA==",                    // Padding.
      "AAA=",                    // Padding.
      "AA+A",                    // Standard base64, not base64url.
      "AA/A",                    // Standard base64, not base64url.
      "AA A",                    // Whitespace.
      "AAA\n",                   // A trailing newline.
      std::string("AA\0A", 4),   // An embedded NUL.
      "AA\x80" "A",              // A byte of 0x80 or higher.
      "AA\xff" "A",
      "A",                       // length % 4 == 1.
      "AAAAA",
      "AB",                      // One byte with nonzero unused bits.
      "AAB",                     // Two bytes with nonzero unused bits.
  };
  for (const std::string& text : rejected) {
    TEST_ASSERT_FALSE_MESSAGE(decodes(text, &bytes), text.c_str());
  }
  // The same bytes with zero unused bits decode.
  TEST_ASSERT_TRUE(decodes("AA", &bytes));
  TEST_ASSERT_TRUE(decodes("AAA", &bytes));
}

void test_base64url_round_trips() {
  // RFC 4648 section 10 test vectors, without padding.
  const char* kVectors[][2] = {{"", ""},         {"f", "Zg"},         {"fo", "Zm8"},
                               {"foo", "Zm9v"},  {"foob", "Zm9vYg"},  {"fooba", "Zm9vYmE"},
                               {"foobar", "Zm9vYmFy"}};
  for (const auto& vector : kVectors) {
    TEST_ASSERT_EQUAL_STRING(vector[1], encodeBase64Url(std::string(vector[0])).c_str());
  }

  // Pseudo-random byte strings of every length from 0 to 70 round-trip.
  uint32_t state = 12345;
  for (size_t length = 0; length <= 70; ++length) {
    std::string original;
    for (size_t i = 0; i < length; ++i) {
      state = state * 1103515245u + 12345u;
      original.push_back(static_cast<char>(state >> 24));
    }
    const std::string text = encodeBase64Url(original);
    TEST_ASSERT_EQUAL_UINT32((length * 4 + 2) / 3, text.size());
    std::string back;
    TEST_ASSERT_TRUE(decodes(text, &back));
    TEST_ASSERT_TRUE(back == original);
  }
}

void test_malformed_qr_text_is_rejected() {
  const std::string token = ostium::kQrSelfTestToken;
  const Segments fixture = split(token);
  TEST_ASSERT_EQUAL_UINT32(453, token.size());
  std::string withNul = token;
  withNul.insert(100, 1, '\0');
  std::string nonAscii = token;
  nonAscii[100] = '\xc3';

  struct Case {
    std::string text;
    const char* detail;
    const char* label;
  };
  const Case cases[] = {
      {"", "empty", "empty"},
      {join(std::string(379, 'A'), fixture.payload, fixture.signature), "over 768", "769 bytes"},
      {fixture.header + fixture.payload + fixture.signature, "two dots", "no dot"},
      {fixture.header + "." + fixture.payload + fixture.signature, "two dots", "one dot"},
      {join(fixture.header, fixture.payload, fixture.signature.substr(0, 40)) + "." +
           fixture.signature.substr(40),
       "two dots", "three dots"},
      {join("", fixture.payload, fixture.signature), "header is empty", "empty header"},
      {join(fixture.header, "", fixture.signature), "payload is empty", "empty payload"},
      {join(fixture.header, fixture.payload, ""), "86", "empty signature"},
      {join(fixture.header, std::string(513, 'A'), fixture.signature), "over 512",
       "513-char payload"},
      {join(fixture.header, fixture.payload + "AAA", fixture.signature), "payload length",
       "payload length % 4 == 1"},
      {join(fixture.header, fixture.payload, fixture.signature.substr(0, 85)), "86",
       "85-char signature"},
      {join(fixture.header, fixture.payload, fixture.signature + "A"), "86", "87-char signature"},
      {join(fixture.header, fixture.payload, withTrailingBitSet(fixture.signature)),
       "signature isn't valid base64url", "signature with nonzero trailing bits"},
      {token + "\n", "byte outside", "trailing newline"},
      {" " + token, "byte outside", "leading space"},
      {withNul, "byte outside", "embedded NUL"},
      {nonAscii, "byte outside", "non-ASCII byte"},
      {token + "==", "byte outside", "padding"},
      {join(fixture.header, fixture.payload, "+" + fixture.signature.substr(1)), "byte outside",
       "standard base64"},
  };
  for (const Case& c : cases) {
    expectProblem(QrTokenProblem::Malformed, c.text, c.detail, c.label);
  }

  // The limits themselves pass the shape checks and fail later: a 768-byte
  // text at the header pin, and a 512-char payload at the signature.
  expectProblem(QrTokenProblem::UnsupportedHeader,
                join(std::string(378, 'A'), fixture.payload, fixture.signature), "header",
                "768 bytes");
  expectProblem(QrTokenProblem::BadSignature,
                join(fixture.header, std::string(512, 'A'), fixture.signature), "verify",
                "512-char payload");
}

void test_qr_demo_payload_is_rejected() {
  // What the app's demo mode encodes: JSON.stringify(createDemoQRCode(...)).
  const std::string demo =
      "{\"type\":\"ostium-demo\",\"houseId\":\"22222222-2222-4222-8222-222222222222\","
      "\"id\":\"6b8d0f2a-4c6e-4a8b-9d0f-2a4c6e8a0b2d\",\"issuedAt\":1792670400123,"
      "\"expiresAt\":1792670460123}";
  expectProblem(QrTokenProblem::Malformed, demo, "byte outside", "demo payload");
  // Encoded, it's still not a token.
  expectProblem(QrTokenProblem::Malformed, encodeBase64Url(demo), "two dots",
                "encoded demo payload");
}

void test_qr_header_must_match_configured_key() {
  // The configured key expects exactly the backend's header bytes.
  const QrKey key = testKey();
  TEST_ASSERT_EQUAL_STRING("eyJhbGciOiJFZERTQSIsInR5cCI6IkpXVCIsImtpZCI6Im9zdGl1bS1xci0xIn0",
                           key.expectedHeader.c_str());
  TEST_ASSERT_EQUAL_STRING(split(ostium::kQrSelfTestToken).header.c_str(),
                           key.expectedHeader.c_str());
  TEST_ASSERT_EQUAL_STRING("ostium-qr-1", key.keyId.c_str());

  const std::string claims = claimsJson(kJti, kIatUs, kIatUs + 60000000);
  TEST_ASSERT_TRUE(verify(mint(headerJson(), claims)).problem == QrTokenProblem::None);

  // Each of these is signed with the trusted key, but the header isn't the
  // pinned bytes. The door never parses it, so it can't pick the algorithm.
  const char* const kHeaders[][2] = {
      {"{\"alg\":\"none\",\"typ\":\"JWT\",\"kid\":\"ostium-qr-1\"}", "alg none"},
      {"{\"alg\":\"HS256\",\"typ\":\"JWT\",\"kid\":\"ostium-qr-1\"}", "alg HS256"},
      {"{\"alg\":\"EdDSA\",\"typ\":\"JWT\",\"kid\":\"ostium-qr-2\"}", "another kid"},
      {"{\"typ\":\"JWT\",\"alg\":\"EdDSA\",\"kid\":\"ostium-qr-1\"}", "reordered keys"},
      {"{\"alg\":\"EdDSA\",\"typ\":\"JWT\",\"kid\":\"ostium-qr-1\",\"jku\":\"https://x.test\"}",
       "extra field"},
      {"{\"alg\":\"EdDSA\",\"kid\":\"ostium-qr-1\"}", "missing typ"},
      {"{\"alg\": \"EdDSA\",\"typ\":\"JWT\",\"kid\":\"ostium-qr-1\"}", "space after a colon"},
  };
  for (const auto& header : kHeaders) {
    expectProblem(QrTokenProblem::UnsupportedHeader, mint(header[0], claims), "doesn't match",
                  header[1]);
  }

  // A door configured for ostium-qr-2 takes that header and refuses ostium-qr-1.
  const QrKey second = keyFor(ostium::kTestOnlyQrPublicKey, "ostium-qr-2");
  TEST_ASSERT_TRUE(verify(mint(headerJson("ostium-qr-2"), claims), second).problem ==
                   QrTokenProblem::None);
  expectProblem(QrTokenProblem::UnsupportedHeader, mint(headerJson(), claims), "doesn't match",
                "ostium-qr-1 at an ostium-qr-2 door", second);

  // makeQrKey takes only a kid that JSON.stringify writes unescaped, so the
  // pinned header always equals the backend's bytes. It refuses a bad kid or
  // key, and leaves *out unchanged when it does.
  const std::string publicKey = ostium::kTestOnlyQrPublicKey;
  const std::string longestKid(ostium::kMaxQrKeyIdLength, 'k');
  TEST_ASSERT_EQUAL_STRING(encodeBase64Url(headerJson(longestKid)).c_str(),
                           keyFor(publicKey, longestKid).expectedHeader.c_str());
  const std::string badKids[] = {"", longestKid + "k", "ostium\"qr", "ostium\\qr", "ostium qr",
                                 "ostium\x7fqr", "ostium\xc3\xa9qr"};
  for (const std::string& kid : badKids) {
    QrKey out = key;
    const char* error = nullptr;
    TEST_ASSERT_FALSE_MESSAGE(ostium::makeQrKey(kid, publicKey, &out, &error), kid.c_str());
    TEST_ASSERT_NOT_NULL(error);
    TEST_ASSERT_TRUE(std::strstr(error, "key ID") != nullptr);
    TEST_ASSERT_EQUAL_STRING(key.expectedHeader.c_str(), out.expectedHeader.c_str());
  }
  const std::string badKeys[] = {
      publicKey.substr(0, 42),                        // 42 chars.
      publicKey + "A",                                // 44 chars.
      "+" + publicKey.substr(1),                      // Standard base64.
      publicKey.substr(0, 42) + "J",                  // Nonzero unused bits.
      std::string(43, 'A'),                           // All zero bytes.
      publicKey.substr(0, 42) + "=",                  // Padding.
  };
  for (const std::string& bad : badKeys) {
    QrKey out = key;
    const char* error = nullptr;
    TEST_ASSERT_FALSE_MESSAGE(ostium::makeQrKey("ostium-qr-1", bad, &out, &error), bad.c_str());
    TEST_ASSERT_TRUE(std::strstr(error, "public key") != nullptr);
    TEST_ASSERT_TRUE(out.publicKey == key.publicKey);
  }
}

void test_forged_qr_is_rejected() {
  const Segments fixture = split(ostium::kQrSelfTestToken);
  const std::string claims = decoded(fixture.payload);
  // The fixture's claims with one piece of text replaced, re-encoded, and
  // sent with the fixture's original signature.
  const auto edited = [&](const std::string& from, const std::string& to) {
    std::string text = claims;
    const size_t at = text.find(from);
    TEST_ASSERT_TRUE_MESSAGE(at != std::string::npos, from.c_str());
    text.replace(at, from.size(), to);
    return join(fixture.header, encodeBase64Url(text), fixture.signature);
  };
  const std::string otherToken = qrfakes::token(qrfakes::jtiFor(1), kIatUs);
  std::array<uint8_t, 32> rfcTest1Seed{};
  const std::vector<uint8_t> rfcSeedBytes =
      qrfakes::fromHex("9d61b19deffd5a60ba844af492ec2cc44449c5697b326919703bac031cae7f60");
  std::copy(rfcSeedBytes.begin(), rfcSeedBytes.end(), rfcTest1Seed.begin());

  const std::pair<std::string, const char*> cases[] = {
      {edited("\"exp\":1792670460.123456", "\"exp\":1792674060.123456"), "exp + 3600"},
      {edited(kUnit, kOtherUnit), "unit changed"},
      {join(fixture.header, fixture.payload, withSwappedCharacter(fixture.signature, 43)),
       "one signature char changed"},
      {mint(headerJson(), claims, qrfakes::otherSeed()), "signed by another key"},
      {join(fixture.header, fixture.payload, split(otherToken).signature),
       "another token's signature"},
      {qrfakes::signSegments(fixture.header, fixture.payload, rfcTest1Seed),
       "signed by the RFC 8032 TEST 1 key"},
  };
  for (const auto& c : cases) {
    expectProblem(QrTokenProblem::BadSignature, c.first, "doesn't verify", c.second);
  }

  // The other key's token verifies under that key, so only the key differs.
  const QrKey otherKey = keyFor(qrfakes::publicKeyFor(qrfakes::otherSeed()));
  TEST_ASSERT_TRUE(verify(mint(headerJson(), claims, qrfakes::otherSeed()), otherKey).problem ==
                   QrTokenProblem::None);
  expectProblem(QrTokenProblem::BadSignature, ostium::kQrSelfTestToken, "doesn't verify",
                "fixture at a door with another key", otherKey);

  // Under a small-order public key, Monocypher's cofactored check accepts one
  // fixed signature over every message: R is the base point and S is 1. So
  // anyone could sign any claims for such a key, and makeQrKey refuses each
  // one, with or without a canonical encoding. The fixture's key refuses the
  // same forgery.
  uint8_t probe[ostium::kEd25519SignatureBytes] = {0x58};
  std::fill(probe + 1, probe + 32, 0x66);
  probe[32] = 0x01;
  const std::string forged =
      join(fixture.header, fixture.payload, encodeBase64Url(probe, sizeof(probe)));
  expectProblem(QrTokenProblem::BadSignature, forged, "doesn't verify", "small-order forgery");
  const char* const kSmallOrderKeys[][2] = {
      {"AQAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAA", "identity"},
      {"AQAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAIA", "identity with the sign bit set"},
      {"7v_______________________________________38", "identity as y = p + 1"},
      {"7P_______________________________________38", "order 2"},
      {"AAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAIA", "order 4"},
      {"JuiVj8KyJ7BFw_SJ8u-Y8NXfrAXTxjM5sTgCiG1T_AU", "order 8"},
      {"xxdqcD1N2E-6PAt2DRBnDyogU_osOczGTsf9d5KsA3o", "the other order 8"},
  };
  const QrKey trusted = testKey();
  for (const auto& smallOrder : kSmallOrderKeys) {
    // A key built without makeQrKey's checks takes the forgery.
    QrKey unchecked = trusted;
    const std::string bytes = decoded(smallOrder[0]);
    TEST_ASSERT_EQUAL_UINT32_MESSAGE(32, bytes.size(), smallOrder[1]);
    std::memcpy(unchecked.publicKey.data(), bytes.data(), unchecked.publicKey.size());
    TEST_ASSERT_EQUAL_STRING_MESSAGE("None", problemName(verify(forged, unchecked).problem),
                                     smallOrder[1]);

    QrKey out = trusted;
    const char* error = nullptr;
    TEST_ASSERT_FALSE_MESSAGE(ostium::makeQrKey("ostium-qr-1", smallOrder[0], &out, &error),
                              smallOrder[1]);
    TEST_ASSERT_EQUAL_STRING_MESSAGE("public key is a small-order point", error, smallOrder[1]);
    TEST_ASSERT_TRUE_MESSAGE(out.publicKey == trusted.publicKey, smallOrder[1]);
  }
}

void test_qr_signature_matches_backend_fixture() {
  const std::string token = ostium::kQrSelfTestToken;
  const QrToken result = verify(token);
  TEST_ASSERT_EQUAL_STRING("None", problemName(result.problem));
  TEST_ASSERT_EQUAL_STRING("", result.detail);
  const ostium::QrClaims& claims = result.claims;
  TEST_ASSERT_EQUAL_STRING(kJti, claims.credentialId.c_str());
  TEST_ASSERT_EQUAL_STRING(kUnit, claims.unitId.c_str());
  TEST_ASSERT_EQUAL_STRING(ostium::kQrSelfTestCredentialId, claims.credentialId.c_str());
  TEST_ASSERT_EQUAL_STRING(ostium::kQrSelfTestUnitId, claims.unitId.c_str());
  TEST_ASSERT_EQUAL_INT64(1792670400125LL, claims.issuedAtMs);
  TEST_ASSERT_EQUAL_INT64(1792670460122LL, claims.expiresAtMs);
  TEST_ASSERT_EQUAL_INT64(qrfakes::kIatMs, claims.issuedAtMs);
  TEST_ASSERT_TRUE(claims.hasIssuer);
  TEST_ASSERT_EQUAL_STRING("ostium", claims.issuer.c_str());
  TEST_ASSERT_TRUE(claims.hasAudience);
  TEST_ASSERT_EQUAL_STRING("ostium-devices", claims.audience.c_str());
  TEST_ASSERT_TRUE(claims.hasSubject);
  TEST_ASSERT_FALSE(claims.hasOtherClaims);

  // Monocypher, signing the fixture's header.payload with the fixture's seed,
  // reproduces the signature that Python's cryptography wrote. Ed25519 is
  // deterministic, so the whole token matches.
  const Segments fixture = split(token);
  TEST_ASSERT_EQUAL_UINT32(86, fixture.signature.size());
  TEST_ASSERT_EQUAL_STRING(token.c_str(),
                           qrfakes::signSegments(fixture.header, fixture.payload).c_str());

  // The test token builders write the backend's bytes too.
  TEST_ASSERT_EQUAL_STRING(decoded(fixture.payload).c_str(),
                           claimsJson(kJti, kIatUs, kIatUs + 60000000).c_str());
  TEST_ASSERT_EQUAL_STRING(token.c_str(), qrfakes::token(kJti, kIatUs).c_str());
}

void test_qr_claims_must_parse() {
  const Segments fixture = split(ostium::kQrSelfTestToken);
  struct Case {
    std::string claims;
    const char* detail;
    const char* label;
  };
  const Case cases[] = {
      {"not json", "valid JSON", "not JSON"},
      {"[1,2,3]", "JSON object", "array root"},
      {"\"text\"", "JSON object", "string root"},
      {claimsWith("jti", nullptr), "jti", "missing jti"},
      {claimsWith("jti", "55555555"), "jti", "numeric jti"},
      {claimsWith("jti", "\"AAAAAAAA-5555-4555-8555-555555555555\""), "jti", "uppercase jti"},
      {claimsWith("jti", "\"55555555-5555-4555-8555-555555555555\\u0000\""), "jti",
       "jti with a NUL"},
      {claimsWith("jti", "\"55555555-5555-4555-8555-5555555555555\""), "jti", "37-char jti"},
      {claimsWith("jti", "\"555555555-555-4555-8555-555555555555\""), "jti",
       "jti with a misplaced dash"},
      {claimsWith("unit_id", nullptr), "unit_id", "missing unit_id"},
      {claimsWith("unit_id", "\"22222222-2222-4222-8222-22222222222\""), "unit_id",
       "35-char unit_id"},
      {claimsWith("unit_id", "\"2222222g-2222-4222-8222-222222222222\""), "unit_id",
       "non-hex unit_id"},
      {claimsWith("unit_id", "[\"22222222-2222-4222-8222-222222222222\"]"), "unit_id",
       "unit_id in an array"},
      {claimsWith("iat", nullptr), "iat isn't a number", "missing iat"},
      {claimsWith("iat", "\"1792670400.123456\""), "iat isn't a number", "string iat"},
      {claimsWith("exp", "true"), "exp isn't a number", "true exp"},
      {claimsWith("exp", "null"), "exp isn't a number", "null exp"},
      {claimsWith("exp", "1e999"), "exp is outside", "infinite exp"},
      {claimsWith("iat", "1.0e9"), "iat is outside", "iat in 2001"},
      {claimsWith("exp", "5e9"), "exp is outside", "exp after 2100"},
      {claimsWith("iat", "1577836799.999"), "iat is outside", "iat just before 2020"},
      {claimsWith("exp", "4102444800.001"), "exp is outside", "exp just after 2100"},
  };
  for (const Case& c : cases) {
    expectProblem(QrTokenProblem::MalformedClaims, mint(headerJson(), c.claims), c.detail,
                  c.label);
  }
  // Signed, but the payload isn't strict base64url.
  expectProblem(QrTokenProblem::MalformedClaims,
                qrfakes::signSegments(fixture.header, withTrailingBitSet(fixture.payload)),
                "base64url", "payload with nonzero trailing bits");

  // An integer iat is a number, and the limits themselves are accepted.
  const QrToken integer = verify(mint(headerJson(), claimsWith("iat", "1792670400")));
  TEST_ASSERT_EQUAL_STRING("None", problemName(integer.problem));
  TEST_ASSERT_EQUAL_INT64(1792670400001LL, integer.claims.issuedAtMs);
  TEST_ASSERT_TRUE(verify(mint(headerJson(), claimsWith("iat", "1577836800"))).problem ==
                   QrTokenProblem::None);
  TEST_ASSERT_TRUE(verify(mint(headerJson(), claimsWith("exp", "4102444800"))).problem ==
                   QrTokenProblem::None);

  // Claim values that parse are left for the caller's checks (Q9).
  const std::string oddClaims = qrfakes::rawClaims(
      "\"iss\":\"evil\",\"aud\":[\"ostium-devices\"],\"sub\":\"\",\"unit_id\":\"" +
      std::string(kOtherUnit) + "\",\"jti\":\"" + std::string(kJti) +
      "\",\"iat\":1792670400,\"exp\":1792670400,\"device_id\":\"front\"");
  const QrToken odd = verify(mint(headerJson(), oddClaims));
  TEST_ASSERT_EQUAL_STRING("None", problemName(odd.problem));
  TEST_ASSERT_TRUE(odd.claims.hasIssuer);
  TEST_ASSERT_EQUAL_STRING("evil", odd.claims.issuer.c_str());
  TEST_ASSERT_FALSE(odd.claims.hasAudience);
  TEST_ASSERT_FALSE(odd.claims.hasSubject);
  TEST_ASSERT_TRUE(odd.claims.hasOtherClaims);
  TEST_ASSERT_EQUAL_STRING(kOtherUnit, odd.claims.unitId.c_str());
  const QrToken nul = verify(mint(headerJson(), claimsWith("iss", "\"ostium\\u0000evil\"")));
  TEST_ASSERT_EQUAL_STRING("None", problemName(nul.problem));
  TEST_ASSERT_FALSE(nul.claims.hasIssuer);
  TEST_ASSERT_FALSE(verify(mint(headerJson(), claimsWith("sub", nullptr))).claims.hasSubject);
  TEST_ASSERT_FALSE(verify(mint(headerJson(), claimsWith("sub", "42"))).claims.hasSubject);
}

void test_qr_times_round_inward() {
  const QrToken fixture = verify(ostium::kQrSelfTestToken);
  TEST_ASSERT_EQUAL_INT64(1792670400125LL, fixture.claims.issuedAtMs);
  TEST_ASSERT_EQUAL_INT64(1792670460122LL, fixture.claims.expiresAtMs);

  // Through the whole token path, as signJWT prints the times.
  const auto issuedAt = [](const char* iat) {
    return verify(mint(headerJson(), claimsWith("iat", iat))).claims.issuedAtMs;
  };
  const auto expiresAt = [](const char* exp) {
    return verify(mint(headerJson(), claimsWith("exp", exp))).claims.expiresAtMs;
  };
  TEST_ASSERT_EQUAL_INT64(1792670460499LL, expiresAt("1792670460.5"));
  TEST_ASSERT_EQUAL_INT64(1792670459999LL, expiresAt("1792670460"));
  TEST_ASSERT_EQUAL_INT64(1792670460998LL, expiresAt("1792670460.999999"));
  TEST_ASSERT_EQUAL_INT64(1792670459999LL, expiresAt("1792670460.000001"));
  TEST_ASSERT_EQUAL_INT64(1792670400002LL, issuedAt("1792670400.000001"));
  TEST_ASSERT_EQUAL_INT64(1792670401001LL, issuedAt("1792670400.999999"));
  TEST_ASSERT_EQUAL_INT64(1792670400501LL, issuedAt("1792670400.5"));
  TEST_ASSERT_EQUAL_INT64(1792670400001LL, issuedAt("1792670400"));

  // Every 37 µs across one second, printed the way JavaScript prints
  // Number(us) / 1e6 and parsed by ArduinoJson: expiry is never rounded up,
  // issue time is never rounded down, and neither moves more than 2 ms.
  const int64_t second = kIatUs / 1000000 * 1000000;
  for (int64_t us = second; us < second + 1000000; us += 37) {
    const std::string text = javascriptSeconds(us);
    const double seconds = parsedNumber(text);
    const int64_t iatMs = ostium::issuedAtMsFromSeconds(seconds);
    const int64_t expMs = ostium::expiresAtMsFromSeconds(seconds);
    TEST_ASSERT_TRUE_MESSAGE(iatMs * 1000 >= us + 1, text.c_str());
    TEST_ASSERT_TRUE_MESSAGE(expMs * 1000 <= us - 1, text.c_str());
    TEST_ASSERT_TRUE_MESSAGE(iatMs * 1000 < us + 3000, text.c_str());
    TEST_ASSERT_TRUE_MESSAGE(expMs * 1000 > us - 3000, text.c_str());
  }
}
