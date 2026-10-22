#include "command_processor.h"

namespace ostium {

CommandProcessor::CommandProcessor(const std::string& doorId, LockController& lock,
                                   EventOutbox& outbox, const Clock& clock, Log& log)
    : doorId_(doorId), lock_(lock), outbox_(outbox), clock_(clock), log_(log) {}

void CommandProcessor::handlePoll(const PollResult& poll, uint32_t receivedAtMs) {
  if (!poll.ok) return;

  if (!hasStartTime_) {
    startTimeMs_ = poll.serverTimeMs - static_cast<int64_t>(receivedAtMs) + kStartTimeMarginMs;
    hasStartTime_ = true;
  }
  if (poll.deferred > 0) {
    log_.write("left " + std::to_string(poll.deferred) + " commands for a later poll");
  }
  for (const ParsedCommand& command : poll.commands) {
    // R4: server_time plus the time elapsed since the response arrived.
    const uint32_t elapsedMs = clock_.nowMs() - receivedAtMs;
    handleCommand(command, poll.serverTimeMs + static_cast<int64_t>(elapsedMs));
  }
}

void CommandProcessor::handleCommand(const ParsedCommand& command, int64_t nowEpochMs) {
  // The checks run in the contract's order, and the first failure wins.
  if (command.problem == CommandProblem::NoCommandId) {
    log_.write(std::string("skipped command with no usable command_id (") + command.detail +
               "): " + command.received);
    return;
  }
  // R3. A command rejected with duplicate_cache_full isn't in handled_, but
  // while its rejected event is queued it must not run or report again.
  if (handled_.contains(command.commandId)) return;
  if (outbox_.contains(eventIdFor(command.commandId))) return;

  const int64_t retainUntil =
      command.hasExpiry ? command.expiresAtMs : nowEpochMs + kFallbackRetentionMs;
  if (!handled_.add(command.commandId, retainUntil, nowEpochMs)) {
    reject(command, reason::kDuplicateCacheFull, nowEpochMs);
    return;
  }
  if (command.problem == CommandProblem::Malformed) {  // R7
    reject(command, reason::kMalformedCommand, nowEpochMs);
    return;
  }
  if (command.problem == CommandProblem::InvalidAction) {  // R8
    reject(command, reason::kInvalidAction, nowEpochMs);
    return;
  }
  if (command.doorId != doorId_) {  // R6
    reject(command, reason::kWrongDoor, nowEpochMs);
    return;
  }
  if (command.issuedAtMs < startTimeMs_) {  // R5
    reject(command, reason::kIssuedBeforeBoot, nowEpochMs);
    return;
  }
  if (command.expiresAtMs <= nowEpochMs) {  // R4
    reject(command, reason::kExpired, nowEpochMs);
    return;
  }

  if (command.action == Action::Unlock) {
    if (!lock_.unlock()) {  // R9, R10
      reject(command, reason::kUnlockFailed, nowEpochMs);
      return;
    }
  } else {
    lock_.lock();  // R11
  }
  report(command, Result::Done, nullptr, nowEpochMs);
}

void CommandProcessor::reject(const ParsedCommand& command, const char* reasonCode,
                              int64_t nowEpochMs) {
  log_.write("rejected command " + command.commandId + ": " + reasonCode);
  report(command, Result::Rejected, reasonCode, nowEpochMs);
}

void CommandProcessor::report(const ParsedCommand& command, Result result,
                              const char* reasonCode, int64_t nowEpochMs) {
  Event event;
  event.commandId = command.commandId;
  event.hasAction = command.hasAction;
  event.action = command.actionText;
  event.result = result;
  if (reasonCode != nullptr) event.reason = reasonCode;
  event.occurredAtMs = nowEpochMs;
  outbox_.push(event, clock_.nowMs());
}

}  // namespace ostium
