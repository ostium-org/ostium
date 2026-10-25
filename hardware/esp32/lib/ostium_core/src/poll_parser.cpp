#include "poll_parser.h"

#include <ArduinoJson.h>

#include <algorithm>
#include <cstring>

#include "timestamp.h"

namespace ostium {
namespace {

// Feeds the body to ArduinoJson and records how far it read, so the parser can
// reject anything after the JSON value. deserializeJson stops after the first
// complete value and ignores the rest.
struct CountingReader {
  const std::string& text;
  size_t position = 0;

  int read() {
    return position < text.size() ? static_cast<unsigned char>(text[position++]) : -1;
  }

  size_t readBytes(char* buffer, size_t length) {
    length = std::min(length, text.size() - position);
    std::memcpy(buffer, text.data() + position, length);
    position += length;
    return length;
  }
};

// JSON allows no raw control character (U+0000 to U+001F) inside a string, and
// only tab, CR, and LF between tokens. ArduinoJson accepts them inside a string,
// so this check runs first, and a response that fails it runs nothing (R16).
// ArduinoJson also accepts single-quoted strings, so a string here ends at the
// same quote character that opened it.
bool hasRawControlCharacter(const std::string& body) {
  char quote = '\0';
  bool escaped = false;
  for (const char c : body) {
    const bool control = static_cast<unsigned char>(c) < 0x20;
    if (quote == '\0') {
      if (c == '"' || c == '\'') quote = c;
      else if (control && c != '\t' && c != '\r' && c != '\n') return true;
    } else if (escaped) {
      escaped = false;
    } else if (c == '\\') {
      escaped = true;
    } else if (c == quote) {
      quote = '\0';
    } else if (control) {
      return true;
    }
  }
  return false;
}

// Reads a JSON string. Rejects strings with an embedded NUL character, which
// would otherwise compare as a shorter string.
bool readString(JsonVariantConst value, std::string* out) {
  if (!value.is<JsonString>()) return false;
  const JsonString text = value.as<JsonString>();
  if (std::memchr(text.c_str(), '\0', text.size()) != nullptr) return false;
  out->assign(text.c_str(), text.size());
  return true;
}

bool readTimestamp(JsonVariantConst value, int64_t* out) {
  std::string text;
  return readString(value, &text) && parseTimestamp(text.c_str(), out);
}

// The start of the command as received, for the log.
std::string describe(JsonVariantConst item) {
  char buffer[kMaxLoggedCommandBytes + 1];
  const size_t length = serializeJson(item, buffer, sizeof(buffer) - 1);
  return std::string(buffer, length);
}

ParsedCommand parseCommand(JsonVariantConst item) {
  ParsedCommand command;
  if (!item.is<JsonObjectConst>()) {
    command.problem = CommandProblem::NoCommandId;
    command.detail = "command isn't a JSON object";
    command.received = describe(item);
    return command;
  }
  JsonObjectConst object = item.as<JsonObjectConst>();

  std::string commandId;
  if (!readString(object["command_id"], &commandId) || commandId.empty() ||
      commandId.size() > kMaxCommandIdLength) {
    command.problem = CommandProblem::NoCommandId;
    command.detail = "missing or unusable command_id";
    command.received = describe(item);
    return command;
  }
  command.commandId = commandId;
  command.hasAction = readString(object["action"], &command.actionText) &&
                      command.actionText.size() <= kMaxActionLength;
  if (!command.hasAction) command.actionText.clear();
  command.hasExpiry = readTimestamp(object["expires_at"], &command.expiresAtMs);
  const bool hasDoor = readString(object["door_id"], &command.doorId);
  const bool hasIssued = readTimestamp(object["issued_at"], &command.issuedAtMs);

  if (!command.hasAction) {
    command.problem = CommandProblem::Malformed;
    command.detail = "missing or invalid action";
  } else if (!hasDoor) {
    command.problem = CommandProblem::Malformed;
    command.detail = "missing or invalid door_id";
  } else if (!hasIssued) {
    command.problem = CommandProblem::Malformed;
    command.detail = "missing or invalid issued_at";
  } else if (!command.hasExpiry) {
    command.problem = CommandProblem::Malformed;
    command.detail = "missing or invalid expires_at";
  } else if (command.actionText == "lock") {
    command.action = Action::Lock;
  } else if (command.actionText == "unlock") {
    command.action = Action::Unlock;
  } else {
    command.problem = CommandProblem::InvalidAction;
    command.detail = "unknown action";
  }
  return command;
}

}  // namespace

PollResult parsePollResponse(int status, const std::string& body) {
  PollResult result;
  if (status != 200) {
    result.error = "poll returned status " + std::to_string(status);
    return result;
  }
  if (body.size() > kMaxPollBodyBytes) {
    result.error = "poll response is over 8 KiB";
    return result;
  }
  if (hasRawControlCharacter(body)) {
    result.error = "poll response isn't valid JSON: raw control character";
    return result;
  }

  JsonDocument document;
  CountingReader reader{body};
  const DeserializationError error = deserializeJson(document, reader);
  if (error) {
    result.error = std::string("poll response isn't valid JSON: ") + error.c_str();
    return result;
  }
  if (body.find_first_not_of(" \t\r\n", reader.position) != std::string::npos) {
    result.error = "poll response has content after the JSON";
    return result;
  }
  if (!document.is<JsonObjectConst>()) {
    result.error = "poll response isn't a JSON object";
    return result;
  }
  JsonObjectConst root = document.as<JsonObjectConst>();

  if (!readTimestamp(root["server_time"], &result.serverTimeMs)) {
    result.error = "missing or invalid server_time";
    return result;
  }
  JsonVariantConst commands = root["commands"];
  if (!commands.is<JsonArrayConst>()) {
    result.error = "commands isn't an array";
    return result;
  }
  const JsonArrayConst list = commands.as<JsonArrayConst>();
  const size_t count = list.size();
  result.commands.reserve(std::min(count, kMaxCommandsPerPoll));
  for (JsonVariantConst item : list) {
    if (result.commands.size() == kMaxCommandsPerPoll) break;
    result.commands.push_back(parseCommand(item));
  }
  result.deferred = count - result.commands.size();
  result.ok = true;
  return result;
}

}  // namespace ostium
