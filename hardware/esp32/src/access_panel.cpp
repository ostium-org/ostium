
#include "access_panel.h"

#include <Arduino.h>
#include <esp_camera.h>
#include <ESP32QRCodeReader.h>
#include <LiquidCrystal.h>
#include <DHT.h>

#include "board_config.h"

#include <math.h>
#include <string.h>

// ==================================================
// CONFIGURATION
// ==================================================

namespace board
{

    namespace
    {

        // --------------------------------------------------
        // PIN CONFIGURATION
        // --------------------------------------------------

        constexpr int DHT_PIN = 19;
        constexpr int SOUND_PIN = 1;
        constexpr int LED_PIN = 14;

        // There's no lock relay yet, so the LED on GPIO 14 stands in for it:
        // it's the lock output, OSTIUM_LOCK_PIN. While LED_PIN is that pin,
        // only LockController drives it, so the LED lights for exactly each
        // unlock. The panel's own LED patterns stay off, so a denial never
        // flashes the "lock" open and a grant never relocks it early. The
        // camera reader must not set up that pin either: its ESP-EYE pull-up
        // workaround did, so it's removed from ESP32QRCodeReader::setup().
        constexpr bool kPanelDrivesLed = LED_PIN != OSTIUM_LOCK_PIN;

        constexpr int LCD_RS = 2;
        constexpr int LCD_EN = 41;
        constexpr int LCD_D4 = 42;
        constexpr int LCD_D5 = 47;
        constexpr int LCD_D6 = 21;
        constexpr int LCD_D7 = 20;

        constexpr uint32_t SENSOR_INTERVAL_MS = 2000;
        constexpr uint32_t DISPLAY_INTERVAL_MS = 500;
        constexpr uint32_t QR_MESSAGE_MS = 3000;

        // --------------------------------------------------
        // CAMERA CONFIGURATION
        // --------------------------------------------------

        CameraPins cameraPins = {
            -1, // PWDN
            -1, // RESET
            15, // XCLK
            4,  // SIOD
            5,  // SIOC
            16, // Y9
            17, // Y8
            18, // Y7
            12, // Y6
            10, // Y5
            8,  // Y4
            9,  // Y3
            11, // Y2
            6,  // VSYNC
            7,  // HREF
            13  // PCLK
        };

        // --------------------------------------------------
        // HARDWARE OBJECTS
        // --------------------------------------------------

        DHT dht(DHT_PIN, DHT11);

        LiquidCrystal lcd(
            LCD_RS,
            LCD_EN,
            LCD_D4,
            LCD_D5,
            LCD_D6,
            LCD_D7);

        ESP32QRCodeReader reader(
            cameraPins,
            FRAMESIZE_QVGA);

        // --------------------------------------------------
        // MODULE STATE
        // --------------------------------------------------

        bool initialized = false;

        uint32_t lastSensorRead = 0;
        uint32_t lastDisplayUpdate = 0;

        PanelTelemetry currentTelemetry;

        // --------------------------------------------------
        // LCD MESSAGE STATE
        // --------------------------------------------------

        bool messageActive = false;
        uint32_t messageUntil = 0;

        void writeLCDMessage(
            const char *line1,
            const char *line2)
        {
            char row1[17] = {};
            char row2[17] = {};

            snprintf(row1, sizeof(row1), "%-16.16s",
                     line1 ? line1 : "");

            snprintf(row2, sizeof(row2), "%-16.16s",
                     line2 ? line2 : "");

            lcd.setCursor(0, 0);
            lcd.print(row1);

            lcd.setCursor(0, 1);
            lcd.print(row2);
        }

        void showTimedMessage(
            const char *line1,
            const char *line2,
            uint32_t durationMs)
        {
            writeLCDMessage(line1, line2);

            messageActive = true;
            messageUntil = millis() + durationMs;
        }

        // --------------------------------------------------
        // LED STATE
        // --------------------------------------------------

        bool ledState = false;
        bool ledPatternActive = false;

        uint8_t ledFlashesRemaining = 0;

        uint32_t ledNextTransition = 0;
        uint32_t ledOnInterval = 0;
        uint32_t ledOffInterval = 0;

        void setLed(bool state)
        {
            ledState = state;

            if (!kPanelDrivesLed)
            {
                return;
            }

            digitalWrite(LED_PIN, state ? HIGH : LOW);
        }

        void startLedPattern(
            uint32_t nowMs,
            uint8_t flashes,
            uint32_t onMs,
            uint32_t offMs)
        {
            if (flashes == 0)
            {
                setLed(false);
                ledPatternActive = false;
                return;
            }

            ledPatternActive = true;
            ledFlashesRemaining = flashes;

            ledOnInterval = onMs;
            ledOffInterval = offMs;

            setLed(true);

            ledNextTransition = nowMs + ledOnInterval;
        }

        void updateLed(uint32_t nowMs)
        {
            if (!ledPatternActive)
            {
                return;
            }

            if (static_cast<int32_t>(
                    nowMs - ledNextTransition) < 0)
            {
                return;
            }

            if (ledState)
            {
                // Finish the current flash.
                setLed(false);

                if (ledFlashesRemaining > 0)
                {
                    ledFlashesRemaining--;
                }

                if (ledFlashesRemaining == 0)
                {
                    ledPatternActive = false;
                    return;
                }

                ledNextTransition = nowMs + ledOffInterval;
            }
            else
            {
                // Start the next flash.
                setLed(true);

                ledNextTransition = nowMs + ledOnInterval;
            }
        }

        // --------------------------------------------------
        // SOUND RMS
        // Non-blocking sampling
        // --------------------------------------------------

        constexpr uint32_t SAMPLE_WINDOW_MS = 50;

        uint32_t soundWindowStart = 0;

        uint32_t soundSampleCount = 0;

        double soundSum = 0.0;
        double soundSumSquares = 0.0;

        uint16_t soundDiagnosticMin = 4095;
        uint16_t soundDiagnosticMax = 0;

        double soundDiagnosticSum = 0.0;
        uint32_t soundDiagnosticCount = 0;

        uint16_t soundWindowMin = 4095;
        uint16_t soundWindowMax = 0;
        double soundWindowMean = 0.0;

        bool dhtLastReadValid = false;
        uint32_t dhtLastSuccessMs = 0;

        void updateSound(uint32_t nowMs)
        {
            const uint16_t reading = analogRead(SOUND_PIN);

            // Collect readings for the longer diagnostic window.
            soundDiagnosticSum += reading;
            soundDiagnosticCount++;

            if (reading < soundDiagnosticMin)
            {
                soundDiagnosticMin = reading;
            }

            if (reading > soundDiagnosticMax)
            {
                soundDiagnosticMax = reading;
            }

            soundSum += reading;

            soundSumSquares +=
                static_cast<double>(reading) * reading;

            if (reading < soundWindowMin)
            {
                soundWindowMin = reading;
            }

            if (reading > soundWindowMax)
            {
                soundWindowMax = reading;
            }

            soundSampleCount++;

            if (nowMs - soundWindowStart < SAMPLE_WINDOW_MS)
            {
                return;
            }

            if (soundSampleCount >= 2)
            {
                const double mean =
                    soundSum / soundSampleCount;

                const double meanSquares =
                    soundSumSquares / soundSampleCount;

                const double variance =
                    meanSquares - (mean * mean);

                currentTelemetry.soundRMS =
                    sqrt(fmax(0.0, variance));

                soundWindowMean = mean;
            }

            soundWindowStart = nowMs;

            soundSampleCount = 0;

            soundSum = 0.0;
            soundSumSquares = 0.0;

            soundWindowMin = 4095;
            soundWindowMax = 0;
        }

        // --------------------------------------------------
        // SENSOR DISPLAY
        // --------------------------------------------------

        void updateDisplay()
        {
            if (messageActive)
            {
                return;
            }

            char line1[17];
            char line2[17];

            if (isnan(currentTelemetry.temperature) ||
                isnan(currentTelemetry.humidity))
            {

                snprintf(
                    line1,
                    sizeof(line1),
                    "T:--.-C H:--%%");
            }
            else
            {

                snprintf(
                    line1,
                    sizeof(line1),
                    "T:%4.1fC H:%2.0f%%",
                    currentTelemetry.temperature,
                    currentTelemetry.humidity);
            }

            snprintf(
                line2,
                sizeof(line2),
                "Sound RMS:%5.1f",
                currentTelemetry.soundRMS);

            writeLCDMessage(line1, line2);
        }

    } // anonymous namespace

    // ==================================================
    // ACCESS PANEL INITIALIZATION
    // ==================================================

    bool AccessPanel::begin()
    {
        Serial.println("Initializing access panel...");

        // ----------------------------------------------
        // 1. Initialize camera FIRST.
        // ----------------------------------------------

        Serial.println("Initializing QR camera...");

        reader.cameraConfig.fb_location =
            CAMERA_FB_IN_PSRAM;

        reader.cameraConfig.grab_mode =
            CAMERA_GRAB_WHEN_EMPTY;

        Serial.println("Starting camera setup...");

        Serial.printf("Free heap before camera: %u\n",
                      ESP.getFreeHeap());

        Serial.printf("Free PSRAM before camera: %u\n",
                      ESP.getFreePsram());

        QRCodeReaderSetupErr result = reader.setup();

        Serial.printf("Camera setup returned: %d\n",
                      (int)result);

        Serial.printf("Free heap after camera: %u\n",
                      ESP.getFreeHeap());

        if (result != SETUP_OK)
        {
            Serial.println("ERROR: Camera setup failed!");
            return false;
        }

        if (result != SETUP_OK)
        {
            Serial.printf(
                "Camera initialization failed: %d\n",
                (int)result);

            return false;
        }

        sensor_t *sensor = esp_camera_sensor_get();

        if (sensor != nullptr)
        {
            sensor->set_hmirror(sensor, 1);
            sensor->set_vflip(sensor, 0);
        }

        Serial.println("Camera initialized.");

        // ----------------------------------------------
        // 2. Initialize LED.
        // ----------------------------------------------

        if (kPanelDrivesLed)
        {
            pinMode(LED_PIN, OUTPUT);

            setLed(false);

            Serial.println("LED initialized.");
        }
        else
        {
            Serial.println(
                "LED is the lock stand-in on OSTIUM_LOCK_PIN; LockController drives it.");
        }

        // ----------------------------------------------
        // 3. Initialize LCD.
        // ----------------------------------------------

        lcd.begin(16, 2);

        showMessage("Ostium Core", "Starting...");

        Serial.println("LCD initialized.");

        // ----------------------------------------------
        // 4. Initialize sensors.
        // ----------------------------------------------

        analogReadResolution(12);

        pinMode(SOUND_PIN, INPUT);

        // Initialize telemetry so unavailable readings are not
        // mistakenly displayed as zero.
        currentTelemetry.temperature = NAN;
        currentTelemetry.humidity = NAN;
        currentTelemetry.soundRMS = 0.0;

        dhtLastReadValid = false;
        dhtLastSuccessMs = 0;

        soundWindowMin = 4095;
        soundWindowMax = 0;
        soundWindowMean = 0.0;

        dht.begin();

        lastSensorRead = millis();

        soundWindowStart = millis();

        Serial.println("Sensors initialized.");

        // ----------------------------------------------
        // 5. Start QR decoder task.
        // ----------------------------------------------

        Serial.println("Starting QR decoder task.");

        reader.beginOnCore(1);

        initialized = true;

        writeLCDMessage("Ready to scan", "Show QR code");

        Serial.println("Access panel initialized.");

        return true;
    }

    // ==================================================
    // MAIN MODULE UPDATE
    // ==================================================

    void AccessPanel::update(uint32_t nowMs)
    {
        if (!initialized)
        {
            return;
        }

        // LED state machine.
        updateLed(nowMs);

        // LCD message timeout.
        if (messageActive &&
            static_cast<int32_t>(
                nowMs - messageUntil) >= 0)
        {

            messageActive = false;

            lastDisplayUpdate = 0;
        }

        // Sound sampling.
        updateSound(nowMs);

        // ----------------------------------------------
        // Temperature and humidity.
        // ----------------------------------------------

        if (nowMs - lastSensorRead >= SENSOR_INTERVAL_MS)
        {
            lastSensorRead = nowMs;

            const float temperature = dht.readTemperature();
            const float humidity = dht.readHumidity();

            if (!isnan(temperature) && !isnan(humidity))
            {
                currentTelemetry.temperature = temperature;
                currentTelemetry.humidity = humidity;

                dhtLastReadValid = true;
                dhtLastSuccessMs = nowMs;
            }
            else
            {
                dhtLastReadValid = false;

                Serial.println(
                    "[SENSOR WARNING] DHT11 reading unavailable.");
            }

            // ----------------------------------------------
            // SENSOR DIAGNOSTICS
            // ----------------------------------------------

            Serial.println();
            Serial.println("========== ACCESS PANEL SENSORS ==========");

            // DHT11 readings.
            if (dhtLastReadValid)
            {
                Serial.printf(
                    "Temperature: %.1f C\n",
                    currentTelemetry.temperature);

                Serial.printf(
                    "Humidity:    %.1f %%\n",
                    currentTelemetry.humidity);
            }
            else
            {
                Serial.println("Temperature: unavailable");
                Serial.println("Humidity:    unavailable");

                if (dhtLastSuccessMs > 0)
                {
                    Serial.printf(
                        "Last successful DHT11 reading: %lu ms ago\n",
                        static_cast<unsigned long>(
                            nowMs - dhtLastSuccessMs));
                }
            }

            // Sound sensor readings.
            Serial.printf(
                "Sound RMS:   %.2f ADC\n",
                currentTelemetry.soundRMS);

            Serial.printf(
                "Sound Avg:   %.1f ADC\n",
                soundDiagnosticCount > 0
                    ? soundDiagnosticSum / soundDiagnosticCount
                    : 0.0);

            Serial.printf(
                "Sound Min:   %u ADC\n",
                soundDiagnosticMin);

            Serial.printf(
                "Sound Max:   %u ADC\n",
                soundDiagnosticMax);

            Serial.println("==========================================");
            Serial.println();

            // Reset 2-second sound diagnostic collection.
            soundDiagnosticMin = 4095;
            soundDiagnosticMax = 0;

            soundDiagnosticSum = 0.0;
            soundDiagnosticCount = 0;
        }

        // ----------------------------------------------
        // LCD refresh.
        // ----------------------------------------------

        if (nowMs - lastDisplayUpdate >= DISPLAY_INTERVAL_MS)
        {
            lastDisplayUpdate = nowMs;

            updateDisplay();
        }
    }

    // ==================================================
    // QR FEEDBACK
    // ==================================================

    void AccessPanel::show(ostium::QrSignal signal)
    {
        if (!initialized)
        {
            return;
        }

        const uint32_t nowMs = millis();

        switch (signal)
        {
        case ostium::QrSignal::Granted:
            showTimedMessage("ACCESS GRANTED", "Welcome!", QR_MESSAGE_MS);

            // With its own LED, the panel lights it for 2 seconds
            // (kPanelDrivesLed).
            startLedPattern(nowMs, 1, 2000, 150);
            break;

        case ostium::QrSignal::Denied:
            showTimedMessage("ACCESS DENIED", "Try again later", QR_MESSAGE_MS);

            // With its own LED, the panel flashes it three times
            // (kPanelDrivesLed).
            startLedPattern(nowMs, 3, 200, 150);
            break;

        case ostium::QrSignal::Wait:
            // A transient denial: the door hasn't polled yet, or the code is
            // newer than the door's clock. The same code may work shortly.
            showTimedMessage("PLEASE WAIT", "Show code again", QR_MESSAGE_MS);
            break;

        case ostium::QrSignal::None:
            break;
        }
    }

    // ==================================================
    // CAMERA SCAN SOURCE
    // ==================================================

    bool CameraScanSource::next(ostium::QrScan *scan)
    {
        if (!initialized || scan == nullptr)
        {
            return false;
        }

        // One entry is about 1 KB, so it stays off the loop task's stack.
        // Only the loop task calls next().
        static QRCodeData decode;
        // Periodically report that the camera is waiting for a QR code.
        static uint32_t lastCameraStatusMs = 0;

        if (millis() - lastCameraStatusMs >= 3000)
        {
            lastCameraStatusMs = millis();

            Serial.println(
                "[CAMERA] Running - waiting for QR code...");
        }

        // Failed decodes carry quirc's error text, not a payload, so skip
        // them. Never log a payload: it's a bearer credential (Q20).
        while (reader.receiveQrCode(&decode, 0))
        {
            Serial.println("[CAMERA] QR decoding result received.");

            Serial.printf(
                "[CAMERA] Decode valid: %s\n",
                decode.valid ? "YES" : "NO");

            Serial.printf(
                "[CAMERA] Payload length: %u bytes\n",
                static_cast<unsigned int>(decode.payloadLen));

            if (ostium::scanFromCameraDecode(
                    decode.valid,
                    decode.payload,
                    decode.payloadLen,
                    sizeof(decode.payload) - 1,
                    decode.decodedAtMs,
                    scan))
            {
                Serial.println(
                    "[CAMERA] QR successfully converted into a scan.");

                return true;
            }

            Serial.println(
                "[CAMERA] QR result rejected by scan parser.");
        }

        return false;
    }

    // ==================================================
    // TELEMETRY
    // ==================================================

    const PanelTelemetry &AccessPanel::telemetry() const
    {
        return currentTelemetry;
    }

    // ==================================================
    // LCD MESSAGE INTERFACE
    // ==================================================

    void AccessPanel::showMessage(
        const char *line1,
        const char *line2)
    {
        if (!initialized)
        {
            return;
        }

        messageActive = false;

        writeLCDMessage(line1, line2);
    }

} // namespace board
