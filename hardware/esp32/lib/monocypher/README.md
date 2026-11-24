# Monocypher

This folder vendors [Monocypher](https://monocypher.org/) 4.0.2, unmodified, for the Ed25519 signature check on QR credentials (SCRUM-38). The board build and the native tests compile the same files, so CI runs the same verifier as the door unit.

## Source

| Item | Value |
|---|---|
| Version | 4.0.2 |
| Upstream | <https://monocypher.org/download/monocypher-4.0.2.tar.gz> |
| Tarball SHA-256 | `38d07179738c0c90677dba3ceb7a7b8496bcfea758ba1a53e803fed30ae0879c` |
| Tarball SHA-512 | `bf275d4c53ff94af6cdc723a4e002e9f080f4d1436c86c76bb37870b34807f1d7b32331d8ff8a1aeb369e946f3769021e03e63efac25b82efc5abf54dc084714`, which matches the hash that monocypher.org publishes |
| License | BSD-2-Clause OR CC0-1.0, in `LICENCE.md`. Either is compatible with the repository's MIT license. |

## Files copied

Each file is a byte-for-byte copy from the tarball. The two Ed25519 files come from upstream's `src/optional/` folder. They sit next to `monocypher.h` here, which works because they include it by its quoted name, `"monocypher.h"`.

| File here | Path in the tarball | SHA-256 |
|---|---|---|
| `src/monocypher.c` | `monocypher-4.0.2/src/monocypher.c` | `afe2b098c8569577a84488e0b98d276d1fba6506adea68bb9241a52111734c59` |
| `src/monocypher.h` | `monocypher-4.0.2/src/monocypher.h` | `f78bb31255cfb7beba66afd2137f5194c8a025cf40488b6cc1e295234d43f374` |
| `src/monocypher-ed25519.c` | `monocypher-4.0.2/src/optional/monocypher-ed25519.c` | `7c9b16056cbd27521919e8a6f56a228808b9e718afc42e3d33f28c08e5abdee2` |
| `src/monocypher-ed25519.h` | `monocypher-4.0.2/src/optional/monocypher-ed25519.h` | `bd546edcd468d64e28caa3dbf4b1d6bfad7435c0ce994723fd81aae26405121b` |
| `LICENCE.md` | `monocypher-4.0.2/LICENCE.md` | `5f8360e4c06ddcc584bdb4b210c6af824c4bb301e6a9a521869b6d90795ca4b3` |

`library.json` and this README are the only files written for the repository.

## What the firmware uses

- The board uses only `crypto_ed25519_check`. `ostium::verifyEd25519` in `lib/ostium_core/src/ed25519.cpp` calls it, and that's the only core file that includes `monocypher-ed25519.h`.
- The native tests also use `crypto_ed25519_key_pair` and `crypto_ed25519_sign`, to derive test keys and sign test tokens.

The `ostium_core` library doesn't list `monocypher` as a dependency in its `library.json`, because PlatformIO would then look for it in the registry. PlatformIO's library dependency finder picks up this folder from the `#include <monocypher-ed25519.h>` in `ed25519.cpp`. Both builds compile it with their own flags, so the native build checks it under `-Wall -Wextra -Werror`.

## Update or check the files

Don't edit these files. To move to a newer release:

1. Download the release tarball from monocypher.org, and check its hash against the one the site publishes.
2. Replace the four source files and `LICENCE.md` with the release's copies, from the same paths.
3. Update the version in `library.json`, and the version, hashes, and paths in this README.
4. Run the native tests and both board builds, from the repository root:

   ```sh
   pio test -d hardware/esp32/native-tests
   pio run -d hardware/esp32 -e esp32s3
   pio run -d hardware/esp32 -e esp32s3-bench
   ```

   The RFC 8032 vectors and `test_ed25519_rejects_non_canonical_signature` in `test/test_core/test_ed25519.cpp` must pass. If the non-canonical test fails, add an explicit S < L check to `ed25519.cpp`, as its comment says.

To check that the copies here are still unmodified, run `shasum -a 256 src/* LICENCE.md` in this folder and compare the output with the table.
