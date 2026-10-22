#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

#include "event.h"
#include "interfaces.h"

namespace ostium {

// Queue of events waiting to be posted. Each event has its own retry backoff,
// so a failing event never holds up later events, polls, or lock control
// (R14).
class EventOutbox {
 public:
  static constexpr size_t kCapacity = 8;

  explicit EventOutbox(Log& log);

  // Queues the event. When the queue is full, drops and logs the oldest event.
  void push(const Event& event, uint32_t nowMs);

  // Posts at most one event that's due, and handles the response as the
  // contract's response table says. Call it from the main loop between polls.
  // The retry backoff starts when the post returns, so a slow request doesn't
  // shorten it.
  void service(EventSender& sender, const Clock& clock);

  size_t size() const { return entries_.size(); }
  bool contains(const std::string& eventId) const;

 private:
  struct Entry {
    std::string eventId;
    std::string body;
    uint32_t nextAttemptMs;
    uint32_t failures;
  };

  Log& log_;
  std::vector<Entry> entries_;
};

}  // namespace ostium
