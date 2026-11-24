#include "board_adapters.h"

#include <WiFi.h>

#include <utility>

namespace board {
namespace {

// The response body as the WiFiClient delivers it, for ostium::readBody.
// HTTPClient's own writeToStream waits with no deadline while the connection
// stays open, so a stalled server would block the loop.
class ClientSource : public ostium::ByteSource {
 public:
  explicit ClientSource(WiFiClient& client) : client_(client) {}

  size_t available() override {
    const int count = client_.available();
    return count > 0 ? static_cast<size_t>(count) : 0;
  }

  int read() override {
    return client_.read();
  }

  bool connected() override {
    return client_.connected();
  }

  void wait() override {
    delay(1);
  }

 private:
  WiFiClient& client_;
};

int statusFor(ostium::BodyResult result) {
  switch (result) {
    case ostium::BodyResult::TimedOut:
      return HTTPC_ERROR_READ_TIMEOUT;

    case ostium::BodyResult::Closed:
      return HTTPC_ERROR_CONNECTION_LOST;

    case ostium::BodyResult::TooLarge:
      return HTTPC_ERROR_TOO_LESS_RAM;

    default:
      return HTTPC_ERROR_ENCODING;
  }
}

const char* kCollectedHeaders[] = {"Transfer-Encoding"};

}  // namespace

void GpioLockOutput::write(bool unlocked) {
  digitalWrite(pin_, unlocked == activeHigh_ ? HIGH : LOW);

  if (!configured_) {
    pinMode(pin_, OUTPUT);
    configured_ = true;
  }
}

bool EspOneShotTimer::begin() {
  if (handle_ != nullptr) {
    return true;
  }

  esp_timer_create_args_t args = {};
  args.callback = &EspOneShotTimer::dispatch;
  args.arg = this;
  args.dispatch_method = ESP_TIMER_TASK;
  args.name = "ostium-relock";

  return esp_timer_create(&args, &handle_) == ESP_OK;
}

bool EspOneShotTimer::start(uint32_t delayMs,
                            Callback callback,
                            void* context) {
  if (handle_ == nullptr) {
    return false;
  }

  // Stop an existing timer before restarting it.
  esp_timer_stop(handle_);

  callback_ = callback;
  context_ = context;

  return esp_timer_start_once(
             handle_,
             static_cast<uint64_t>(delayMs) * 1000) == ESP_OK;
}

void EspOneShotTimer::cancel() {
  if (handle_ != nullptr) {
    esp_timer_stop(handle_);
  }
}

void EspOneShotTimer::dispatch(void* self) {
  EspOneShotTimer* timer = static_cast<EspOneShotTimer*>(self);

  const Callback callback = timer->callback_;

  if (callback != nullptr) {
    callback(timer->context_);
  }
}

void SerialLog::write(const std::string& message) {
  Serial.printf("[%lu] %s\n",
                static_cast<unsigned long>(millis()),
                message.c_str());
}

BackendClient::BackendClient(const char* apiRoot,
                             const char* deviceId,
                             const char* deviceToken,
                             const char* rootCa,
                             ostium::Log& log)
    : root_(apiRoot),
      deviceId_(deviceId),
      authorization_(String("Bearer ") + deviceToken),
      rootCa_(rootCa),
      log_(log) {}

bool BackendClient::begin() {
  while (root_.endsWith("/")) {
    root_.remove(root_.length() - 1);
  }

  if (deviceId_.isEmpty() || authorization_ == "Bearer ") {
    log_.write(
        "ERROR: OSTIUM_DEVICE_ID and OSTIUM_DEVICE_TOKEN must be set; not polling");
    return false;
  }

  if (root_.startsWith("https://")) {
    if (rootCa_ == nullptr || rootCa_[0] == '\0') {
      log_.write(
          "ERROR: OSTIUM_ROOT_CA is empty for an https:// root; not polling");
      return false;
    }

    secureClient_.setCACert(rootCa_);
    secureClient_.setHandshakeTimeout(kTimeoutMs / 1000);
    client_ = &secureClient_;

    log_.write("Backend transport configured for HTTPS");
  } else if (root_.startsWith("http://")) {
#ifdef OSTIUM_ALLOW_PLAIN_HTTP
    log_.write(
        "WARNING: bench build using plain HTTP to " +
        std::string(root_.c_str()));

    client_ = &plainClient_;
#else
    log_.write(
        "ERROR: an http:// root needs the esp32s3-bench build; not polling");
    return false;
#endif
  } else {
    log_.write(
        "ERROR: OSTIUM_API_ROOT must start with https://; not polling");
    return false;
  }

  commandsUrl_ = root_ + "/devices/" + deviceId_ + "/commands";
  eventsUrl_ = root_ + "/devices/" + deviceId_ + "/events";
  qrEventsUrl_ = root_ + "/devices/" + deviceId_ + "/qr-events";

  http_.setReuse(true);

  log_.write("Backend URLs configured");
  return true;
}

int BackendClient::get(std::string& body) {
  return send(commandsUrl_, nullptr, &body);
}

int BackendClient::post(const std::string& body) {
  return send(eventsUrl_, &body, nullptr);
}

int BackendClient::postQrEvent(const std::string& body) {
  return send(qrEventsUrl_, &body, nullptr);
}

int BackendClient::send(const String& url,
                        const std::string* requestBody,
                        std::string* responseBody) {
  if (client_ == nullptr) {
    log_.write("HTTP ERROR: backend client is not configured");
    return HTTPC_ERROR_CONNECTION_REFUSED;
  }

  log_.write(
      std::string("HTTP: starting ") +
      (requestBody != nullptr ? "POST " : "GET ") +
      std::string(url.c_str()));

  if (!http_.begin(*client_, url)) {
    log_.write("HTTP ERROR: failed to initialize HTTP connection");
    return HTTPC_ERROR_CONNECTION_REFUSED;
  }

  http_.setConnectTimeout(kTimeoutMs);
  http_.setTimeout(kTimeoutMs);

  // Collecting again resets the value, so it never comes from an
  // earlier response.
  http_.collectHeaders(kCollectedHeaders, 1);
  http_.addHeader("Authorization", authorization_);

  int status;

  if (requestBody != nullptr) {
    http_.addHeader("Content-Type", "application/json");

    status = http_.POST(
        reinterpret_cast<uint8_t*>(
            const_cast<char*>(requestBody->data())),
        requestBody->size());
  } else {
    status = http_.GET();
  }

  log_.write(
      "HTTP: response status=" + std::to_string(status));

  if (status < 0) {
    log_.write(
        "HTTP ERROR: request failed; check DNS, routing, TLS certificate, "
        "server availability, and timeout settings");
  } else if (status >= 200 && status < 300) {
    log_.write("HTTP: request succeeded");
  } else {
    log_.write(
        "HTTP WARNING: server returned a non-2xx response");
  }

  // Read every body, including event responses, so no unread bytes
  // are left on the reused connection for the next request.
  // A 204 or 304 has no body.
  if (status > 0 && status != 204 && status != 304) {
    std::string discarded;

    std::string& body =
        responseBody != nullptr ? *responseBody : discarded;

    const bool chunked =
        http_.header("Transfer-Encoding").equalsIgnoreCase("chunked");

    ClientSource source(*client_);

    const ostium::BodyResult read =
        ostium::readBody(
            source,
            clock_,
            http_.getSize(),
            chunked,
            kBodyLimits,
            &body);

    if (read != ostium::BodyResult::Complete) {
      log_.write(
          std::string("HTTP ERROR: response body ") +
          ostium::describe(read) +
          "; closing the connection");

      body.clear();
      client_->stop();

      // A poll fails. An event keeps its status, which already
      // says how the request went.
      if (responseBody != nullptr) {
        status = statusFor(read);
      }
    } else {
      log_.write("HTTP: response body read successfully");
    }
  }

  http_.end();

  return status;
}

bool SerialScanSource::next(ostium::QrScan* scan) {
  std::string token;
  for (int waiting = stream_.available(); waiting > 0; --waiting) {
    const int c = stream_.read();
    if (c < 0) break;
    if (parser_.feed(static_cast<char>(c), &token, log_)) {
      scan->text = std::move(token);
      scan->decodedAtMs = millis();
      return true;
    }
  }
  return false;
}

void WifiLink::begin() {
  WiFi.mode(WIFI_STA);
  WiFi.setAutoReconnect(true);

  log_.write(
      "Wi-Fi: connecting to SSID " + std::string(ssid_));

  WiFi.begin(ssid_, password_);

  nextAttemptMs_ = millis() + retryMs_;
}

void WifiLink::checkInternet() {
  if (!connected()) {
    log_.write(
        "NET FAIL: cannot test internet; Wi-Fi is disconnected");
    return;
  }

  log_.write(
      "NET: Wi-Fi connected; checking network configuration");

  log_.write(
      "NET: SSID=" + std::string(WiFi.SSID().c_str()) +
      " RSSI=" + std::to_string(WiFi.RSSI()) + " dBm");

  log_.write(
      "NET: IP=" +
      std::string(WiFi.localIP().toString().c_str()) +
      " gateway=" +
      std::string(WiFi.gatewayIP().toString().c_str()) +
      " subnet=" +
      std::string(WiFi.subnetMask().toString().c_str()) +
      " DNS=" +
      std::string(WiFi.dnsIP().toString().c_str()));

  // DNS test.
  IPAddress resolved;

  log_.write("NET: testing DNS resolution for example.com");

  if (!WiFi.hostByName("example.com", resolved)) {
    log_.write(
        "NET FAIL: DNS lookup failed; check DNS configuration or internet access");
    return;
  }

  log_.write(
      "NET OK: example.com resolved to " +
      std::string(resolved.toString().c_str()));

  // Outbound TCP test. This does not use the backend's credentials
  // or alter the door's lock state.
  WiFiClient testClient;
  testClient.setTimeout(3000);

  log_.write("NET: testing outbound TCP to example.com:80");

  if (!testClient.connect("example.com", 80)) {
    log_.write(
        "NET FAIL: DNS works, but TCP connection to example.com:80 failed");
    testClient.stop();
    return;
  }

  testClient.stop();

  log_.write(
      "NET OK: DNS and outbound TCP connectivity confirmed");
}

void WifiLink::maintain(uint32_t nowMs) {
  const bool up = connected();

  if (up != wasConnected_) {
    if (up) {
      log_.write(
          "Wi-Fi connected, IP " +
          std::string(WiFi.localIP().toString().c_str()));

      log_.write(
          "Wi-Fi signal strength: " +
          std::to_string(WiFi.RSSI()) + " dBm");

      checkInternet();
    } else {
      log_.write(
          "Wi-Fi disconnected; the door stays locked");
    }

    wasConnected_ = up;
    nextAttemptMs_ = nowMs + retryMs_;
  }

  // The library reconnects after a normal drop. This restart is a
  // fallback for drops it doesn't retry, such as failed authentication.
  // WiFi.begin doesn't block, so loop() keeps running while it connects.
  if (!up && ostium::reached(nowMs, nextAttemptMs_)) {
  const wl_status_t status = WiFi.status();

  log_.write(
      "Wi-Fi reconnect: status=" +
      std::to_string(static_cast<int>(status)));

  switch (status) {
    case WL_NO_SSID_AVAIL:
      log_.write("Wi-Fi ERROR: SSID not found");
      break;

    case WL_CONNECT_FAILED:
      log_.write("Wi-Fi ERROR: connection failed; check password/security");
      break;

    case WL_CONNECTION_LOST:
      log_.write("Wi-Fi ERROR: connection lost");
      break;

    case WL_DISCONNECTED:
      log_.write("Wi-Fi: disconnected; starting connection attempt");
      break;

    case WL_IDLE_STATUS:
      log_.write("Wi-Fi: connection attempt still in progress");
      break;

    default:
      log_.write("Wi-Fi: other connection status");
      break;
  }

  WiFi.disconnect();
  delay(100);
  WiFi.begin(ssid_, password_);

  nextAttemptMs_ = millis() + retryMs_;
}
}

}  // namespace board