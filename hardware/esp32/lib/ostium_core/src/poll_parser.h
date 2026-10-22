#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace ostium {

constexpr size_t kMaxPollBodyBytes = 8 * 1024;
constexpr size_t kMaxCommandIdLength = 128;
// A longer action counts as missing, which keeps event bodies under 1 KiB.
constexpr size_t kMaxActionLength = 16;
// Commands past this many in one response wait for a later poll.
constexpr size_t kMaxCommandsPerPoll = 32;
// How much of an unusable command the log keeps.
constexpr size_t kMaxLoggedCommandBytes = 200;

enum class Action { None, Lock, Unlock };

// The first problem found in a command, in the contract's check order.
enum class CommandProblem {
  None,
  NoCommandId,    // R7: logged and skipped, no event
  Malformed,      // R7: malformed_command
  InvalidAction,  // R8: invalid_action
};

struct ParsedCommand {
  std::string commandId;   // Empty when problem is NoCommandId.
  bool hasAction = false;  // False when action is missing, too long, or not a string.
  std::string actionText;  // The action as received, echoed in the event.
  Action action = Action::None;
  std::string doorId;
  int64_t issuedAtMs = 0;
  bool hasExpiry = false;  // False when expires_at is missing or invalid.
  int64_t expiresAtMs = 0;
  CommandProblem problem = CommandProblem::None;
  const char* detail = "";  // Why the command has a problem, for the log.
  std::string received;     // The start of the command as received, set only for NoCommandId.
};

struct PollResult {
  bool ok = false;     // False means the poll failed and runs nothing (R16).
  std::string error;   // Why the poll failed, for the log.
  int64_t serverTimeMs = 0;
  std::vector<ParsedCommand> commands;
  size_t deferred = 0;  // Commands left for a later poll by kMaxCommandsPerPoll.
};

// Parses a GET /devices/{device_id}/commands response. The result isn't ok if
// the status isn't 200, the body is over 8 KiB, isn't one JSON object, or has
// anything but whitespace after it, server_time is missing or invalid, or
// commands isn't an array (R16).
PollResult parsePollResponse(int status, const std::string& body);

}  // namespace ostium
