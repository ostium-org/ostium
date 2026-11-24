#include "serial_scan.h"

namespace ostium {
namespace {

constexpr const char* kPrefix = "qr ";
constexpr size_t kPrefixLength = 3;
constexpr const char* kLineTooLong = "serial line over 1024 bytes, ignored";
constexpr const char* kUsage = "bench input: send \"qr <token>\" to scan a QR payload";

}  // namespace

bool SerialScanParser::feed(char c, std::string* token, Log& log) {
  // CR, LF, and CRLF each end a line, so a terminal's Enter key works however
  // it's set. An LF right after a CR is the rest of a CRLF, so it ends nothing.
  const bool afterCr = lastWasCr_;
  lastWasCr_ = c == '\r';
  if (c == '\n' && afterCr) return false;
  if (c != '\r' && c != '\n') {
    // A byte past kMaxLineBytes makes the line too long.
    if (!overflow_ && line_.size() >= kMaxLineBytes) {
      overflow_ = true;
      line_.clear();
      log.write(kLineTooLong);
    }
    if (!overflow_) line_.push_back(c);
    return false;
  }

  // A line end. The rest of a discarded line was already logged.
  if (overflow_) {
    overflow_ = false;
    return false;
  }
  bool found = false;
  if (line_.size() > kPrefixLength && line_.compare(0, kPrefixLength, kPrefix) == 0) {
    token->assign(line_, kPrefixLength, std::string::npos);
    found = true;
  } else if (!line_.empty()) {
    log.write(kUsage);
  }
  line_.clear();
  return found;
}

}  // namespace ostium
