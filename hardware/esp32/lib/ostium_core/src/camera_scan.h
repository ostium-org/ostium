#pragma once

#include <cstddef>
#include <cstdint>

#include "interfaces.h"

namespace ostium {

// Turns one camera decode into a QrScan (Q1). The camera task hands over a
// fixed-size payload buffer, the decoded length, and a valid flag, and this
// checks them, so board::CameraScanSource stays a thin wrapper that the native
// tests don't need to reach.
//
// Returns false and leaves *scan unchanged for a failed decode (valid is false,
// and the buffer holds the decoder's error text), a null buffer, or a length
// below 0 or above capacity. Otherwise it copies exactly `length` bytes, so a
// payload that holds a 0 byte isn't cut short, sets decodedAtMs, and returns
// true.
bool scanFromCameraDecode(bool valid, const uint8_t* payload, int length, size_t capacity,
                          uint32_t decodedAtMs, QrScan* scan);

}  // namespace ostium
