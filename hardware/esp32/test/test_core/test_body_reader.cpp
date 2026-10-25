// Reading a response body with a size cap and deadlines, so a stalled server
// can't block the door's loop.

#include <unity.h>

#include <vector>

#include "fakes.h"
#include "tests.h"

using fakes::FakeClock;
using ostium::BodyLimits;
using ostium::BodyResult;
using ostium::readBody;

namespace {

constexpr BodyLimits kLimits{8 * 1024, 3000, 6000};

// Delivers bytes at set times on the fake clock, and can stall or close.
class ScriptedSource : public ostium::ByteSource {
 public:
  explicit ScriptedSource(FakeClock& clock) : clock_(clock) {}

  void send(uint32_t atMs, const std::string& bytes) { parts_.push_back({atMs, bytes}); }
  void closeAt(uint32_t atMs) {
    closes_ = true;
    closeAtMs_ = atMs;
  }
  // Makes each byte read take this long, like a slow link that never pauses.
  void costPerByte(uint32_t ms) { msPerByte_ = ms; }

  size_t available() override {
    refill();
    return buffer_.size() - position_;
  }
  int read() override {
    refill();
    if (position_ >= buffer_.size()) return -1;
    clock_.advance(msPerByte_);
    return static_cast<unsigned char>(buffer_[position_++]);
  }
  bool connected() override { return !(closes_ && clock_.nowMs() >= closeAtMs_); }
  void wait() override { clock_.advance(1); }

  // Bytes delivered but not read, such as the start of the next response.
  size_t unread() {
    refill();
    return buffer_.size() - position_;
  }

 private:
  struct Part {
    uint32_t atMs;
    std::string bytes;
  };

  void refill() {
    while (next_ < parts_.size() && parts_[next_].atMs <= clock_.nowMs()) {
      buffer_ += parts_[next_++].bytes;
    }
  }

  FakeClock& clock_;
  std::vector<Part> parts_;
  size_t next_ = 0;
  std::string buffer_;
  size_t position_ = 0;
  bool closes_ = false;
  uint32_t closeAtMs_ = 0;
  uint32_t msPerByte_ = 0;
};

struct Read {
  BodyResult result;
  std::string body;
  uint32_t tookMs;
};

Read readFrom(ScriptedSource& source, FakeClock& clock, int64_t contentLength, bool chunked) {
  const uint32_t start = clock.nowMs();
  std::string body;
  const BodyResult result = readBody(source, clock, contentLength, chunked, kLimits, &body);
  return {result, body, clock.nowMs() - start};
}

}  // namespace

void test_body_read_stops_when_server_stalls() {
  {
    // Half the declared body arrives, then nothing, and the connection stays
    // open. The read ends after the idle limit.
    FakeClock clock;
    clock.set(10000);
    ScriptedSource source(clock);
    source.send(10000, "0123456789");
    const Read read = readFrom(source, clock, 20, false);
    TEST_ASSERT_TRUE(read.result == BodyResult::TimedOut);
    TEST_ASSERT_EQUAL_UINT32(kLimits.idleMs, read.tookMs);
  }
  {
    // One byte a second never trips the idle limit, but the total limit ends
    // the read before the 20-byte body is complete.
    FakeClock clock;
    ScriptedSource source(clock);
    for (uint32_t i = 0; i < 20; ++i) source.send(i * 1000, "x");
    const Read read = readFrom(source, clock, 20, false);
    TEST_ASSERT_TRUE(read.result == BodyResult::TimedOut);
    TEST_ASSERT_EQUAL_UINT32(kLimits.totalMs, read.tookMs);
  }
  {
    // A chunked body that stalls in a chunk-size line.
    FakeClock clock;
    ScriptedSource source(clock);
    source.send(0, "4\r\nabcd\r\n1");
    const Read read = readFrom(source, clock, -1, true);
    TEST_ASSERT_TRUE(read.result == BodyResult::TimedOut);
  }
  {
    // A body with no length that never closes.
    FakeClock clock;
    ScriptedSource source(clock);
    source.send(0, "{\"a\":1}");
    TEST_ASSERT_TRUE(readFrom(source, clock, -1, false).result == BodyResult::TimedOut);
  }
  {
    // Bytes that never stop arriving, 1 ms apart, still end at the total limit:
    // first in an endless run of trailer lines after the last chunk, then in
    // a long body.
    std::string trailers = "0\r\n";
    for (int i = 0; i < 2000; ++i) trailers += "X: y\r\n";
    FakeClock clock;
    ScriptedSource source(clock);
    source.costPerByte(1);
    source.send(0, trailers);
    const Read read = readFrom(source, clock, -1, true);
    TEST_ASSERT_TRUE(read.result == BodyResult::TimedOut);
    TEST_ASSERT_EQUAL_UINT32(kLimits.totalMs, read.tookMs);

    FakeClock bodyClock;
    ScriptedSource body(bodyClock);
    body.costPerByte(1);
    body.send(0, std::string(8000, 'x'));
    const Read bodyRead = readFrom(body, bodyClock, 8000, false);
    TEST_ASSERT_TRUE(bodyRead.result == BodyResult::TimedOut);
    TEST_ASSERT_EQUAL_UINT32(kLimits.totalMs, bodyRead.tookMs);
  }
}

void test_body_read_follows_content_length_and_chunks() {
  {
    // Exactly Content-Length bytes, arriving in parts, and nothing after them.
    FakeClock clock;
    ScriptedSource source(clock);
    source.send(0, "{\"server_");
    source.send(2500, "time\":1}NEXT");
    const Read read = readFrom(source, clock, 17, false);
    TEST_ASSERT_TRUE(read.result == BodyResult::Complete);
    TEST_ASSERT_EQUAL_STRING("{\"server_time\":1}", read.body.c_str());
    TEST_ASSERT_EQUAL_UINT32(4, source.unread());
  }
  {
    FakeClock clock;
    ScriptedSource source(clock);
    TEST_ASSERT_TRUE(readFrom(source, clock, 0, false).result == BodyResult::Complete);
  }
  {
    // Chunks with an extension and a trailer, then the next response.
    FakeClock clock;
    ScriptedSource source(clock);
    source.send(0, "4\r\nabcd\r\n3;name=value\r\nefg\r\n0\r\nX-Trailer: 1\r\n\r\nNEXT");
    const Read read = readFrom(source, clock, 99, true);
    TEST_ASSERT_TRUE(read.result == BodyResult::Complete);
    TEST_ASSERT_EQUAL_STRING("abcdefg", read.body.c_str());
    TEST_ASSERT_EQUAL_UINT32(4, source.unread());
  }
  {
    // HTTP allows spaces and tabs before an extension's semicolon.
    FakeClock clock;
    ScriptedSource source(clock);
    source.send(0, "4 ;name=value\r\nabcd\r\n2\t;x\r\nef\r\n0\r\n\r\n");
    const Read read = readFrom(source, clock, -1, true);
    TEST_ASSERT_TRUE(read.result == BodyResult::Complete);
    TEST_ASSERT_EQUAL_STRING("abcdef", read.body.c_str());
  }
  {
    // No length: the body ends when the server closes the connection.
    FakeClock clock;
    ScriptedSource source(clock);
    source.send(0, "{\"a\":1}");
    source.closeAt(100);
    const Read read = readFrom(source, clock, -1, false);
    TEST_ASSERT_TRUE(read.result == BodyResult::Complete);
    TEST_ASSERT_EQUAL_STRING("{\"a\":1}", read.body.c_str());
  }
}

void test_body_read_rejects_bad_or_large_bodies() {
  const std::string tooBig(kLimits.maxBytes + 1, 'x');
  {
    // A declared length over the cap isn't read at all.
    FakeClock clock;
    ScriptedSource source(clock);
    source.send(0, tooBig);
    TEST_ASSERT_TRUE(readFrom(source, clock, tooBig.size(), false).result ==
                     BodyResult::TooLarge);
    TEST_ASSERT_EQUAL_UINT32(tooBig.size(), source.unread());
  }
  {
    // The cap holds for a chunked body and for a body with no length.
    FakeClock clock;
    ScriptedSource chunked(clock);
    chunked.send(0, "2001\r\n" + tooBig + "\r\n0\r\n\r\n");
    TEST_ASSERT_TRUE(readFrom(chunked, clock, -1, true).result == BodyResult::TooLarge);
    ScriptedSource unbounded(clock);
    unbounded.send(clock.nowMs(), tooBig);
    unbounded.closeAt(clock.nowMs() + 100);
    TEST_ASSERT_TRUE(readFrom(unbounded, clock, -1, false).result == BodyResult::TooLarge);
  }
  {
    // A connection that closes before Content-Length bytes arrive.
    FakeClock clock;
    ScriptedSource source(clock);
    source.send(0, "01234");
    source.closeAt(50);
    TEST_ASSERT_TRUE(readFrom(source, clock, 10, false).result == BodyResult::Closed);
  }
  const char* badChunks[] = {
      "zz\r\nabc\r\n0\r\n\r\n",       // not a hex size
      "\r\nabc\r\n0\r\n\r\n",         // no size
      "123456789\r\n",                // more than 8 hex digits
      "3\r\nabcX\r\n0\r\n\r\n",       // no CRLF after the chunk data
      "1\r\na\n0\r\n\r\n",             // LF without CR after the chunk data
      "4 x\r\nabcd\r\n0\r\n\r\n",      // junk after the size
  };
  for (const char* body : badChunks) {
    FakeClock clock;
    ScriptedSource source(clock);
    source.send(0, body);
    TEST_ASSERT_TRUE_MESSAGE(readFrom(source, clock, -1, true).result == BodyResult::Malformed,
                             body);
  }
  {
    // A chunk-size line longer than 64 bytes.
    FakeClock clock;
    ScriptedSource source(clock);
    source.send(0, "1;" + std::string(80, 'e') + "\r\nx\r\n0\r\n\r\n");
    TEST_ASSERT_TRUE(readFrom(source, clock, -1, true).result == BodyResult::Malformed);
  }
}
