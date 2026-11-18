// Door unit firmware (SCRUM-58). Polls the backend for Lock and Unlock
// commands, runs them through ostium_core, and reports one event per command.
// docs/firmware/lock-command-v0.md defines the behavior.

#include <Arduino.h>
#include <esp_system.h>
#include <ostium_core.h>

#include "board_adapters.h"
#include "board_config.h"

#if __has_include("secrets.h")
#include "secrets.h"
#else
#warning "include/secrets.h not found; building with the placeholders in secrets.example.h"
#include "secrets.example.h"
#endif

namespace {

board::SerialLog serialLog;
board::MillisClock deviceClock;
board::GpioLockOutput lockOutput(OSTIUM_LOCK_PIN, OSTIUM_LOCK_ACTIVE_HIGH);
board::EspOneShotTimer relockTimer;
ostium::LockController lockController(
    lockOutput, relockTimer, OSTIUM_UNLOCK_MS);
ostium::EventOutbox outbox(serialLog);
ostium::CommandProcessor processor(
    OSTIUM_DEVICE_ID, lockController, outbox, deviceClock, serialLog);
ostium::PollScheduler scheduler;

board::BackendClient backend(
    OSTIUM_API_ROOT,
    OSTIUM_DEVICE_ID,
    OSTIUM_DEVICE_TOKEN,
    OSTIUM_ROOT_CA,
    serialLog);

board::WifiLink wifi(
    OSTIUM_WIFI_SSID,
    OSTIUM_WIFI_PASSWORD,
    OSTIUM_WIFI_RETRY_MS,
    serialLog);

bool backendReady = false;

constexpr uint32_t kNetworkCheckIntervalMs = 60000;
uint32_t nextNetworkCheckMs = 0;

}  // namespace

void setup() {
  // R1: Write the locked level before anything else starts.
  lockController.begin();

  Serial.begin(OSTIUM_SERIAL_BAUD);

  serialLog.write(
      "door unit " + std::string(OSTIUM_DEVICE_ID) +
      " starting, locked, reset reason " +
      std::to_string(static_cast<int>(esp_reset_reason())));

  if (!relockTimer.begin()) {
    serialLog.write(
        "ERROR: couldn't create the relock timer; every unlock will be refused");
  } else {
    serialLog.write("Relock timer initialized");
  }

  serialLog.write("Initializing backend configuration");
  backendReady = backend.begin();

  if (backendReady) {
    serialLog.write("Backend configuration OK");
  } else {
    serialLog.write(
        "ERROR: backend configuration failed; API polling disabled");
  }

  serialLog.write("Initializing Wi-Fi");
  wifi.begin();

  nextNetworkCheckMs = millis() + kNetworkCheckIntervalMs;
}

void loop() {
  const uint32_t nowMs = deviceClock.nowMs();

  wifi.maintain(nowMs);

  // Check internet reachability periodically while Wi-Fi is connected.
  // This is diagnostic only; it does not change lock state.
  if (wifi.connected() &&
      ostium::reached(nowMs, nextNetworkCheckMs)) {
    serialLog.write("NET: periodic connectivity check starting");
    wifi.checkInternet();
    nextNetworkCheckMs = deviceClock.nowMs() +
                         kNetworkCheckIntervalMs;
  }

  if (backendReady && wifi.connected()) {
    if (scheduler.due(deviceClock.nowMs())) {
      ostium::runPoll(backend, deviceClock, scheduler, processor);
    }

    // At most one event per pass, so a slow post never holds up
    // the next poll for long (R14).
    outbox.service(backend, deviceClock);
  }

  delay(10);
}