#pragma once

#include <cstddef>
#include <cstdint>
#include <string>

namespace ostium {

// Strict RFC 4648 section 5 decoding, with no padding (Q2). Returns false for
// any character outside A-Z a-z 0-9 - _ (so '=', '+', '/', whitespace, NUL, and
// bytes >= 0x80 fail), for length % 4 == 1, or when the unused low bits of the
// last character aren't zero (4 bits when length % 4 == 2, 2 bits when
// length % 4 == 3). So each byte string has exactly one accepted encoding.
// *out is unspecified on failure.
bool decodeBase64Url(const char* text, size_t length, std::string* out);

// Encodes without padding.
std::string encodeBase64Url(const uint8_t* data, size_t length);

inline std::string encodeBase64Url(const std::string& bytes) {
  return encodeBase64Url(reinterpret_cast<const uint8_t*>(bytes.data()), bytes.size());
}

}  // namespace ostium
