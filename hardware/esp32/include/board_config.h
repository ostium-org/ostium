#pragma once

// Board settings that aren't secret. To change one, add a build flag in
// platformio.ini, for example -DOSTIUM_LOCK_PIN=21.

// The GPIO that drives the relay or lock. This is a placeholder until SCRUM-49
// records the board's pinout. Use a pin that isn't a strapping pin (0, 3, 45,
// or 46) and isn't used by the camera or PSRAM, with a pull resistor that
// keeps the relay off while the pin floats during boot (R1).
#ifndef OSTIUM_LOCK_PIN
#define OSTIUM_LOCK_PIN 14
#endif

// 1 if driving the pin high turns the relay on (unlocks), 0 if low does.
#ifndef OSTIUM_LOCK_ACTIVE_HIGH
#define OSTIUM_LOCK_ACTIVE_HIGH 1
#endif

// How long an unlock lasts. LockController clamps it to 5,000 ms (R10).
#ifndef OSTIUM_UNLOCK_MS
#define OSTIUM_UNLOCK_MS 3000
#endif

// How long a Wi-Fi join may take before the firmware restarts it. It's a
// fallback: the Wi-Fi library reconnects by itself after a normal drop.
#ifndef OSTIUM_WIFI_RETRY_MS
#define OSTIUM_WIFI_RETRY_MS 15000
#endif

#ifndef OSTIUM_SERIAL_BAUD
#define OSTIUM_SERIAL_BAUD 115200
#endif
