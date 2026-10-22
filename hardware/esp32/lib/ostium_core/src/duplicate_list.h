#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace ostium {

// Command IDs the door has already run or rejected (R3). Each ID stays until
// its command expires, and the list never removes an unexpired ID.
class DuplicateList {
 public:
  static constexpr size_t kCapacity = 32;

  DuplicateList();

  bool contains(const std::string& commandId) const;

  // Removes IDs whose expiry is at or before nowEpochMs, then adds commandId.
  // Returns false, without adding it, if the list is full of unexpired IDs.
  bool add(const std::string& commandId, int64_t expiresAtEpochMs, int64_t nowEpochMs);

  size_t size() const { return entries_.size(); }

 private:
  struct Entry {
    std::string commandId;
    int64_t expiresAtEpochMs;
  };

  std::vector<Entry> entries_;
};

}  // namespace ostium
