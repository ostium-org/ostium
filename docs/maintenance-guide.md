# Maintenance Guide

This guide is for a developer who takes over a part of Ostium. Each module has its own section.

## Door unit firmware

The door unit runs on an ESP32-S3 board with PlatformIO and the Arduino framework. It polls the backend's `device-api` for Lock and Unlock commands, checks signed QR codes on the door, and reports each result. Two contracts define its behavior: [Lock command](firmware/lock-command-v0.md) (rules R1 to R16) and [QR access](firmware/qr-access-v0.md) (rules Q1 to Q20). To deploy, configure, or roll back a door, use the [operations guide](operations-guide.md#door-unit-firmware).

### Where the code is

- `hardware/esp32/lib/ostium_core/`: the core logic in plain C++17, with no Arduino code: lock control, command checks, event reports, the QR check, and the server clock. Everything that decides whether the door opens is here.
- `hardware/esp32/src/` and `include/`: the board code. `main.cpp` wires the core to the hardware. `board_adapters` holds the lock GPIO, the relock timer, Wi-Fi, and the HTTPS client. `access_panel` drives the camera, LCD, and sensors.
- `hardware/esp32/test/test_core/`: the native unit tests. `tests.h` lists every test with the rule it covers.
- `hardware/esp32/lib/monocypher/` and `lib/ESP32QRCodeReader/`: vendored libraries for Ed25519 and for QR decoding.
- `hardware/esp32/tools/`: `capture_qr_backend.ts` generates the QR test fixtures, and `package_firmware.sh` packages releases.
- `docs/firmware/`: the two contracts, the design rationale (`design.md`), and the diagrams.

### Build and test

From the repository root, with PlatformIO 6.2.0:

```sh
pio test -d hardware/esp32/native-tests
pio run -d hardware/esp32 -e esp32s3
pio run -d hardware/esp32 -e esp32s3-bench
```

The tests run on a laptop in a few seconds and need no board. They build with `-Wall -Wextra -Werror`, so any warning fails them. CI runs the same commands on every pull request, and a pull request into `main` merges only when every build and test passes.

### Change a rule

1. Update the rule in its contract, `lock-command-v0.md` or `qr-access-v0.md`.
2. Change the code in `ostium_core`.
3. Add or change a test that shows the rule. Register it in `tests.h` with the rule number in its comment, and in `test_main.cpp`.
4. Break the code on purpose and check that the test fails, then put the code back.
5. If the number of tests changed, update the counts in `docs/firmware/design.md`, `docs/firmware/qr-access-v0.md`, `docs/testing.md`, and `docs/ci.md`.

Keep the core free of Arduino code, so the tests keep covering it. New hardware goes behind an interface in `interfaces.h`, with a fake in the tests.

### When the backend changes

The QR replay tests use tokens and responses recorded from the real Deno backend. CI fails when they no longer match. After a change to the QR or device routes, record them again, then run the tests:

```sh
deno run --check --allow-read --allow-write=hardware/esp32/test/test_core,hardware/esp32/lib/ostium_core/src hardware/esp32/tools/capture_qr_backend.ts
```

To check without writing, add `--check` at the end. The nine command replay tests use responses from the removed FastAPI backend and can't be recorded again.

### Dependencies

- **ArduinoJson** is pinned to exactly 7.4.3 in `hardware/esp32/platformio.ini`, `hardware/esp32/native-tests/platformio.ini`, and `lib/ostium_core/library.json`, because the replay tests depend on how it parses decimal numbers. To upgrade, change all three pins and `ARDUINOJSON_VERSION` in `capture_qr_backend.ts`, record the fixtures again, and run the tests.
- **Monocypher** 4.0.2 is vendored unmodified, and its `README.md` lists the SHA-256 of each file. To upgrade, copy the files from the official release tarball, check its hash, update the README, and run the tests, which include the RFC 8032 vectors. Release 4.0.3 (June 2026) fixes a timing leak in its EdDSA/Ed25519 signature code. The door only verifies signatures, so check whether the fix applies before you upgrade.
- **PlatformIO** is pinned to 6.2.0 in CI, and the board platform to `espressif32 @ 6.12.0`.

### Diagrams

The class and use case diagrams are PlantUML sources in `docs/firmware/diagrams/`. After changing a class or a use case, update the source and render it again. PlantUML uses only the last `-t` flag, so run it once per format:

```sh
java -DPLANTUML_LIMIT_SIZE=16384 -jar plantuml.jar -tsvg docs/firmware/diagrams/class-diagram.puml
java -DPLANTUML_LIMIT_SIZE=16384 -jar plantuml.jar -tpng docs/firmware/diagrams/class-diagram.puml
```

### Open work

- The camera path ([PR #33](https://github.com/ostium-org/ostium/pull/33)) is waiting for a board test.
- The Ed25519 check time and the free loop stack haven't been measured on a board. The bench procedure in `qr-access-v0.md` covers both.
- There's no revocation feed, no settings sync from the backend, no over-the-air update, and no real lock relay yet. An LED on GPIO 14 stands in for the relay.
