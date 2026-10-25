#include "event.h"

#include <ArduinoJson.h>

#include "timestamp.h"

namespace ostium {

std::string serializeEvent(const Event& event) {
  JsonDocument document;
  document["event_id"] = event.eventId();
  document["command_id"] = event.commandId;
  if (event.hasAction) {
    document["action"] = event.action;
  } else {
    document["action"] = nullptr;
  }
  if (event.result == Result::Done) {
    document["result"] = "done";
    document["reason"] = nullptr;
  } else {
    document["result"] = "rejected";
    document["reason"] = event.reason;
  }
  document["occurred_at"] = formatTimestamp(event.occurredAtMs);

  std::string body;
  serializeJson(document, body);
  return body;
}

}  // namespace ostium
