#include "duplicate_list.h"

namespace ostium {

DuplicateList::DuplicateList() { entries_.reserve(kCapacity); }

bool DuplicateList::contains(const std::string& commandId) const {
  for (const Entry& entry : entries_) {
    if (entry.commandId == commandId) return true;
  }
  return false;
}

bool DuplicateList::add(const std::string& commandId, int64_t expiresAtEpochMs,
                        int64_t nowEpochMs) {
  for (size_t i = 0; i < entries_.size();) {
    if (entries_[i].expiresAtEpochMs <= nowEpochMs) {
      entries_.erase(entries_.begin() + static_cast<std::ptrdiff_t>(i));
    } else {
      ++i;
    }
  }
  if (entries_.size() >= kCapacity) return false;
  entries_.push_back({commandId, expiresAtEpochMs});
  return true;
}

}  // namespace ostium
