#include "camera_scan.h"

namespace ostium {

bool scanFromCameraDecode(bool valid, const uint8_t* payload, int length, size_t capacity,
                          uint32_t decodedAtMs, QrScan* scan) {
  if (!valid || payload == nullptr || scan == nullptr || length < 0) return false;
  if (static_cast<size_t>(length) > capacity) return false;
  scan->text.assign(reinterpret_cast<const char*>(payload), static_cast<size_t>(length));
  scan->decodedAtMs = decodedAtMs;
  return true;
}

}  // namespace ostium
