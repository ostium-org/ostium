#include "body_reader.h"

#include <initializer_list>

namespace ostium {
namespace {

// A chunk-size line is at most 8 hex digits plus a short extension.
constexpr size_t kMaxChunkLineBytes = 64;
constexpr size_t kMaxChunkSizeDigits = 8;

class Reader {
 public:
  Reader(ByteSource& source, const Clock& clock, const BodyLimits& limits)
      : source_(source),
        clock_(clock),
        limits_(limits),
        startedAtMs_(clock.nowMs()),
        lastByteAtMs_(startedAtMs_) {}

  // Returns the next byte, or -1 with failure() set to Closed or TimedOut. The
  // total limit is checked before every byte, so a server that never stops
  // sending can't keep the read going either.
  int next() {
    for (;;) {
      const uint32_t now = clock_.nowMs();
      if (now - startedAtMs_ >= limits_.totalMs) return fail(BodyResult::TimedOut);
      if (source_.available() > 0) {
        const int c = source_.read();
        if (c >= 0) {
          lastByteAtMs_ = now;
          return c;
        }
      } else if (!source_.connected()) {
        return fail(BodyResult::Closed);
      }
      if (now - lastByteAtMs_ >= limits_.idleMs) return fail(BodyResult::TimedOut);
      source_.wait();
    }
  }

  // Reads the CRLF that ends a chunk's data.
  BodyResult expectCrlf() {
    for (const char expected : {'\r', '\n'}) {
      const int c = next();
      if (c < 0) return failure_;
      if (c != expected) return BodyResult::Malformed;
    }
    return BodyResult::Complete;
  }

  // Reads a line up to LF and drops a trailing CR. Returns false on a failure,
  // or with failure() set to Malformed if the line is too long.
  bool readLine(std::string* line) {
    line->clear();
    for (;;) {
      const int c = next();
      if (c < 0) return false;
      if (c == '\n') break;
      if (line->size() >= kMaxChunkLineBytes) {
        failure_ = BodyResult::Malformed;
        return false;
      }
      line->push_back(static_cast<char>(c));
    }
    if (!line->empty() && line->back() == '\r') line->pop_back();
    return true;
  }

  BodyResult failure() const { return failure_; }

 private:
  int fail(BodyResult result) {
    failure_ = result;
    return -1;
  }

  ByteSource& source_;
  const Clock& clock_;
  const BodyLimits& limits_;
  const uint32_t startedAtMs_;
  uint32_t lastByteAtMs_;
  BodyResult failure_ = BodyResult::Closed;
};

int hexValue(char c) {
  if (c >= '0' && c <= '9') return c - '0';
  if (c >= 'a' && c <= 'f') return c - 'a' + 10;
  if (c >= 'A' && c <= 'F') return c - 'A' + 10;
  return -1;
}

// Parses a chunk-size line such as "1a", "1a;name=value", or "1a ;name=value".
// HTTP allows spaces and tabs before an extension's semicolon.
bool parseChunkSize(const std::string& line, size_t* size) {
  size_t value = 0;
  size_t i = 0;
  for (; i < line.size() && hexValue(line[i]) >= 0; ++i) {
    if (i >= kMaxChunkSizeDigits) return false;
    value = value * 16 + static_cast<size_t>(hexValue(line[i]));
  }
  if (i == 0) return false;
  while (i < line.size() && (line[i] == ' ' || line[i] == '\t')) ++i;
  if (i < line.size() && line[i] != ';') return false;
  *size = value;
  return true;
}

BodyResult readChunked(Reader& reader, const BodyLimits& limits, std::string* out) {
  std::string line;
  for (;;) {
    if (!reader.readLine(&line)) return reader.failure();
    size_t size = 0;
    if (!parseChunkSize(line, &size)) return BodyResult::Malformed;
    if (size == 0) break;
    if (size > limits.maxBytes - out->size()) return BodyResult::TooLarge;
    for (size_t i = 0; i < size; ++i) {
      const int c = reader.next();
      if (c < 0) return reader.failure();
      out->push_back(static_cast<char>(c));
    }
    // Each chunk's data ends with exactly CRLF.
    const BodyResult end = reader.expectCrlf();
    if (end != BodyResult::Complete) return end;
  }
  // Skip any trailer lines up to the empty line that ends the body, so nothing
  // is left on a reused connection. The trailers count against the cap too.
  size_t trailerBytes = 0;
  do {
    if (!reader.readLine(&line)) return reader.failure();
    trailerBytes += line.size() + 2;
    if (trailerBytes > limits.maxBytes) return BodyResult::TooLarge;
  } while (!line.empty());
  return BodyResult::Complete;
}

}  // namespace

BodyResult readBody(ByteSource& source, const Clock& clock, int64_t contentLength,
                    bool chunked, const BodyLimits& limits, std::string* out) {
  out->clear();
  Reader reader(source, clock, limits);
  if (chunked) return readChunked(reader, limits, out);

  if (contentLength >= 0) {
    if (static_cast<uint64_t>(contentLength) > limits.maxBytes) return BodyResult::TooLarge;
    for (int64_t i = 0; i < contentLength; ++i) {
      const int c = reader.next();
      if (c < 0) return reader.failure();
      out->push_back(static_cast<char>(c));
    }
    return BodyResult::Complete;
  }

  // No length: the body ends when the server closes the connection.
  for (;;) {
    const int c = reader.next();
    if (c < 0) {
      return reader.failure() == BodyResult::Closed ? BodyResult::Complete : reader.failure();
    }
    if (out->size() >= limits.maxBytes) return BodyResult::TooLarge;
    out->push_back(static_cast<char>(c));
  }
}

const char* describe(BodyResult result) {
  switch (result) {
    case BodyResult::Complete:
      return "complete";
    case BodyResult::TooLarge:
      return "over the size cap";
    case BodyResult::TimedOut:
      return "timed out";
    case BodyResult::Closed:
      return "connection closed early";
    case BodyResult::Malformed:
      return "bad chunked encoding";
  }
  return "unknown";
}

}  // namespace ostium
