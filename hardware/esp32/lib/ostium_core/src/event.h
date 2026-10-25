#pragma once

#include <cstdint>
#include <string>

namespace ostium {

namespace reason {
constexpr const char* kDuplicateCacheFull = "duplicate_cache_full";
constexpr const char* kMalformedCommand = "malformed_command";
constexpr const char* kInvalidAction = "invalid_action";
constexpr const char* kWrongDoor = "wrong_door";
constexpr const char* kIssuedBeforeBoot = "issued_before_boot";
constexpr const char* kExpired = "expired";
constexpr const char* kUnlockFailed = "unlock_failed";
}  // namespace reason

enum class Result { Done, Rejected };

// The event ID for a command. Derived from the command ID, so it's the same on
// every retry and after a reset (R13).
inline std::string eventIdFor(const std::string& commandId) { return "evt-" + commandId; }

// One report for POST /devices/{device_id}/events.
struct Event {
  std::string commandId;
  bool hasAction = false;  // False sends "action": null.
  std::string action;
  Result result = Result::Done;
  std::string reason;  // A reason:: code when result is Rejected.
  int64_t occurredAtMs = 0;

  std::string eventId() const { return eventIdFor(commandId); }
};

// Serializes the event as the JSON request body.
std::string serializeEvent(const Event& event);

}  // namespace ostium
