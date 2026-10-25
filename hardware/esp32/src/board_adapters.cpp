#include "board_adapters.h"

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
  int read() override { return client_.read(); }
  bool connected() override { return client_.connected(); }
  void wait() override { delay(1); }

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
  if (handle_ != nullptr) return true;
  esp_timer_create_args_t args = {};
  args.callback = &EspOneShotTimer::dispatch;
  args.arg = this;
  args.dispatch_method = ESP_TIMER_TASK;
  args.name = "ostium-relock";
  return esp_timer_create(&args, &handle_) == ESP_OK;
}

bool EspOneShotTimer::start(uint32_t delayMs, Callback callback, void* context) {
  if (handle_ == nullptr) return false;
  // esp_timer_start_once fails on a running timer, so stop it first. Stopping
  // a timer that isn't running returns ESP_ERR_INVALID_STATE, which is fine.
  esp_timer_stop(handle_);
  callback_ = callback;
  context_ = context;
  return esp_timer_start_once(handle_, static_cast<uint64_t>(delayMs) * 1000) == ESP_OK;
}

void EspOneShotTimer::cancel() {
  if (handle_ != nullptr) esp_timer_stop(handle_);
}

void EspOneShotTimer::dispatch(void* self) {
  EspOneShotTimer* timer = static_cast<EspOneShotTimer*>(self);
  const Callback callback = timer->callback_;
  if (callback != nullptr) callback(timer->context_);
}

void SerialLog::write(const std::string& message) {
  Serial.printf("[%lu] %s\n", static_cast<unsigned long>(millis()), message.c_str());
}

BackendClient::BackendClient(const char* apiRoot, const char* deviceId, const char* deviceToken,
                             const char* rootCa, ostium::Log& log)
    : root_(apiRoot),
      deviceId_(deviceId),
      authorization_(String("Bearer ") + deviceToken),
      rootCa_(rootCa),
      log_(log) {}

bool BackendClient::begin() {
  while (root_.endsWith("/")) root_.remove(root_.length() - 1);
  if (deviceId_.isEmpty() || authorization_ == "Bearer ") {
    log_.write("OSTIUM_DEVICE_ID and OSTIUM_DEVICE_TOKEN must be set; not polling");
    return false;
  }
  if (root_.startsWith("https://")) {
    if (rootCa_ == nullptr || rootCa_[0] == '\0') {
      log_.write("OSTIUM_ROOT_CA is empty for an https:// root; not polling");
      return false;
    }
    secureClient_.setCACert(rootCa_);
    secureClient_.setHandshakeTimeout(kTimeoutMs / 1000);
    client_ = &secureClient_;
  } else if (root_.startsWith("http://")) {
#ifdef OSTIUM_ALLOW_PLAIN_HTTP
    log_.write("bench build: using plain HTTP to " + std::string(root_.c_str()));
    client_ = &plainClient_;
#else
    log_.write("an http:// root needs the esp32s3-bench build; not polling");
    return false;
#endif
  } else {
    log_.write("OSTIUM_API_ROOT must start with https://; not polling");
    return false;
  }
  commandsUrl_ = root_ + "/devices/" + deviceId_ + "/commands";
  eventsUrl_ = root_ + "/devices/" + deviceId_ + "/events";
  http_.setReuse(true);
  return true;
}

int BackendClient::get(std::string& body) { return send(commandsUrl_, nullptr, &body); }

int BackendClient::post(const std::string& body) { return send(eventsUrl_, &body, nullptr); }

int BackendClient::send(const String& url, const std::string* requestBody,
                        std::string* responseBody) {
  if (client_ == nullptr) return HTTPC_ERROR_CONNECTION_REFUSED;
  if (!http_.begin(*client_, url)) return HTTPC_ERROR_CONNECTION_REFUSED;
  http_.setConnectTimeout(kTimeoutMs);
  http_.setTimeout(kTimeoutMs);
  // Collecting again resets the value, so it never comes from an earlier response.
  http_.collectHeaders(kCollectedHeaders, 1);
  http_.addHeader("Authorization", authorization_);

  int status;
  if (requestBody != nullptr) {
    http_.addHeader("Content-Type", "application/json");
    status = http_.POST(reinterpret_cast<uint8_t*>(const_cast<char*>(requestBody->data())),
                        requestBody->size());
  } else {
    status = http_.GET();
  }

  // Read every body, including event responses, so no unread bytes are left
  // on the reused connection for the next request. A 204 or 304 has no body.
  if (status > 0 && status != 204 && status != 304) {
    std::string discarded;
    std::string& body = responseBody != nullptr ? *responseBody : discarded;
    const bool chunked = http_.header("Transfer-Encoding").equalsIgnoreCase("chunked");
    ClientSource source(*client_);
    const ostium::BodyResult read =
        ostium::readBody(source, clock_, http_.getSize(), chunked, kBodyLimits, &body);
    if (read != ostium::BodyResult::Complete) {
      // A body over 8 KiB would fail the parser anyway (R16). Closing the
      // connection drops any unread bytes.
      log_.write(std::string("response body ") + ostium::describe(read) +
                 "; closing the connection");
      body.clear();
      client_->stop();
      // A poll fails. An event keeps its status, which already says how it went.
      if (responseBody != nullptr) status = statusFor(read);
    }
  }
  http_.end();
  return status;
}

void WifiLink::begin() {
  WiFi.mode(WIFI_STA);
  WiFi.setAutoReconnect(true);
  WiFi.begin(ssid_, password_);
  nextAttemptMs_ = millis() + retryMs_;
}

void WifiLink::maintain(uint32_t nowMs) {
  const bool up = connected();
  if (up != wasConnected_) {
    log_.write(up ? "Wi-Fi connected, IP " + std::string(WiFi.localIP().toString().c_str())
                  : "Wi-Fi disconnected; the door stays locked");
    wasConnected_ = up;
    nextAttemptMs_ = nowMs + retryMs_;
  }
  // The library reconnects after a normal drop. This restart is a fallback for
  // drops it doesn't retry, such as a failed authentication. WiFi.begin doesn't
  // block, so loop() keeps running while it connects.
  if (!up && ostium::reached(nowMs, nextAttemptMs_)) {
    WiFi.disconnect();
    WiFi.begin(ssid_, password_);
    nextAttemptMs_ = nowMs + retryMs_;
  }
}

}  // namespace board
