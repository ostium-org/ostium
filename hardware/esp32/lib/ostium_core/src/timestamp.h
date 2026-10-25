#pragma once

#include <cstdint>
#include <string>

namespace ostium {

// Parses an ISO 8601 timestamp such as 2026-10-22T15:00:01.200Z into
// milliseconds since the Unix epoch (R15). Accepts 0 to 9 fractional digits
// and a Z or +hh:mm / -hh:mm offset. Rejects timestamps with no offset.
// Fractions finer than a millisecond are truncated.
bool parseTimestamp(const char* text, int64_t* epochMs);

// Formats milliseconds since the Unix epoch as YYYY-MM-DDTHH:MM:SS.mmmZ.
std::string formatTimestamp(int64_t epochMs);

}  // namespace ostium
