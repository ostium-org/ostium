# Operations guide

This guide explains how to deploy, configure, check, and roll back each part of Ostium. It covers Assignment 2, Part C. Keep passwords, tokens, and keys out of the repository: each section says where its secrets live.

## Door unit firmware

The door unit is an ESP32-S3 board that runs the firmware in `hardware/esp32`. It polls the backend's `device-api` for Lock and Unlock commands, checks signed QR codes itself, and reports each result. There's no lock relay yet, so an LED on the lock output, GPIO 14, stands in for the lock: it lights for each 3-second unlock.

The door has no over-the-air updates. Every deploy and rollback means flashing the board over USB.

### How a release reaches the door

1. A pull request into `main` passes CI. The `auto-merge` job merges it and starts a CI run on `main`. If the pull request has to wait for a review, the reviewer merges it after approving, and that merge starts the run.
2. That run builds and tests the merged code. Then `firmware-release` publishes a GitHub release named `firmware-r<N>`, tagged at the tested commit. [Continuous integration](ci.md#firmware-releases) describes the release files.
3. You flash that tag to the door with the door's own settings. CI can't do this step: the board sits on a bench, and its settings are secrets that never enter the public repository.

### What you need

- The door board and a USB data cable. Use the board's USB-to-serial port, not its native USB port: the access panel uses GPIO 19 and 20, which are the ESP32-S3's native USB pins.
- Python 3 and PlatformIO 6.2.0: `pip install "platformio==6.2.0"`. The first build downloads the ESP32 toolchain.
- A clone of the repository, with the release tags: `git fetch --tags`.
- The door's settings, which the backend owner provides. See [Configure the door](#configure-the-door).

### Configure the door

The firmware reads its settings at build time from `hardware/esp32/include/secrets.h`. Git ignores this file, so it never leaves your computer.

1. Copy the template:

   ```sh
   cp hardware/esp32/include/secrets.example.h hardware/esp32/include/secrets.h
   ```

2. Fill in every value:

   | Setting | What it is | Where it comes from |
   |---|---|---|
   | `OSTIUM_WIFI_SSID`, `OSTIUM_WIFI_PASSWORD` | The Wi-Fi network the door joins. 2.4 GHz only. | The site |
   | `OSTIUM_API_ROOT` | The device API root, with no trailing slash: `https://<project-ref>.supabase.co/functions/v1/device-api/api/v1` | The backend owner |
   | `OSTIUM_DEVICE_ID` | The door's `devices.device_key` | The backend owner |
   | `OSTIUM_DEVICE_TOKEN` | The door's own token. The backend stores only its SHA-256 digest. | The backend owner, once, through a private channel |
   | `OSTIUM_ROOT_CA` | The PEM root certificate that the backend host's certificate chains to. On November 24, 2026, the Supabase host chained to GTS Root R4. | [Google Trust Services](https://pki.goog/repository/) |
   | `OSTIUM_UNIT_ID` | The unit's `units.id` UUID. The backend owner must set `devices.unit_id` for this door to the same UUID. | The backend owner |
   | `OSTIUM_QR_PUBLIC_KEY` | The backend's 43-character QR signing public key. It isn't secret. | The backend owner, with the OpenSSL command in [QR access](firmware/qr-access-v0.md#configuration) |

   Leave `OSTIUM_UNIT_ID` or `OSTIUM_QR_PUBLIC_KEY` empty to run the door with QR access off.

3. Check the board settings in `hardware/esp32/include/board_config.h`. The defaults are GPIO 14 for the lock, active high, and a 3,000 ms unlock. To change one for a single board, add a flag to `build_flags` in `platformio.ini`, for example `-DOSTIUM_LOCK_PIN=14`, instead of editing the file.

Never commit `secrets.h`, paste it into a chat, or put it in a screenshot. If a device token leaks, ask the backend owner to issue a new one and to disable the old one.

### Deploy a release

1. Pick the release on the repository's **Releases** page, for example `firmware-r57`, and check out its tag:

   ```sh
   git fetch --tags
   git checkout firmware-r57
   ```

2. Make sure `hardware/esp32/include/secrets.h` holds this door's settings.
3. Connect the board, then build, flash, and open the serial monitor:

   ```sh
   pio run -d hardware/esp32 -e esp32s3 -t upload -t monitor
   ```

   Use the `esp32s3` build for a door. The `esp32s3-bench` build also accepts plain HTTP and can trust the test-only QR key when `secrets.h` sets it, so never flash it on a door.

4. Run the [health check](#check-that-the-door-is-healthy).
5. Return to your branch: `git checkout main`.

If you have only a release zip, and no build tools, you can flash its images directly. They hold placeholder settings, so the door boots locked but can't reach the backend. Use them only to check a board:

```sh
shasum -a 256 -c SHA256SUMS
unzip ostium-door-esp32s3-r57.zip
cd ostium-door-esp32s3-r57
pio pkg exec -p tool-esptoolpy -- esptool.py --chip esp32s3 --baud 460800 write_flash \
  0x0 bootloader.bin 0x8000 partitions.bin 0xe000 boot_app0.bin 0x10000 firmware.bin
```

### Check that the door is healthy

Open the serial monitor at 115,200 baud: `pio device monitor -d hardware/esp32 -e esp32s3`. Each line starts with the milliseconds since boot. A healthy door logs these lines, in this order:

| Line | What it shows | If it's missing or different |
|---|---|---|
| `door unit <id> starting, locked, reset reason <n>` | The lock output was set to locked before anything else. | An unexpected reset reason after a hang points to a crash. Decode the backtrace with the release's `firmware.elf`. |
| `Relock timer initialized` | Unlocks can be timed. | `couldn't create the relock timer` means every unlock is refused. Reflash, and report it. |
| `QR self-test passed; Ed25519 check took <n> ms` | The signature check works on this board. The time should be 100 ms or less. | `QR access off: self-test failed` means don't use QR on this build. Report it. |
| `QR access on for unit <uuid>, key ostium-qr-1` | The QR settings are valid. | `QR access off: <reason>` names the setting to fix in `secrets.h`. |
| `loop stack: <n> bytes never used` | Stack headroom. It should be at least 2,048. | A smaller number risks a crash. Report it. |
| `Backend configuration OK` | The API root, device ID, token, and certificate are set. | An `ERROR:` line before it names the missing setting, and the door doesn't poll. |
| `Wi-Fi: connecting to SSID <ssid>` | The door is joining Wi-Fi. | `Wi-Fi ERROR: SSID not found` or `connection failed` means check the network name, password, and 2.4 GHz. |
| `Access panel initialized` | The camera, LCD, and sensors started. | `Camera initialization failed: <n>` means check the camera ribbon cable and the board's PSRAM. |
| `Ostium Core startup complete` | Setup finished. | |
| `server clock set (window <n> ms)` | The first poll succeeded, so the door knows the server's time and QR access can work. | Repeated `poll failed:` lines mean the backend, the certificate, or the token is wrong. `backend refused this device (401)` means the token or device status is wrong. |

Then check the lock and the reports:

1. **After boot:** the lock LED is off.
2. **Remote unlock:** send Unlock from the app. Within about 2 seconds, the door logs `ran command <id>: unlock`, the LED lights for 3 seconds, and then the door logs `event <id> delivered`.
3. **QR access:** show a code from the app's QR screen. The door logs `QR granted credential <jti>`, the LED lights for 3 seconds, and the door logs `event qr-<jti>-granted delivered`. The app then shows *Access granted*.

A `dropped event` line means the backend refused a report for good. If it says to check `devices.unit_id`, ask the backend owner to set it to `OSTIUM_UNIT_ID`. A line that ends in `retrying` is normal while the backend is unavailable: the door keeps the report and tries again.

### Roll back

A rollback is a deploy of an earlier release:

1. Find the last good release on the **Releases** page, for example `firmware-r56`.
2. Before you flash, wait until the monitor shows `delivered` for every recent event. The door keeps unsent reports only in memory, so a reflash loses them.
3. Follow [Deploy a release](#deploy-a-release) with that tag.

After any restart, including a rollback, the door refuses QR codes issued before the restart (`issued_before_boot`), and it waits for its first poll before it accepts any code (`clock_unsynced`). Ask people at the door to refresh the code in the app.

If the bad release came from a change on `main`, also revert that pull request. The next merge then publishes a fixed release.

### Where to look next

- [Lock command contract](firmware/lock-command-v0.md): every command rule and setting.
- [QR access contract](firmware/qr-access-v0.md): the QR rules, the bench procedure, and every `QR access off` reason.
- [Door unit README](../hardware/esp32/README.md): building and testing on a laptop.
