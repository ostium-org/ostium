// Door unit firmware (SCRUM-58). Polls the backend for Lock and Unlock
// commands, runs them through ostium_core, and reports one event per command.
// It also checks decoded QR credentials on the door, unlocks for a valid one,
// and reports the result (SCRUM-38). docs/firmware/lock-command-v0.md and
// docs/firmware/qr-access-v0.md define the behavior.

#include <Arduino.h>
#include <esp_system.h>
#include <ostium_core.h>
#include <WiFi.h>
#include <string>

#include "board_adapters.h"
#include "board_config.h"
#include "access_panel.h"

#if __has_include("secrets.h")
#include "secrets.h"
#else
#warning "include/secrets.h not found; building with the placeholders in secrets.example.h"
#include "secrets.example.h"
#endif

// A secrets.h from before SCRUM-38 has no QR settings, so QR access stays off.
#ifndef OSTIUM_UNIT_ID
#define OSTIUM_UNIT_ID ""
#endif
#ifndef OSTIUM_QR_PUBLIC_KEY
#define OSTIUM_QR_PUBLIC_KEY ""
#endif

namespace
{

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

  // QR reports have their own queue, so they never evict a command event.
  ostium::EventOutbox qrOutbox(serialLog, ostium::ReportKind::Qr);
  ostium::ServerClock serverClock(serialLog);
  ostium::QrAccess qrAccess(
      lockController, qrOutbox, serverClock, deviceClock, serialLog);

  board::BackendClient backend(
      OSTIUM_API_ROOT,
      OSTIUM_DEVICE_ID,
      OSTIUM_DEVICE_TOKEN,
      OSTIUM_ROOT_CA,
      serialLog);

  // One report post per loop pass across both queues, QR reports first, taking
  // turns with command events while QR posts keep failing (R14, Q19).
  ostium::ReportScheduler reports(
      qrOutbox, backend.qrEvents(), outbox, backend);

  board::WifiLink wifi(
      OSTIUM_WIFI_SSID,
      OSTIUM_WIFI_PASSWORD,
      OSTIUM_WIFI_RETRY_MS,
      serialLog);

  // Access panel: the camera, LCD, and sensors (SCRUM-68).
  board::AccessPanel accessPanel;
  board::CameraScanSource cameraScans;

  bool backendReady = false;

  constexpr uint32_t kNetworkCheckIntervalMs = 60000;
  uint32_t nextNetworkCheckMs = 0;

#ifdef OSTIUM_BENCH_TOOLS
  board::SerialScanSource serialScans(Serial, serialLog);
#endif

  ostium::QrSettings qrSettings()
  {
    ostium::QrSettings settings;
    settings.unitId = OSTIUM_UNIT_ID;
    settings.publicKey = OSTIUM_QR_PUBLIC_KEY;
    settings.keyId = OSTIUM_QR_KEY_ID;
    settings.issuer = OSTIUM_QR_ISSUER;
    settings.audience = OSTIUM_QR_AUDIENCE;
#ifdef OSTIUM_BENCH_TOOLS
    settings.allowTestKey = true;
#endif
    return settings;
  }

  // Hands waiting decoded QR codes to QrAccess, at most 4 from each source per
  // call (Q1), and shows each decision on the panel. The bench build also
  // takes "qr <token>" lines from Serial.
  void pumpScans()
  {
    ostium::drainScans(cameraScans, qrAccess, 4, &accessPanel);
#ifdef OSTIUM_BENCH_TOOLS
    ostium::drainScans(serialScans, qrAccess, 4, &accessPanel);
#endif
  }

} // namespace

void setup()
{
  // R1: Write the locked level before anything else starts.
  lockController.begin();

#ifdef OSTIUM_BENCH_TOOLS
  // Must come before Serial.begin.
  Serial.setRxBufferSize(OSTIUM_SERIAL_RX_BUFFER);
#endif
  Serial.begin(OSTIUM_SERIAL_BAUD);

  serialLog.write(
      "door unit " + std::string(OSTIUM_DEVICE_ID) +
      " starting, locked, reset reason " +
      std::to_string(static_cast<int>(esp_reset_reason())));

  if (!relockTimer.begin())
  {
    serialLog.write(
        "ERROR: couldn't create the relock timer; every unlock will be refused");
  }
  else
  {
    serialLog.write("Relock timer initialized");
  }

  // Runs the Ed25519 self-test on this stack, checks the QR settings, and logs
  // "QR access on" or "QR access off". The stack line after it covers the
  // verifier's stack use.
  qrAccess.begin(qrSettings());
  serialLog.write(
      "loop stack: " + std::to_string(uxTaskGetStackHighWaterMark(nullptr)) +
      " bytes never used");
#ifdef OSTIUM_BENCH_TOOLS
  serialLog.write(
      "bench build: send 'qr <token>' on Serial to scan a QR payload");
#endif

  serialLog.write("Initializing backend configuration");
  backendReady = backend.begin();

  if (backendReady)
  {
    serialLog.write("Backend configuration OK");
  }
  else
  {
    serialLog.write(
        "ERROR: backend configuration failed; API polling disabled");
  }

  serialLog.write("Initializing Wi-Fi");
  wifi.begin();

  // Access Panel Initialization
  if (!accessPanel.begin())
  {
    serialLog.write(
        "ERROR: access panel initialization failed");
  }
  else
  {
    serialLog.write("Access panel initialized");
  }

  // R1: the panel's camera and sensor setup must leave OSTIUM_LOCK_PIN alone.
  // Write the locked level again, which also makes the pin an output again, so
  // a library that reconfigured it can't leave the lock undriven until the
  // first command or QR grant.
  lockController.lock();

  nextNetworkCheckMs = millis() + kNetworkCheckIntervalMs;

  serialLog.write("Ostium Core startup complete");
}

void loop()
{
  const uint32_t nowMs = deviceClock.nowMs();

  serverClock.tick(nowMs);
  wifi.maintain(nowMs);

  // Access panel update
  accessPanel.update(nowMs);

  // Signed QR codes, from the camera (and Serial in the bench build). They
  // work offline while the server clock is known. QrAccess logs each decision.
  pumpScans();

  // Check internet reachability periodically while Wi-Fi is connected.
  // This is diagnostic only; it does not change lock state.
  if (wifi.connected() &&
      ostium::reached(nowMs, nextNetworkCheckMs))
  {
    serialLog.write("NET: periodic connectivity check starting");
    wifi.checkInternet();
    nextNetworkCheckMs = deviceClock.nowMs() +
                         kNetworkCheckIntervalMs;
  }

  if (backendReady && wifi.connected())
  {
    if (scheduler.due(deviceClock.nowMs()))
    {
      ostium::runPoll(backend, deviceClock, scheduler, processor, &serverClock);
      // Decide the scans that waited during the poll before the report post.
      pumpScans();
    }

    // At most one report post per pass, so a slow post never holds up the
    // next poll for long, and neither queue can starve the other (R14, Q19).
    reports.serviceOne(deviceClock);
  }

  delay(10);
}
