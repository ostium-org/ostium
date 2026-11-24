#pragma once

// Board implementations of the ostium_core interfaces. They're the only code
// that touches the GPIO, esp_timer, Wi-Fi, and HTTPS.

#include <Arduino.h>
#include <HTTPClient.h>
#include <WiFi.h>
#include <WiFiClientSecure.h>
#include <esp_timer.h>
#include <ostium_core.h>

#include <atomic>
#include <string>

namespace board {

// Drives the lock GPIO. The first write sets the level before the pin becomes
// an output, so the pin never drives the unlocked level by accident (R1).
class GpioLockOutput : public ostium::LockOutput {
 public:
  GpioLockOutput(uint8_t pin, bool activeHigh) : pin_(pin), activeHigh_(activeHigh) {}

  void write(bool unlocked) override;

 private:
  const uint8_t pin_;
  const bool activeHigh_;
  bool configured_ = false;
};

// A one-shot esp_timer. Its callback runs in the esp_timer task, so the relock
// fires on time while loop() is blocked in a request (R12).
class EspOneShotTimer : public ostium::OneShotTimer {
 public:
  // Creates the esp_timer. Until it succeeds, start() returns false, so an
  // unlock is refused and the door stays locked.
  bool begin();

  bool start(uint32_t delayMs, Callback callback, void* context) override;
  void cancel() override;

 private:
  static void dispatch(void* self);

  esp_timer_handle_t handle_ = nullptr;
  std::atomic<Callback> callback_{nullptr};
  std::atomic<void*> context_{nullptr};
};

class MillisClock : public ostium::Clock {
 public:
  uint32_t nowMs() const override { return millis(); }
};

class SerialLog : public ostium::Log {
 public:
  void write(const std::string& message) override;
};

// Talks to the backend's device routes over HTTPS, or over plain HTTP in the
// bench build. Each request carries the device token. The firmware never calls
// setInsecure() and doesn't follow redirects. As an EventSender, it posts
// command events. qrEvents() posts QR reports.
class BackendClient : public ostium::PollFetcher, public ostium::EventSender {
 public:
  static constexpr uint32_t kTimeoutMs = 3000;
  // A response body must keep arriving: no gap over kTimeoutMs, and no more
  // than twice that for the whole body.
  static constexpr ostium::BodyLimits kBodyLimits{ostium::kMaxPollBodyBytes, kTimeoutMs,
                                                  2 * kTimeoutMs};

  BackendClient(const char* apiRoot, const char* deviceId, const char* deviceToken,
                const char* rootCa, ostium::Log& log);

  // Checks the configuration. Returns false, and the door never polls, if the
  // root isn't https:// (or http:// in the bench build), the device ID or
  // token is empty, or an https:// root has no root certificate.
  bool begin();

  int get(std::string& body) override;
  int post(const std::string& body) override;

  // Posts one QR report to .../qr-events, the same way post() sends an event.
  int postQrEvent(const std::string& body);
  // The sender for the QR outbox (Q19).
  ostium::EventSender& qrEvents() { return qrEvents_; }

 private:
  class QrEventSender : public ostium::EventSender {
   public:
    explicit QrEventSender(BackendClient& client) : client_(client) {}
    int post(const std::string& body) override { return client_.postQrEvent(body); }

   private:
    BackendClient& client_;
  };

  int send(const String& url, const std::string* requestBody, std::string* responseBody);

  String root_;
  const String deviceId_;
  const String authorization_;
  const char* rootCa_;
  ostium::Log& log_;
  MillisClock clock_;
  WiFiClientSecure secureClient_;
  WiFiClient plainClient_;
  WiFiClient* client_ = nullptr;
  HTTPClient http_;
  String commandsUrl_;
  String eventsUrl_;
  String qrEventsUrl_;
  QrEventSender qrEvents_{*this};
};

// Reads "qr <token>" lines from a stream, such as Serial, as if the camera had
// decoded each token (Q1). A line can end in CR, LF, or CRLF, so any serial
// terminal works. Only the bench build creates one. next() reads only
// the bytes already waiting, so it never blocks, and it stops after the first
// token, so later lines wait in the stream for the next call.
class SerialScanSource : public ostium::ScanSource {
 public:
  SerialScanSource(Stream& stream, ostium::Log& log) : stream_(stream), log_(log) {}

  bool next(ostium::QrScan* scan) override;

 private:
  Stream& stream_;
  ostium::Log& log_;
  ostium::SerialScanParser parser_;
};

// Joins the Wi-Fi network and reconnects after it drops, without blocking
// loop(). The door stays locked while it's offline.
class WifiLink {
 public:
  WifiLink(const char* ssid, const char* password, uint32_t retryMs, ostium::Log& log)
      : ssid_(ssid), password_(password), retryMs_(retryMs), log_(log) {}

  void begin();
  void maintain(uint32_t nowMs);
  bool connected() const { return WiFi.status() == WL_CONNECTED; }
  void checkInternet();

 private:
  const char* ssid_;
  const char* password_;
  const uint32_t retryMs_;
  ostium::Log& log_;
  bool wasConnected_ = false;
  uint32_t nextAttemptMs_ = 0;
};

}  // namespace board
