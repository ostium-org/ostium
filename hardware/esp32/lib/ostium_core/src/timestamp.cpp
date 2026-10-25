#include "timestamp.h"

#include <cstdio>

namespace ostium {
namespace {

constexpr int64_t kMsPerDay = 86400000;

// Reads exactly count digits. Stops at the first non-digit, including the
// string terminator, so it never reads past the end of text.
bool readDigits(const char* text, int count, int* value) {
  int result = 0;
  for (int i = 0; i < count; ++i) {
    if (text[i] < '0' || text[i] > '9') return false;
    result = result * 10 + (text[i] - '0');
  }
  *value = result;
  return true;
}

bool isLeapYear(int year) {
  return (year % 4 == 0 && year % 100 != 0) || year % 400 == 0;
}

int daysInMonth(int year, int month) {
  static const int kDays[] = {31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31};
  return month == 2 && isLeapYear(year) ? 29 : kDays[month - 1];
}

// Days since 1970-01-01 in the proleptic Gregorian calendar, from Howard
// Hinnant's days_from_civil and civil_from_days algorithms.
int64_t daysFromCivil(int year, int month, int day) {
  year -= month <= 2 ? 1 : 0;
  const int64_t era = (year >= 0 ? year : year - 399) / 400;
  const int64_t yearOfEra = year - era * 400;
  const int64_t dayOfYear = (153 * (month + (month > 2 ? -3 : 9)) + 2) / 5 + day - 1;
  const int64_t dayOfEra = yearOfEra * 365 + yearOfEra / 4 - yearOfEra / 100 + dayOfYear;
  return era * 146097 + dayOfEra - 719468;
}

void civilFromDays(int64_t days, int* year, int* month, int* day) {
  days += 719468;
  const int64_t era = (days >= 0 ? days : days - 146096) / 146097;
  const int64_t dayOfEra = days - era * 146097;
  const int64_t yearOfEra =
      (dayOfEra - dayOfEra / 1460 + dayOfEra / 36524 - dayOfEra / 146096) / 365;
  const int64_t dayOfYear = dayOfEra - (365 * yearOfEra + yearOfEra / 4 - yearOfEra / 100);
  const int64_t monthIndex = (5 * dayOfYear + 2) / 153;
  *day = static_cast<int>(dayOfYear - (153 * monthIndex + 2) / 5 + 1);
  *month = static_cast<int>(monthIndex < 10 ? monthIndex + 3 : monthIndex - 9);
  *year = static_cast<int>(yearOfEra + era * 400 + (*month <= 2 ? 1 : 0));
}

}  // namespace

bool parseTimestamp(const char* text, int64_t* epochMs) {
  if (text == nullptr || epochMs == nullptr) return false;

  int year, month, day, hour, minute, second;
  const char* p = text;
  if (!readDigits(p, 4, &year) || p[4] != '-') return false;
  p += 5;
  if (!readDigits(p, 2, &month) || p[2] != '-') return false;
  p += 3;
  if (!readDigits(p, 2, &day) || p[2] != 'T') return false;
  p += 3;
  if (!readDigits(p, 2, &hour) || p[2] != ':') return false;
  p += 3;
  if (!readDigits(p, 2, &minute) || p[2] != ':') return false;
  p += 3;
  if (!readDigits(p, 2, &second)) return false;
  p += 2;

  int millis = 0;
  if (*p == '.') {
    ++p;
    int digits = 0;
    while (*p >= '0' && *p <= '9') {
      if (digits < 3) millis = millis * 10 + (*p - '0');
      ++digits;
      ++p;
    }
    if (digits == 0 || digits > 9) return false;
    for (int i = digits; i < 3; ++i) millis *= 10;
  }

  int64_t offsetMs = 0;
  if (*p == 'Z') {
    ++p;
  } else if (*p == '+' || *p == '-') {
    const int sign = *p == '+' ? 1 : -1;
    ++p;
    int offsetHour, offsetMinute;
    if (!readDigits(p, 2, &offsetHour) || p[2] != ':') return false;
    p += 3;
    if (!readDigits(p, 2, &offsetMinute)) return false;
    p += 2;
    if (offsetHour > 23 || offsetMinute > 59) return false;
    offsetMs = sign * (offsetHour * 3600000LL + offsetMinute * 60000LL);
  } else {
    return false;
  }
  if (*p != '\0') return false;

  if (month < 1 || month > 12) return false;
  if (day < 1 || day > daysInMonth(year, month)) return false;
  if (hour > 23 || minute > 59 || second > 59) return false;

  *epochMs = daysFromCivil(year, month, day) * kMsPerDay + hour * 3600000LL +
             minute * 60000LL + second * 1000LL + millis - offsetMs;
  return true;
}

std::string formatTimestamp(int64_t epochMs) {
  int64_t days = epochMs / kMsPerDay;
  int64_t msOfDay = epochMs % kMsPerDay;
  if (msOfDay < 0) {
    msOfDay += kMsPerDay;
    --days;
  }
  int year, month, day;
  civilFromDays(days, &year, &month, &day);

  char buffer[32];
  std::snprintf(buffer, sizeof(buffer), "%04d-%02d-%02dT%02d:%02d:%02d.%03dZ", year, month,
                day, static_cast<int>(msOfDay / 3600000), static_cast<int>(msOfDay / 60000 % 60),
                static_cast<int>(msOfDay / 1000 % 60), static_cast<int>(msOfDay % 1000));
  return buffer;
}

}  // namespace ostium
