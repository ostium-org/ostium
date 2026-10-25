#pragma once

#include <cstdint>
#include <string>

#include "duplicate_list.h"
#include "event_outbox.h"
#include "interfaces.h"
#include "lock_controller.h"
#include "poll_parser.h"

namespace ostium {

// Runs the commands from each successful poll through the contract's checks,
// drives the lock controller, and queues one event per command.
class CommandProcessor {
 public:
  // Added to the estimated start time so the R5 check errs toward rejecting.
  static constexpr int64_t kStartTimeMarginMs = 2000;
  // How long to remember the ID of a command with no valid expires_at.
  static constexpr int64_t kFallbackRetentionMs = 10000;

  // deviceId is this door unit's device key. The backend uses it as the
  // door_id of every command for this door (R6).
  CommandProcessor(const std::string& deviceId, LockController& lock, EventOutbox& outbox,
                   const Clock& clock, Log& log);

  // Runs the commands in poll, in response order (R2). requestStartedAtMs is
  // the Clock time when the poll request started; runPoll() records it. The
  // server stamps server_time after that, so measuring from the request start
  // never puts the start time or the current time earlier than they are (R4,
  // R5). A poll that isn't ok runs nothing (R16); the caller backs off.
  void handlePoll(const PollResult& poll, uint32_t requestStartedAtMs);

  // The door's start time on the server clock, set once from the first
  // successful poll (R5).
  bool hasStartTime() const { return hasStartTime_; }
  int64_t startTimeMs() const { return startTimeMs_; }

 private:
  void handleCommand(const ParsedCommand& command, int64_t nowEpochMs, int64_t serverTimeMs);
  void reject(const ParsedCommand& command, const char* reasonCode, int64_t nowEpochMs);
  void report(const ParsedCommand& command, Result result, const char* reasonCode,
              int64_t nowEpochMs);

  const std::string deviceId_;
  LockController& lock_;
  EventOutbox& outbox_;
  const Clock& clock_;
  Log& log_;
  DuplicateList handled_;
  bool hasStartTime_ = false;
  int64_t startTimeMs_ = 0;
};

}  // namespace ostium
