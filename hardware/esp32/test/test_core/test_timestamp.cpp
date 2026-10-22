// Timestamp parsing and formatting: R15.

#include <unity.h>

#include "fakes.h"
#include "tests.h"

using ostium::formatTimestamp;
using ostium::parseTimestamp;

namespace {

int64_t parsed(const char* text) {
  int64_t epochMs = -1;
  TEST_ASSERT_TRUE_MESSAGE(parseTimestamp(text, &epochMs), text);
  return epochMs;
}

}  // namespace

void test_timestamp_parser_accepts_utc_formats() {
  const int64_t base = fakes::kBaseEpochMs;  // 2026-10-22T15:00:00.000Z

  TEST_ASSERT_EQUAL_INT64(base, parsed("2026-10-22T15:00:00Z"));
  TEST_ASSERT_EQUAL_INT64(base + 100, parsed("2026-10-22T15:00:00.1Z"));
  TEST_ASSERT_EQUAL_INT64(base + 120, parsed("2026-10-22T15:00:00.12Z"));
  TEST_ASSERT_EQUAL_INT64(base + 123, parsed("2026-10-22T15:00:00.123Z"));
  TEST_ASSERT_EQUAL_INT64(base + 123, parsed("2026-10-22T15:00:00.123456Z"));
  TEST_ASSERT_EQUAL_INT64(base + 123, parsed("2026-10-22T15:00:00.123456789Z"));
  TEST_ASSERT_EQUAL_INT64(base, parsed("2026-10-22T15:00:00+00:00"));
  TEST_ASSERT_EQUAL_INT64(base, parsed("2026-10-22T15:00:00.000-00:00"));
  TEST_ASSERT_EQUAL_INT64(base, parsed("2026-10-22T17:30:00+02:30"));
  TEST_ASSERT_EQUAL_INT64(base, parsed("2026-10-22T11:30:00-03:30"));
  TEST_ASSERT_EQUAL_INT64(0, parsed("1970-01-01T00:00:00Z"));
  TEST_ASSERT_EQUAL_INT64(1835481599999LL, parsed("2028-02-29T23:59:59.999Z"));

  const char* rejected[] = {
      "2026-10-22T15:00:00",             // No offset.
      "2026-10-22 15:00:00Z",            // Space instead of T.
      "2026-10-22T15:00:00.Z",           // Dot with no digits.
      "2026-10-22T15:00:00.1234567890Z", // 10 fractional digits.
      "2026-10-22T15:00:00+0000",        // Offset with no colon.
      "2026-10-22T15:00:00+24:00",       // Offset hour out of range.
      "2026-13-01T00:00:00Z",            // Month 13.
      "2026-02-29T00:00:00Z",            // Not a leap year.
      "2026-04-31T00:00:00Z",            // April has 30 days.
      "2026-10-22T24:00:00Z",            // Hour 24.
      "2026-10-22T15:60:00Z",            // Minute 60.
      "2026-10-22T15:00:60Z",            // Second 60.
      "2026-10-22T15:00:00Zjunk",        // Trailing text.
      "2026-10-22",                      // Date only.
      "",
      "yesterday",
  };
  for (const char* text : rejected) {
    int64_t epochMs = 0;
    TEST_ASSERT_FALSE_MESSAGE(parseTimestamp(text, &epochMs), text);
  }
  int64_t unused = 0;
  TEST_ASSERT_FALSE(parseTimestamp(nullptr, &unused));
}

void test_timestamp_formats_as_utc_with_milliseconds() {
  TEST_ASSERT_EQUAL_STRING("2026-10-22T15:00:01.250Z",
                           formatTimestamp(fakes::kBaseEpochMs + 1250).c_str());
  TEST_ASSERT_EQUAL_STRING("1970-01-01T00:00:00.000Z", formatTimestamp(0).c_str());
  TEST_ASSERT_EQUAL_STRING("1969-12-31T23:59:59.999Z", formatTimestamp(-1).c_str());
  TEST_ASSERT_EQUAL_STRING("2028-02-29T23:59:59.999Z", formatTimestamp(1835481599999LL).c_str());

  // Formatting and parsing round-trip.
  const int64_t samples[] = {0, fakes::kBaseEpochMs, fakes::kBaseEpochMs + 86399999,
                             1835481599999LL, 4102444800000LL};
  for (int64_t sample : samples) {
    int64_t back = -1;
    TEST_ASSERT_TRUE(parseTimestamp(formatTimestamp(sample).c_str(), &back));
    TEST_ASSERT_EQUAL_INT64(sample, back);
  }
}
