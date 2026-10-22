# ESP32 Firmware

The door unit runs Arduino-ESP32 on an ESP32-S3 board. For the Assignment 1 MVP, it polls the backend for Lock and Unlock commands, drives the lock, and reports one event per command. The [lock command contract](../../docs/firmware/lock-command-v0.md) defines that behavior.

- `lib/ostium_core/` holds the core logic in plain C++, with no Arduino or hardware dependency.
- `test/test_core/` holds its Unity tests. They run on a laptop with `pio test -d hardware/esp32/native-tests`, from the repository root. For details, see [Testing](../../docs/testing.md#firmware-unit-tests).
- `native-tests/` is the laptop test project, kept separate from the board's `platformio.ini`.
- `tools/capture_backend_responses.py` records responses from the real backend for the replay tests.

When the board code is added, keep application code in `src/` and headers in `include/`, with the device secrets in `include/secrets.h`, which Git ignores.

In the final product, the door also scans signed QR credentials and checks them locally from cached configuration, so a valid credential works while the backend is unavailable (SCRUM-38). That design isn't part of the MVP.
