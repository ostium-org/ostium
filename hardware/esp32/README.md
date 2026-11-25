# ESP32 Firmware

The door unit runs Arduino-ESP32 on an ESP32-S3 board. It polls the backend for Lock and Unlock commands, drives the lock, and reports one event per command. It also checks signed QR credentials by itself, unlocks for a valid one, and reports each scan (SCRUM-38). The [lock command contract](../../docs/firmware/lock-command-v0.md) and the [QR access contract](../../docs/firmware/qr-access-v0.md) define that behavior, and the [door unit design](../../docs/firmware/design.md) explains the architecture, with the class and use case diagrams.

- `lib/ostium_core/` holds the core logic in plain C++, with no Arduino or hardware dependency.
- `lib/monocypher/` holds an unmodified copy of Monocypher 4.0.2, for the Ed25519 check on QR credentials. Its [README](lib/monocypher/README.md) records the source and hashes.
- `test/test_core/` holds the Unity tests. They run on a laptop with `pio test -d hardware/esp32/native-tests`, from the repository root. For details, see [Testing](../../docs/testing.md#firmware-unit-tests).
- `native-tests/` is the laptop test project, kept separate from the board's `platformio.ini`.
- `tools/capture_backend_responses.py` recorded responses from the old FastAPI backend for the command replay tests. It can't run against the Supabase backend.
- `tools/capture_qr_backend.ts` records tokens and responses from the Deno backend for the QR replay tests, on a microsecond clock and on the backend's default whole-millisecond clock, and generates the startup self-test vector. CI runs it with `--check`.
- `src/` and `include/` hold the board code: `main.cpp`, the adapters in `board_adapters.h` for the lock GPIO, the esp_timer relock, Wi-Fi, the HTTPS client, and the bench build's serial QR input, and `access_panel.h` for the camera, LCD, and sensors, with the camera's QR scan source (SCRUM-68).

## Build and flash the board

The board build is `platformio.ini` in this folder. Its board ID is a generic ESP32-S3 until SCRUM-49 records the exact model.

1. Copy `include/secrets.example.h` to `include/secrets.h`, and fill in the Wi-Fi network, `OSTIUM_API_ROOT`, the device key, the device token, and the backend's root certificate. For QR access, also fill in `OSTIUM_UNIT_ID` and `OSTIUM_QR_PUBLIC_KEY`, as [Set up QR access](#set-up-qr-access) describes. Git ignores `secrets.h`.
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

   The bench build also reads `qr <token>` lines from Serial, and trusts the test-only QR key, so never flash it on a door outside the bench.

The serial monitor shows the reset reason at startup, then each failed poll, each command that runs or is rejected, each QR decision, and each event. The door stays locked until a valid Unlock command or QR credential arrives.

At startup, after the reset reason, expect these lines:

```text
QR self-test passed; Ed25519 check took N ms
QR access on for unit <unit>, key ostium-qr-1
loop stack: N bytes never used
```

If QR access is off, `QR access off: <reason>` takes the place of the `QR access on` line, or of both QR lines when the self-test fails. The bench build then adds `bench build: send 'qr <token>' on Serial to scan a QR payload`. After the first good poll, the monitor shows `server clock set (window N ms)`, and QR access can start.

Without `secrets.h`, the build still compiles, using the placeholders and printing a warning, but the door can't reach a backend, and QR access stays off.

## Set up QR access

The door checks each signed QR credential itself, from settings set at build time, so a valid credential works for up to 72 hours while the backend is unavailable. The [QR access contract](../../docs/firmware/qr-access-v0.md) defines every check, rule Q1 to Q20.

1. Ask the backend owner to set `devices.unit_id` for this door's device row to the unit's `units.id`. Otherwise, the backend answers every QR report with `403`.
2. In `include/secrets.h`, set `OSTIUM_UNIT_ID` to the same unit UUID.
3. Derive the backend's QR public key from its PEM private key, with OpenSSL 1.1.1 or later, and set `OSTIUM_QR_PUBLIC_KEY` to the 43-character result:

   ```sh
   openssl pkey -in qr-signing-key.pem -pubout -outform DER | tail -c 32 | base64 | tr '+/' '-_' | tr -d '='
   ```

4. If the backend changes `QR_KEY_ID`, `QR_ISSUER`, or `QR_AUDIENCE` from their defaults, set `OSTIUM_QR_KEY_ID`, `OSTIUM_QR_ISSUER`, or `OSTIUM_QR_AUDIENCE` in `include/board_config.h` to match.

Leave either secret empty to turn QR access off. The door refuses the test-only key from `backend/supabase/tests/python_compatibility.json` outside the bench build.

### Scan a token on the bench

The camera panel (`src/access_panel.cpp`, SCRUM-68) hands its decodes to `QrAccess` in both builds, and its LCD shows each grant or denial. The team has no lock yet, so the LED on GPIO 14, `OSTIUM_LOCK_PIN`, stands in for the relay, and lights for each 3-second unlock. Only `LockController` sets up and drives that pin. The camera reader doesn't touch it, and step 5 of the contract's [bench procedure](../../docs/firmware/qr-access-v0.md#bench-procedure) checks it with a meter. The `esp32s3-bench` build also reads decoded QR text from the serial monitor, so you can check a token without showing it to the camera. Send a line of the form `qr <token>`, where the token is the `payload` from `POST /api/v1/houses/{houseId}/qr-credentials`. The door handles it like a camera decode. A line can end in CR, LF, or CRLF, so any serial terminal works. Any other line prints the usage line, and the door never logs the token. The contract's [bench procedure](../../docs/firmware/qr-access-v0.md#bench-procedure) has a command that issues a token and prints the line to paste.

For how the camera feeds `QrAccess`, and the steps that remain before a camera goes on a door, see the contract's [camera handoff](../../docs/firmware/qr-access-v0.md#camera-handoff-scrum-68).
