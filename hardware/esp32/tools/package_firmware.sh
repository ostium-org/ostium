#!/usr/bin/env bash
# Packages the door firmware for a GitHub release (Assignment 2, Part C).
#
# Run it from the repository root, in the same place as both board builds,
# with no include/secrets.h:
#
#   pio run -d hardware/esp32 -e esp32s3
#   pio run -d hardware/esp32 -e esp32s3-bench
#   hardware/esp32/tools/package_firmware.sh <out-dir> <version> <commit>
#
# For each build it writes ostium-door-<build>-<version>.zip, holding every
# image a full flash needs plus FLASHING.txt, and then SHA256SUMS and
# release-notes.md. CI runs it on every build, so a packaging problem fails
# the pull request, and the release job publishes the result from main.
#
# The repository is public, so the images must hold the placeholder settings
# from include/secrets.example.h, never real secrets. The script stops if
# include/secrets.h exists, or if an image doesn't hold the placeholder Wi-Fi
# password and device token, which means it was built with a secrets.h.

set -euo pipefail

if [ "$#" -ne 3 ]; then
  echo "usage: $0 <out-dir> <version> <commit>" >&2
  exit 2
fi

out_dir=$1
version=$2
commit=$3

firmware_dir=hardware/esp32
builds="esp32s3 esp32s3-bench"
framework_dir="${PLATFORMIO_CORE_DIR:-$HOME/.platformio}/packages/framework-arduinoespressif32"
boot_app0="$framework_dir/tools/partitions/boot_app0.bin"

# ESP32-S3 offsets, as PlatformIO's Arduino build flashes them
# (framework-arduinoespressif32/tools/platformio-build.py).
flash_args="0x0 bootloader.bin 0x8000 partitions.bin 0xe000 boot_app0.bin 0x10000 firmware.bin"

rebuild_hint="move $firmware_dir/include/secrets.h out of the repository, then rebuild both board builds before you package"

if [ -e "$firmware_dir/include/secrets.h" ]; then
  echo "error: $firmware_dir/include/secrets.h exists, and a release must not contain real secrets; $rebuild_hint" >&2
  exit 1
fi

# The placeholder values of the two secrets. Moving secrets.h aside doesn't
# change images that were already built with it, so each image is checked too.
placeholder() {
  sed -n "s/^#define $1 \"\(.*\)\"\$/\1/p" "$firmware_dir/include/secrets.example.h"
}
placeholders=("$(placeholder OSTIUM_WIFI_PASSWORD)" "$(placeholder OSTIUM_DEVICE_TOKEN)")
for value in "${placeholders[@]}"; do
  if [ -z "$value" ]; then
    echo "error: couldn't read the placeholder Wi-Fi password and device token from $firmware_dir/include/secrets.example.h" >&2
    exit 1
  fi
done
if [ ! -f "$boot_app0" ]; then
  echo "error: $boot_app0 not found; run a board build first" >&2
  exit 1
fi

sha256() {
  if command -v sha256sum >/dev/null 2>&1; then sha256sum "$@"; else shasum -a 256 "$@"; fi
}

# Check every build before writing anything, so a failure leaves no package.
for build in $builds; do
  build_dir="$firmware_dir/.pio/build/$build"
  for image in bootloader.bin partitions.bin firmware.bin firmware.elf; do
    if [ ! -f "$build_dir/$image" ]; then
      echo "error: $build_dir/$image not found; run: pio run -d $firmware_dir -e $build" >&2
      exit 1
    fi
  done
  for image in firmware.bin firmware.elf; do
    for value in "${placeholders[@]}"; do
      if ! grep -q -a -F -e "$value" "$build_dir/$image"; then
        echo "error: $build_dir/$image lacks the placeholder settings from secrets.example.h, so it was built with a secrets.h; $rebuild_hint" >&2
        exit 1
      fi
    done
  done
done

mkdir -p "$out_dir"
work=$(mktemp -d)
trap 'rm -rf "$work"' EXIT

for build in $builds; do
  build_dir="$firmware_dir/.pio/build/$build"
  name="ostium-door-$build-$version"
  stage="$work/$name"
  mkdir -p "$stage"
  cp "$build_dir/bootloader.bin" "$build_dir/partitions.bin" "$build_dir/firmware.bin" \
    "$build_dir/firmware.elf" "$boot_app0" "$stage/"

  cat >"$stage/FLASHING.txt" <<EOF
Ostium door firmware $version, build $build, commit $commit.

These images use the placeholder settings in include/secrets.example.h: no
Wi-Fi, no device token, and QR access off. Flash them to check that a board
boots, keeps the lock output off, and passes the QR self-test. For a working
door, build commit $commit with your own include/secrets.h, as the operations
guide describes.

Full flash with esptool (ESP32-S3):

  esptool.py --chip esp32s3 --baud 460800 write_flash $flash_args

firmware.elf is the same build with debug symbols, for decoding a crash
backtrace from this release. Don't flash it.
EOF

  (cd "$work" && zip -qr "$name.zip" "$name")
  mv "$work/$name.zip" "$out_dir/"
done

(cd "$out_dir" && sha256 ostium-door-*-"$version".zip >SHA256SUMS)

cat >"$out_dir/release-notes.md" <<EOF
Door firmware built and tested by CI from commit \`$commit\`.

| File | What it is |
|---|---|
| \`ostium-door-esp32s3-$version.zip\` | The door build: HTTPS only. |
| \`ostium-door-esp32s3-bench-$version.zip\` | The bench build: it also reads \`qr <token>\` on Serial. Built with your own \`include/secrets.h\`, it can use a plain-HTTP API root for a laptop mock and trust the test-only QR key. Never flash it on a door. |
| \`SHA256SUMS\` | Checksums of both zips. |
| \`firmware-tests.xml\` | The native test report for this commit, when CI attached it. |

Both images hold the placeholder settings from \`include/secrets.example.h\`, never secrets: no Wi-Fi, no device token, and QR access off, so they refuse every QR code, including a \`qr <token>\` on the bench. Use them only to check that a board boots and passes the QR self-test. To deploy a door, check out this release's tag, build it with your own \`include/secrets.h\`, and flash that build. To roll back, do the same with the previous release's tag. The operations guide gives the steps. Don't flash these images on a door.

To check a board only, flash these images with esptool (ESP32-S3), from inside an unzipped folder:

\`\`\`sh
esptool.py --chip esp32s3 --baud 460800 write_flash $flash_args
\`\`\`
EOF

echo "packaged $version into $out_dir"
