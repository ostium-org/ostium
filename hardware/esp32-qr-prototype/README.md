# Ostium Core --- QR Access-Control Gateway

> This folder is a standalone QR prototype for the Freenove ESP32-S3 WROOM. It's separate from the MVP door firmware in `hardware/esp32/`. To build it from the repository root, run `pio run -d hardware/esp32-qr-prototype`.

**Firmware version:** 1.0\
**Platform:** Freenove ESP32-S3 WROOM camera board\
**Development environment:** Arduino IDE 2.x\
**Firmware language:** Arduino C++

## 1. Project overview

Ostium Core is an ESP32-S3-based QR access-control prototype. The
onboard camera scans QR codes while the device monitors temperature,
humidity, and sound level. A 16x2 LCD displays live sensor readings or
temporary access-status messages. When the decoded QR payload exactly
matches the configured authorized string, the firmware moves a
positional servo to its open position, holds it there for five seconds,
and returns it to the closed position. Other valid QR payloads result in
an access-denied message.

### Current behavior

  -----------------------------------------------------------------------
  Event                               Expected behavior
  ----------------------------------- -----------------------------------
  No QR code is decoded               LCD displays temperature, humidity,
                                      and sound RMS

  Authorized QR payload               LCD displays `ACCESS GRANTED`;
                                      servo moves to 180deg, holds for 5
                                      seconds, then returns to 0deg

  Other valid QR payload              LCD displays `ACCESS DENIED` for
                                      about 3 seconds

  During an access message/action     Sensor readings and normal LCD
                                      updates pause

  Serial Monitor                      Prints startup messages, servo
                                      commands, sensor values, and QR
                                      results
  -----------------------------------------------------------------------

The authorized payload currently configured in the firmware is:

``` text
https://www.youtube.com/watch?v=dQw4w9WgXcQ
```

The comparison is an exact string comparison. A QR code containing a
different URL, extra whitespace, or a different string is denied.

> **Prototype note:** This is an extemely early version of the
> ostium core. The access QR is hardcoded and very low securty,
> especially given the weakness of the "lock" mechanism. Future
> updates will iterate on this as the device transitions past
> the MVP stage

## 2. Hardware

### Required components

-   Freenove ESP32-S3 WROOM camera development board with PSRAM
-   DHT11 temperature and humidity sensor
-   KY-037 sound sensor module, using its **analog output (AO)**
-   1602/1602A-compatible 16x2 character LCD, connected in 4-bit
    parallel mode
-   10k ohm potentiometer for LCD contrast
-   SG90 or compatible positional servo
-   Appropriate USB data cable
-   Jumper wires and a breadboard or equivalent wiring
-   Suitable regulated 5 V supply for the servo, as needed by the servo
    and LCD (likely available on ESP32-S3 dev board)

### Power and wiring cautions

-   **Do not power the servo motor from an ESP32 GPIO.** Use an
    appropriate servo supply. Connect the servo supply ground to ESP32
    ground so the control signal has a shared reference.
-   Confirm the LCD module's supply-voltage requirements. A conventional
    LCD1602 uses 5 V for VDD, but its logic levels and any
    backpack/interface circuitry must be compatible with the ESP32-S3's
    3.3 V GPIO.
-   The ESP32-S3 GPIOs are **not 5 V tolerant**. Do not allow a 5 V
    signal from a sensor or LCD interface to reach a GPIO.
-   Connect the KY-037 **AO** output to the ESP32-S3 analog input. The
    sketch does not use the module's digital output (DO).
-   The onboard camera is connected internally; its camera signals do
    not need external jumper wires.
-   Check your exact board's pin labels and schematic before wiring.
    Board variants can differ.

## 3. Pinouts

### External peripherals

  ------------------------------------------------------------------------
  Peripheral       Peripheral pin     ESP32-S3 connection Notes
  ---------------- ---------------- --------------------- ----------------
  DHT11            DATA / OUT                     GPIO 19 Temperature and
                                                          humidity

  DHT11            VCC                Suitable supply for Check module
                                      the specific module voltage
                                                          requirements

  DHT11            GND                                GND Shared ground

  KY-037           AO                              GPIO 1 Analog sound
                                                          signal used for
                                                          RMS calculation

  KY-037           VCC                    Suitable module Ensure AO never
                                                   supply exceeds 3.3 V

  KY-037           GND                                GND Shared ground

  SG90 servo       Signal                         GPIO 14 PWM control

  SG90 servo       V+ / red wire       External regulated Do not power
                                            servo supply, from GPIO
                                            typically 5 V 

  SG90 servo       GND / brown or              Common GND Connect to ESP32
                   black wire                             ground

  LCD1602          RS                              GPIO 2 Register select

  LCD1602          E                              GPIO 41 Enable

  LCD1602          D4                             GPIO 42 4-bit data

  LCD1602          D5                             GPIO 47 4-bit data

  LCD1602          D6                             GPIO 21 4-bit data

  LCD1602          D7                             GPIO 20 4-bit data

  LCD1602          RW                                 GND Write-only
                                                          operation

  LCD1602          VSS                                GND Ground

  LCD1602          VDD                    Appropriate LCD Common LCD1602
                                                   supply modules use 5 V;
                                                          verify logic
                                                          compatibility

  LCD1602          V0 / VO            Potentiometer wiper Adjusts contrast

  LCD1602          A / LED+             Per LCD backlight Use current
                                             requirements limiting if the
                                                          module does not
                                                          include it

  LCD1602          K / LED-           GND, as appropriate Backlight
                                                          cathode
  ------------------------------------------------------------------------

**LCD contrast potentiometer:** Connect the two outer terminals to the
3.3V V and GND for a conventional LCD1602 and the middle/wiper terminal 
to V0. Adjust slowly until the characters are clear. Confirm that the 
LCD and ESP32-S3 interface voltage levels are safe before connecting 
5 V LCD directly to GPIO.

The firmware creates the LCD object dynamically **after**
`servo.attach()`. Preserve this order when modifying the sketch; the
project has previously experienced an LCD/servo initialization
interaction that causes an error/crashing undetected by the IDE at
compilation. The root cause of this is unknown.

### Onboard camera GPIO mapping

These are the camera signal assignments used by the working Freenove
ESP32-S3 WROOM camera configuration in the firmware. They are onboard
connections, not external wiring instructions.

  Camera signal                     ESP32-S3 GPIO
  ----------------- -----------------------------
  PWDN                -1 (not connected / unused)
  RESET               -1 (not connected / unused)
  XCLK                                    GPIO 15
  SIOD / SCCB SDA                          GPIO 4
  SIOC / SCCB SCL                          GPIO 5
  Y9 / D7                                 GPIO 16
  Y8 / D6                                 GPIO 17
  Y7 / D5                                 GPIO 18
  Y6 / D4                                 GPIO 12
  Y5 / D3                                 GPIO 10
  Y4 / D2                                  GPIO 8
  Y3 / D1                                  GPIO 9
  Y2 / D0                                 GPIO 11
  VSYNC                                    GPIO 6
  HREF                                     GPIO 7
  PCLK                                    GPIO 13

The QR reader is configured at `FRAMESIZE_QVGA`. It initializes the
camera through `ESP32QRCodeReader`; do not separately call
`esp_camera_init()` in this sketch unless you deliberately refactor the
QR library's camera setup.

## 4. Software and dependencies

### 4.1 Install Arduino IDE

1.  Download and install Arduino IDE 2.x from the official Arduino
    website: <https://www.arduino.cc/en/software>.

2.  Start Arduino IDE and open **File -> Preferences** (on macOS,
    **Arduino IDE -> Settings/Preferences**, depending on IDE version).

3.  In **Additional Boards Manager URLs**, add the official Espressif
    stable package index if it is not already present:

    `https://espressif.github.io/arduino-esp32/package_esp32_index.json`

4.  Open **Tools -> Board -> Boards Manager**.

5.  Search for `esp32` and install **esp32 by Espressif Systems**.

6.  Wait for installation to finish. Restart Arduino IDE if needed.

Official setup reference:
<https://docs.espressif.com/projects/arduino-esp32/en/latest/installing.html>

### 4.2 Select the board and enable PSRAM

1.  Connect the ESP32-S3 board using a known-good **USB data cable**.
2.  Select **Tools -> Board -> esp32 -> ESP32S3 Dev Module**.
3.  Select the correct port under **Tools -> Port**.
4.  Open the board options under **Tools**.
5.  Enable/configure PSRAM using the option appropriate to the actual
    ESP32-S3 module. Depending on the board package and module variant,
    the option may be labelled **OPI PSRAM**, **QSPI PSRAM**, or
    similar. Use the setting that matches the fitted module; do not
    choose blindly.
6.  Confirm at runtime that the Serial Monitor reports
    `PSRAM: detected`.

The QR decoding library requires PSRAM. If PSRAM is not detected, camera
initialization or QR decoding may fail.

### 4.3 Install Arduino libraries

Use **Sketch -> Include Library -> Manage Libraries...** (or the Library
Manager icon), search for each library, and install it.

  ---------------------------------------------------------------------------
  Library                 Search/install name         Used for
  ----------------------- --------------------------- -----------------------
  ESP32QRCodeReader       `ESP32QRCodeReader` by      QR decoding from camera
                          alvarowolfx                 frames

  ESP32Servo              `ESP32Servo`                Servo PWM control

  DHT sensor library      `DHT sensor library` by     DHT11
                          Adafruit                    temperature/humidity
                                                      readings

  Adafruit Unified Sensor `Adafruit Unified Sensor`   Dependency used by
                          by Adafruit                 Adafruit's DHT library
  ---------------------------------------------------------------------------

Please note that `ESP32QRCodeReader` may be unavailable in the Arduino IDE
library manager. In that case, install by downloading the 
ZIP file from the GitHub and installing
 with **Sketch -> Include Library -> Add .ZIP Library...**

The `LiquidCrystal` library is normally included with Arduino IDE.
`Arduino.h`, `math.h`, `string.h`, and `esp_camera.h` are supplied by
the Arduino-ESP32 core or its toolchain; do not install an unrelated
third-party `esp_camera` library to replace the one bundled with the
ESP32 platform.

Library source/reference: - ESP32QRCodeReader:
<https://github.com/alvarowolfx/ESP32QRCodeReader> - ESP32Servo:
<https://github.com/madhephaestus/ESP32Servo> - Adafruit DHT sensor
library: <https://github.com/adafruit/DHT-sensor-library> - Adafruit
Unified Sensor: <https://github.com/adafruit/Adafruit_Sensor>

The QR library's upstream README *requires* a PSRAM-equipped ESP32
camera board. This project uses a custom `CameraPins` configuration
because the onboard Freenove pinout differs from the library's common
default camera models.

### 4.4 Open and upload the firmware

1.  Open the firmware sketch in Arduino IDE.
2.  Ensure the sketch folder and `.ino` filename follow Arduino's normal
    naming convention.
3.  Confirm the libraries above are installed.
4.  Select **ESP32S3 Dev Module** and the correct serial port.
5.  Click **Verify** (checkmark) to compile.
6.  Connect the sensors, LCD, and servo with power off, double-check all
    wiring, then reconnect USB and any external supply.
7.  Click **Upload**.
8.  If upload fails, follow any boot-mode instructions shown by Arduino
    IDE; some ESP32-S3 boards require holding **BOOT** while upload
    begins.
9.  Open **Tools -> Serial Monitor**, set the baud rate to **115200**,
    and reset the board if necessary.

### 4.5 Serial output

Normal startup output should include messages similar to:

``` text
Ostium Core QR Access Control
PSRAM: detected
Initializing QR camera...
QR camera initialized.
Setting servo PWM frequency.
Attaching servo.
Servo attach result: 1
Servo angle: 0
LCD initialized.
Starting QR decoder task.
Ostium Core started.
Monitoring sensors and QR codes.
```

Exact wording and the servo attach result can vary by library/core
version. A nonzero attach result indicates the servo library reported
successful attachment.

Sensor values are printed approximately every 500 ms. DHT11 values are
refreshed approximately every 2 seconds.

## 5. Firmware configuration

The primary settings are near the top of the sketch.

### Authorized QR payload

Change `AUTHORIZED_QR` to the exact string that should grant access:

``` cpp
const char* AUTHORIZED_QR =
  "https://www.youtube.com/watch?v=dQw4w9WgXcQ";
```

The code currently compares the decoded payload using `strcmp()`. It
does not automatically normalize URLs, remove whitespace, or treat
different URL forms as equivalent.

### Pin assignments

The external pin definitions are:

``` cpp
#define DHTPIN 19
#define SOUND_ANALOG_PIN 1
#define SERVO_PIN 14

#define LCD_RS 2
#define LCD_EN 41
#define LCD_D4 42
#define LCD_D5 47
#define LCD_D6 21
#define LCD_D7 20
```

If wiring changes, update both the physical wiring and these
definitions. Do not move the onboard camera pins without checking the
board's camera schematic and validating the mapping.

### Timing settings

  --------------------------------------------------------------------------
  Setting                                Current value Purpose
  ----------------------- ---------------------------- ---------------------
  `SENSOR_INTERVAL_MS`                         2000 ms Interval for DHT11
                                                       readings

  `DISPLAY_INTERVAL_MS`                         500 ms Sensor LCD and serial
                                                       update interval

  `QR_COOLDOWN_MS`                             5000 ms Suppresses repeated
                                                       QR actions briefly
                                                       after a result

  Authorized servo hold                        5000 ms Time held at the open
                                                       angle

  Denied message                               3000 ms Duration of
                                                       access-denied display

  Return delay                                 1000 ms Brief pause after
                                                       commanding the servo
                                                       closed

  `SAMPLE_WINDOW_MS`                             50 ms Window used to
                                                       calculate sound RMS

  `MAX_SAMPLES`                                   2048 Maximum samples in
                                                       one sound window
  --------------------------------------------------------------------------

### Servo behavior

The current access sequence commands:

1.  `servo.write(180)` --- open position.
2.  Wait 5 seconds.
3.  `servo.write(0)` --- return to the closed position.

This assumes a positional servo and a mechanism that can safely travel
through this range. Actual movement direction depends on servo
installation. Adjust endpoints if the mechanical linkage binds; do not
force the servo against a physical stop. The `servo.write()` command
sets a target angle and does not confirm that the shaft physically
reached it.

## 6. How the firmware works

1.  **Camera initialization:** The QR reader initializes the camera
    using the configured Freenove pin mapping and QVGA frame size.
2.  **Servo setup:** The servo PWM is configured and attached after
    camera initialization.
3.  **LCD setup:** The LCD object is dynamically constructed after servo
    attachment to preserve the working initialization order.
4.  **Sensor setup:** The analog-to-digital converter is configured, the
    sound input is set up, and the DHT11 is initialized.
5.  **QR decoder task:** `reader.beginOnCore(1)` starts the decoder task
    on core 1.
6.  **Main loop:** The firmware checks for decoded QR messages, then
    updates sensor values, LCD output, and serial output when no access
    action is underway.
7.  **Authorization:** A valid decoded payload is compared exactly with
    `AUTHORIZED_QR`. A match runs the access-granted sequence; any other
    valid payload runs the access-denied sequence.
8.  **Cooldown:** Recently queued detections are discarded after an
    action and a cooldown suppresses immediate retriggering.

The sound measurement is an RMS fluctuation calculation over analog
readings. It is a relative sound-level indicator, not a calibrated
decibel measurement.

## 7. Troubleshooting

### Camera or QR reader fails to initialize

-   Confirm the board is selected as **ESP32S3 Dev Module**.
-   Check that PSRAM is enabled with the correct mode for the installed
    module and that Serial Monitor reports `PSRAM: detected`.
-   Confirm the custom camera pin mapping matches the working Freenove
    camera test.
-   Ensure no other part of the sketch initializes the camera a second
    time.
-   Keep the frame size at QVGA while debugging.
-   Check for library compatibility errors if the ESP32 board package
    has recently been upgraded.

### QR codes are detected but cannot be decoded

-   Use a clearly printed or bright, high-contrast QR code.
-   Keep the entire code inside the camera's field of view and avoid
    glare.
-   Try moving the code closer or farther away.
-   Ensure the camera lens is focused and the image is not blurred.
-   The decoder can report an invalid detection before successfully
    decoding the payload; occasional invalid detections do not
    necessarily mean the camera is broken.

### Authorized code is denied

-   Print the decoded payload in Serial Monitor and compare it
    character-for-character with `AUTHORIZED_QR`.
-   Check for whitespace, line breaks, URL variations, or a different QR
    payload type.
-   The firmware authorizes the QR's text content; it does not verify
    that the URL is safe or that a remote website is trustworthy.

### Servo does not move

-   Confirm Serial Monitor reports a nonzero servo attachment result.
-   Verify the signal wire is connected to GPIO 14.
-   Power the servo from an appropriate regulated supply; do not draw
    motor current from a GPIO.
-   Connect the servo supply ground and ESP32 ground together.
-   Check that the mechanism can move freely and that the commanded
    angle range is safe.
-   Test the servo separately if it still does not move. A software
    attachment message does not prove that the servo has power or that
    the signal wire is connected correctly.

### LCD shows random characters or has poor contrast

-   Verify RS, E, D4--D7, RW, VSS, and VDD connections.
-   Connect RW to GND for write-only operation.
-   Adjust the contrast potentiometer connected to V0.
-   Confirm the LCD interface does not drive 5 V logic into ESP32-S3
    GPIOs.
-   Preserve the code order: camera setup, servo attachment, then
    dynamic LCD construction.

### DHT11 reports invalid values

-   Check VCC, GND, and the data connection to GPIO 19.
-   Confirm the selected sensor type is `DHT11`.
-   Allow at least about 2 seconds between DHT11 readings.
-   The firmware retains the last valid temperature/humidity values if a
    later read fails and prints a warning to Serial Monitor.

### Sound RMS is always zero or erratic

-   Ensure the sensor's **AO** pin is connected to GPIO 1, not DO.
-   Check the sensor's supply and common ground.
-   Confirm the analog output stays within the ESP32-S3 input-voltage
    range.
-   The RMS number depends on sensor gain, placement, ambient noise, and
    sampling; it is not expected to match a calibrated sound meter.

### Serial Monitor shows boot text but no ongoing sensor output

-   Set Serial Monitor to **115200 baud**.
-   Confirm the sketch reaches `Ostium Core started.`.
-   If the program remains at camera setup, inspect the preceding error
    output.
-   If the LCD works but serial readings stop after a QR action, check
    whether the access sequence is still in its timed delay or the
    firmware has stalled/reset.

### Upload fails or board is not detected

-   Try a known-good USB data cable and another USB port.
-   Close any other application using the serial port.
-   Select the correct port and ESP32-S3 board.
-   If required, hold BOOT while starting upload and release it when
    upload begins.
-   On Linux, verify that the user has permission to access the serial
    device.

## 8. Known limitations and future improvements

-   The authorized URL is hard-coded and can be extracted from firmware.
-   The firmware performs exact text matching, not signed-token or
    cryptographic authorization.
-   A copied QR code can be reused; the cooldown only reduces immediate
    repeated triggering and is not replay protection.
-   The servo command does not provide physical lock-position feedback.
-   The sound RMS value is relative and uncalibrated.
-   Camera decoding quality depends on lighting, focus, QR size, and
    angle.
-   Blocking delays are used for the access status and servo sequence;
    sensor display updates pause during these delays by design.
-   Electrical and mechanical safety depend on the final lock mechanism,
    power supply, and enclosure.

Potential future improvements include time-limited or signed QR tokens,
access logs, lock-position feedback, a watchdog/recovery strategy, and a
non-blocking state machine for access actions.

## 9. References

-   Arduino IDE downloads: <https://www.arduino.cc/en/software>
-   Espressif Arduino-ESP32 installation guide:
    <https://docs.espressif.com/projects/arduino-esp32/en/latest/installing.html>
-   ESP32QRCodeReader repository and examples:
    <https://github.com/alvarowolfx/ESP32QRCodeReader>
-   ESP32Servo repository: <https://github.com/madhephaestus/ESP32Servo>
-   Adafruit DHT sensor library:
    <https://github.com/adafruit/DHT-sensor-library>
-   Adafruit Unified Sensor library:
    <https://github.com/adafruit/Adafruit_Sensor>

------------------------------------------------------------------------

**End of README**
