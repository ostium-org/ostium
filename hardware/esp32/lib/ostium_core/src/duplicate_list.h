#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace ostium {

// IDs the door must remember until they expire: command IDs it has already run
// or rejected (R3), credentials it granted (Q13), revoked credentials (Q12),
// and credentials with a reported denial (Q18). Each ID stays until its
// expiry, and the list never removes an unexpired ID.
//
// An ID can also have a hold on the local clock. The epoch time that the QR
// lists compare with comes from the server's clock, which can move backward
// when the door restarts its time bounds. A held ID stays until its expiry has
// passed and holdMs of local time has passed since it was added, so a restart
// that the door sees while it holds the ID can't make it valid again (Q13).
class DuplicateList {
 public:
  static constexpr size_t kCapacity = 32;

  DuplicateList();

  bool contains(const std::string& id) const;

  // Removes IDs whose expiry is at or before nowEpochMs, except held IDs whose
  // hold hasn't passed at nowMs. Lists that use holds must pass nowMs.
  void removeExpired(int64_t nowEpochMs, uint32_t nowMs = 0);

  // Removes expired IDs as removeExpired does, then adds id. With holdMs set,
  // id also stays until holdMs has passed after nowMs on the local clock.
  // Returns false, without adding it, if the list is full of IDs it can't
  // remove yet.
  bool add(const std::string& id, int64_t expiresAtEpochMs, int64_t nowEpochMs,
           uint32_t nowMs = 0, uint32_t holdMs = 0);

  size_t size() const { return entries_.size(); }

 private:
  struct Entry {
    std::string id;
    int64_t expiresAtEpochMs;
    uint32_t addedAtMs;  // local time of add()
    uint32_t holdMs;     // 0 for no hold
  };

  std::vector<Entry> entries_;
};

}  // namespace ostium
