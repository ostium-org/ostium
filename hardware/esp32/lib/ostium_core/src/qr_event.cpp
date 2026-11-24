#include "qr_event.h"

#include <ArduinoJson.h>

#include "timestamp.h"

namespace ostium {

std::string serializeQrReport(const QrReport& report) {
  JsonDocument document;
  document["event_id"] = report.eventId();
  document["credential_id"] = report.credentialId;
  if (report.granted) {
    document["result"] = "granted";
    document["reason"] = nullptr;
  } else {
    document["result"] = "denied";
    document["reason"] = report.reason;
  }
  document["occurred_at"] = formatTimestamp(report.occurredAtMs);

  std::string body;
  serializeJson(document, body);
  return body;
}

}  // namespace ostium
