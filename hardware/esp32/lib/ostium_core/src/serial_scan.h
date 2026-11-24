#pragma once

#include <cstddef>
#include <string>

#include "interfaces.h"

namespace ostium {

// Bench input for QR access (Q1). Splits Serial bytes into lines and picks out
// "qr <token>" lines, as if the camera had decoded the token. It's plain C++,
// so the native tests cover it. On the board, board::SerialScanSource feeds it
// in the esp32s3-bench build only.
class SerialScanParser {
 public:
  static constexpr size_t kMaxLineBytes = 1024;

  // Feeds one byte. CR, LF, and CRLF each end a line: an LF right after a CR
  // ends nothing. Returns true and sets *token when a line of the form
  // "qr <token>" ends, with a token of at least one byte. It also does these
  // things:
  // - ignores empty lines;
  // - discards a line over kMaxLineBytes, not counting its line end, through
  //   that line end, logging "serial line over 1024 bytes, ignored" once;
  // - for any other non-empty line, logs: bench input: send "qr <token>" to
  //   scan a QR payload.
  // No log line contains the line's text, because a token is a credential.
  bool feed(char c, std::string* token, Log& log);

 private:
  std::string line_;  // At most kMaxLineBytes bytes, never a CR or LF.
  bool overflow_ = false;
  bool lastWasCr_ = false;  // The previous byte was a CR.
};

}  // namespace ostium
