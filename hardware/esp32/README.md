# ESP32 Firmware

The door unit runs Arduino-ESP32 on an ESP32-S3 board. For the Assignment 1 MVP, it polls the backend for Lock and Unlock commands, drives the lock, and reports one event per command. The [lock command contract](../../docs/firmware/lock-command-v0.md) defines that behavior.

- `lib/ostium_core/` holds the core logic in plain C++, with no Arduino or hardware dependency.
- `test/test_core/` holds its Unity tests. They run on a laptop with `pio test -d hardware/esp32/native-tests`, from the repository root. For details, see [Testing](../../docs/testing.md#firmware-unit-tests).
- `native-tests/` is the laptop test project, kept separate from the board's `platformio.ini`.
- `tools/capture_backend_responses.py` records responses from the real backend for the replay tests.
- `src/` and `include/` hold the board code: `main.cpp`, and the adapters in `board_adapters.h` for the lock GPIO, the esp_timer relock, Wi-Fi, and the HTTPS client.

## Build and flash the board

The board build is `platformio.ini` in this folder. Its board ID is a generic ESP32-S3 until SCRUM-49 records the exact model.

1. Copy `include/secrets.example.h` to `include/secrets.h`, and fill in the Wi-Fi network, `OSTIUM_API_ROOT`, the device key, the device token, and the backend's root certificate. Git ignores `secrets.h`.
2. In `include/board_config.h`, set `OSTIUM_LOCK_PIN` to the GPIO that drives the lock, from the board's pinout. If the relay turns on when its input is low, as most opto-isolated relay modules do, also set `OSTIUM_LOCK_ACTIVE_HIGH` to `0`. Wire a pull resistor toward the locked level: a pull-down for an active-high relay, or a pull-up for an active-low one. You can also set both values as build flags.
3. Before you connect the lock, flash the board and confirm with a meter or the relay's LED that the relay stays off after boot and through a reset.
4. Connect the board's UART port, labeled UART on an ESP32-S3-DevKitC-1. If the board has only a native USB port, add `-DARDUINO_USB_CDC_ON_BOOT=1` to `build_flags`, or the serial monitor shows nothing.
5. To build and flash, run one of these from the repository root.

   For a door that uses an `https://` root:

   ```sh
   pio run -d hardware/esp32 -t upload -t monitor
   ```

   For a backend on a laptop over plain HTTP, bench only:

   ```sh
   pio run -d hardware/esp32 -e esp32s3-bench -t upload -t monitor
   ```

The serial monitor shows the reset reason at startup, then each failed poll, each command that runs or is rejected, and each event. The door stays locked until a valid Unlock command arrives.

Without `secrets.h`, the build still compiles, using the placeholders and printing a warning, but the door can't reach a backend.

In the final product, the door also scans signed QR credentials and checks them locally from cached configuration, so a valid credential works while the backend is unavailable (SCRUM-38). That design isn't part of the MVP.
