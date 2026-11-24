#include "duplicate_list.h"

namespace ostium {

DuplicateList::DuplicateList() { entries_.reserve(kCapacity); }

bool DuplicateList::contains(const std::string& id) const {
  for (const Entry& entry : entries_) {
    if (entry.id == id) return true;
  }
  return false;
}

void DuplicateList::removeExpired(int64_t nowEpochMs, uint32_t nowMs) {
  for (size_t i = 0; i < entries_.size();) {
    // Unsigned elapsed time, so a hold survives millis() wrapping. An ID with
    // no hold passes this check at any nowMs. A held ID left unchecked for
    // 49.7 days looks held again for at most holdMs, which only keeps it longer.
    const bool held = nowMs - entries_[i].addedAtMs < entries_[i].holdMs;
    if (entries_[i].expiresAtEpochMs <= nowEpochMs && !held) {
      entries_.erase(entries_.begin() + static_cast<std::ptrdiff_t>(i));
    } else {
      ++i;
    }
  }
}

bool DuplicateList::add(const std::string& id, int64_t expiresAtEpochMs, int64_t nowEpochMs,
                        uint32_t nowMs, uint32_t holdMs) {
  removeExpired(nowEpochMs, nowMs);
  if (entries_.size() >= kCapacity) return false;
  entries_.push_back({id, expiresAtEpochMs, nowMs, holdMs});
  return true;
}

}  // namespace ostium
