// The bench build's Serial input for QR scans: Q1.

#include <unity.h>

#include <string>
#include <vector>

#include "fakes.h"
#include "tests.h"

using fakes::FakeLog;
using ostium::SerialScanParser;

namespace {

constexpr const char* kUsage = "bench input: send \"qr <token>\" to scan a QR payload";
constexpr const char* kLineTooLong = "serial line over 1024 bytes, ignored";

// Feeds each byte in order and returns the tokens the parser found.
std::vector<std::string> feed(SerialScanParser& parser, const std::string& bytes, FakeLog& log) {
  std::vector<std::string> tokens;
  std::string token;
  for (const char c : bytes) {
    if (parser.feed(c, &token, log)) tokens.push_back(token);
  }
  return tokens;
}

size_t countLines(const FakeLog& log, const std::string& message) {
  size_t count = 0;
  for (const std::string& line : log.messages) {
    if (line == message) ++count;
  }
  return count;
}

}  // namespace

void test_serial_scan_lines_are_parsed() {
  // A full backend-sized token, pasted from the monitor.
  const std::string token = ostium::kQrSelfTestToken;

  // CRLF, LF, and CR each end a line, so every serial terminal's Enter key
  // works. Each line gives the token, without the line end, and logs nothing.
  {
    SerialScanParser parser;
    FakeLog log;
    const std::vector<std::string> tokens = feed(
        parser, "qr " + token + "\r\n" + "qr " + token + "\n" + "qr " + token + "\r", log);
    TEST_ASSERT_EQUAL_UINT32(3, tokens.size());
    for (const std::string& found : tokens) {
      TEST_ASSERT_EQUAL_STRING(token.c_str(), found.c_str());
    }
    TEST_ASSERT_EQUAL_UINT32(0, log.messages.size());
  }

  // A terminal that sends only CR, such as screen, PuTTY, or minicom.
  {
    SerialScanParser parser;
    FakeLog log;
    const std::vector<std::string> tokens = feed(parser, "qr a\rqr b\r\rqr c\r", log);
    TEST_ASSERT_EQUAL_UINT32(3, tokens.size());
    TEST_ASSERT_EQUAL_STRING("a", tokens[0].c_str());
    TEST_ASSERT_EQUAL_STRING("b", tokens[1].c_str());
    TEST_ASSERT_EQUAL_STRING("c", tokens[2].c_str());
    TEST_ASSERT_EQUAL_UINT32(0, log.messages.size());
  }

  // Mixed line ends. Only an LF right after a CR is ignored, as the second
  // half of a CRLF. Any other LF or CR ends a line, which may be empty.
  {
    SerialScanParser parser;
    FakeLog log;
    const std::vector<std::string> tokens =
        feed(parser, "qr a\nqr b\r\nqr c\n\rqr d\r\n\nqr e\r\r\nhelp\r\n", log);
    TEST_ASSERT_EQUAL_UINT32(5, tokens.size());
    const char* const expected[] = {"a", "b", "c", "d", "e"};
    for (size_t i = 0; i < tokens.size(); ++i) {
      TEST_ASSERT_EQUAL_STRING(expected[i], tokens[i].c_str());
    }
    TEST_ASSERT_EQUAL_UINT32(1, log.messages.size());
    TEST_ASSERT_EQUAL_STRING(kUsage, log.messages[0].c_str());
  }

  // Empty lines give nothing and log nothing. Any other line gives no token
  // and logs the usage line, never the line's text.
  {
    SerialScanParser parser;
    FakeLog log;
    TEST_ASSERT_TRUE(feed(parser, "\n\r\n\r\r", log).empty());
    TEST_ASSERT_EQUAL_UINT32(0, log.messages.size());
    TEST_ASSERT_TRUE(feed(parser, "help\n", log).empty());
    TEST_ASSERT_EQUAL_UINT32(1, log.messages.size());
    TEST_ASSERT_EQUAL_STRING(kUsage, log.messages[0].c_str());

    const std::string others[] = {"qr\n", "qr \r\n", "qr \r", "QR " + token + "\n",
                                  " qr " + token + "\n", "qr\t" + token + "\n"};
    for (const std::string& line : others) {
      TEST_ASSERT_TRUE_MESSAGE(feed(parser, line, log).empty(), line.c_str());
    }
    TEST_ASSERT_EQUAL_UINT32(7, countLines(log, kUsage));
    TEST_ASSERT_FALSE(log.contains("help"));
    TEST_ASSERT_FALSE(log.contains(token.substr(0, 20)));
  }

  // A token never contains a line end: a second CR ends an empty line.
  {
    SerialScanParser parser;
    FakeLog log;
    const std::vector<std::string> tokens = feed(parser, "qr x\r\r\n", log);
    TEST_ASSERT_EQUAL_UINT32(1, tokens.size());
    TEST_ASSERT_EQUAL_STRING("x", tokens[0].c_str());
    TEST_ASSERT_EQUAL_UINT32(0, log.messages.size());
  }

  // A 1,100-byte line is discarded through its line end and logged once, and
  // the next line still parses, whichever line end the terminal sends.
  for (const char* ending : {"\n", "\r\n", "\r"}) {
    SerialScanParser parser;
    FakeLog log;
    const std::vector<std::string> tokens = feed(
        parser, "qr " + std::string(1097, 'A') + ending + "qr " + token + ending, log);
    TEST_ASSERT_EQUAL_UINT32(1, tokens.size());
    TEST_ASSERT_EQUAL_STRING(token.c_str(), tokens[0].c_str());
    TEST_ASSERT_EQUAL_UINT32(1, log.messages.size());
    TEST_ASSERT_EQUAL_STRING(kLineTooLong, log.messages[0].c_str());
  }

  // The limit counts the line without its line end: 1,024 bytes pass with
  // any line end, and 1,025 bytes don't.
  {
    SerialScanParser parser;
    FakeLog log;
    const std::string longest = "qr " + std::string(1021, 'B');
    TEST_ASSERT_EQUAL_UINT32(SerialScanParser::kMaxLineBytes, longest.size());
    for (const char* ending : {"\r\n", "\n", "\r"}) {
      const std::vector<std::string> tokens = feed(parser, longest + ending, log);
      TEST_ASSERT_EQUAL_UINT32(1, tokens.size());
      TEST_ASSERT_EQUAL_UINT32(1021, tokens[0].size());
    }
    TEST_ASSERT_EQUAL_UINT32(0, log.messages.size());
    for (const char* ending : {"\r\n", "\n", "\r"}) {
      TEST_ASSERT_TRUE(feed(parser, longest + "B" + ending, log).empty());
    }
    TEST_ASSERT_EQUAL_UINT32(3, countLines(log, kLineTooLong));
    TEST_ASSERT_EQUAL_UINT32(3, log.messages.size());
  }

  // Bytes split across calls still parse, and a line can start in the same
  // call that ends the one before it. The CR ends the line, and the LF of
  // that CRLF, arriving in the next call, is still ignored.
  {
    SerialScanParser parser;
    FakeLog log;
    TEST_ASSERT_TRUE(feed(parser, "qr " + token.substr(0, 100), log).empty());
    std::vector<std::string> tokens = feed(parser, token.substr(100) + "\r", log);
    TEST_ASSERT_EQUAL_UINT32(1, tokens.size());
    TEST_ASSERT_EQUAL_STRING(token.c_str(), tokens[0].c_str());
    TEST_ASSERT_TRUE(feed(parser, "\nqr", log).empty());
    tokens = feed(parser, " y\n", log);
    TEST_ASSERT_EQUAL_UINT32(1, tokens.size());
    TEST_ASSERT_EQUAL_STRING("y", tokens[0].c_str());
    TEST_ASSERT_EQUAL_UINT32(0, log.messages.size());
  }
}
