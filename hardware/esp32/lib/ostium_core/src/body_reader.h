#pragma once

// Reads an HTTP response body with a size cap and two deadlines, so a server
// that stops sending can't block the main loop.

#include <cstddef>
#include <cstdint>
#include <string>

#include "interfaces.h"

namespace ostium {

// The bytes of one response body as the network delivers them. On the board it
// wraps the WiFiClient that HTTPClient leaves after reading the headers.
class ByteSource {
 public:
  virtual ~ByteSource() = default;
  // How many bytes can be read now without waiting.
  virtual size_t available() = 0;
  // Reads one byte that available() reported, or returns -1.
  virtual int read() = 0;
  // False once the server has closed the connection.
  virtual bool connected() = 0;
  // Lets other tasks run while no bytes are ready. delay(1) on the board.
  virtual void wait() = 0;
};

enum class BodyResult {
  Complete,
  TooLarge,   // the body is longer than the cap
  TimedOut,   // no byte for idleMs, or the whole body took longer than totalMs
  Closed,     // the connection closed before the body ended
  Malformed,  // bad chunked encoding
};

struct BodyLimits {
  size_t maxBytes;
  uint32_t idleMs;
  uint32_t totalMs;
};

// Reads one body into out. contentLength is the Content-Length header, or -1 if
// the response has none. chunked is true for Transfer-Encoding: chunked, which
// takes precedence over contentLength. Without either, the body ends when the
// server closes the connection. On any result other than Complete, out holds
// what was read so far, and the caller must close the connection.
BodyResult readBody(ByteSource& source, const Clock& clock, int64_t contentLength,
                    bool chunked, const BodyLimits& limits, std::string* out);

const char* describe(BodyResult result);

}  // namespace ostium
