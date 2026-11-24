#include "base64url.h"

namespace ostium {
namespace {

constexpr char kAlphabet[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789-_";

// The 6-bit value of a base64url character, or -1 for any other byte.
int valueOf(char c) {
  if (c >= 'A' && c <= 'Z') return c - 'A';
  if (c >= 'a' && c <= 'z') return c - 'a' + 26;
  if (c >= '0' && c <= '9') return c - '0' + 52;
  if (c == '-') return 62;
  if (c == '_') return 63;
  return -1;
}

}  // namespace

bool decodeBase64Url(const char* text, size_t length, std::string* out) {
  if (length % 4 == 1) return false;
  if (length > 0 && text == nullptr) return false;
  out->clear();
  out->reserve(length / 4 * 3 + 2);
  // bits counts the decoded bits in buffer that aren't written out yet.
  uint32_t buffer = 0;
  unsigned bits = 0;
  for (size_t i = 0; i < length; ++i) {
    const int value = valueOf(text[i]);
    if (value < 0) return false;
    buffer = (buffer << 6) | static_cast<uint32_t>(value);
    bits += 6;
    if (bits >= 8) {
      bits -= 8;
      out->push_back(static_cast<char>((buffer >> bits) & 0xFF));
      buffer &= (1u << bits) - 1;
    }
  }
  // What's left is the 0, 2, or 4 unused bits of the last character.
  return buffer == 0;
}

std::string encodeBase64Url(const uint8_t* data, size_t length) {
  std::string out;
  out.reserve((length * 4 + 2) / 3);
  size_t i = 0;
  for (; i + 3 <= length; i += 3) {
    const uint32_t block = (static_cast<uint32_t>(data[i]) << 16) |
                           (static_cast<uint32_t>(data[i + 1]) << 8) | data[i + 2];
    out.push_back(kAlphabet[block >> 18]);
    out.push_back(kAlphabet[(block >> 12) & 63]);
    out.push_back(kAlphabet[(block >> 6) & 63]);
    out.push_back(kAlphabet[block & 63]);
  }
  const size_t rest = length - i;
  if (rest > 0) {
    uint32_t block = static_cast<uint32_t>(data[i]) << 16;
    if (rest == 2) block |= static_cast<uint32_t>(data[i + 1]) << 8;
    out.push_back(kAlphabet[block >> 18]);
    out.push_back(kAlphabet[(block >> 12) & 63]);
    if (rest == 2) out.push_back(kAlphabet[(block >> 6) & 63]);
  }
  return out;
}

}  // namespace ostium
