
#include "access_panel.h"

#include <Arduino.h>
#include <esp_camera.h>
#include <ESP32QRCodeReader.h>
#include <LiquidCrystal.h>
#include <DHT.h>

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
        // AUTHORIZED QR
        // --------------------------------------------------

        // Testing credential only.
        // Replace with a protected authorization mechanism
        // before deploying this as a real access-control system.

        constexpr char AUTHORIZED_QR[] =
            "https://www.youtube.com/watch?v=dQw4w9WgXcQ";

        // --------------------------------------------------
        // PIN CONFIGURATION
        // --------------------------------------------------

        constexpr int DHT_PIN = 19;
        constexpr int SOUND_PIN = 1;
        constexpr int LED_PIN = 14;

        constexpr int LCD_RS = 2;
        constexpr int LCD_EN = 41;
        constexpr int LCD_D4 = 42;
        constexpr int LCD_D5 = 47;
        constexpr int LCD_D6 = 21;
        constexpr int LCD_D7 = 20;

        constexpr uint32_t SENSOR_INTERVAL_MS = 2000;
        constexpr uint32_t DISPLAY_INTERVAL_MS = 500;
        constexpr uint32_t QR_COOLDOWN_MS = 5000;

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

        void updateSound(uint32_t nowMs)
        {
            const uint16_t reading = analogRead(SOUND_PIN);

            soundSum += reading;

            soundSumSquares +=
                static_cast<double>(reading) * reading;

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
            }

            soundWindowStart = nowMs;

            soundSampleCount = 0;

            soundSum = 0.0;
            soundSumSquares = 0.0;
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

        // --------------------------------------------------
        // QR COOLDOWN
        // --------------------------------------------------

        bool hasHandledQr = false;
        uint32_t lastQrAction = 0;

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

        pinMode(LED_PIN, OUTPUT);

        setLed(false);

        Serial.println("LED initialized.");

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
            }
            else
            {
                Serial.println("Warning: DHT11 reading failed.");
            }
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
    // QR PROCESSING
    // ==================================================

    AccessResult AccessPanel::processQr()
    {
        if (!initialized)
        {
            return AccessResult::None;
        }

        const uint32_t nowMs = millis();

        QRCodeData qrCodeData;

        // ----------------------------------------------
        // Cooldown handling.
        // ----------------------------------------------

        if (hasHandledQr &&
            nowMs - lastQrAction < QR_COOLDOWN_MS)
        {

            while (reader.receiveQrCode(&qrCodeData, 0))
            {
                // Discard detections during cooldown.
            }

            return AccessResult::None;
        }

        // ----------------------------------------------
        // Receive QR code.
        // ----------------------------------------------

        if (!reader.receiveQrCode(&qrCodeData, 0))
        {
            return AccessResult::None;
        }

        if (!qrCodeData.valid)
        {
            Serial.println("QR detected but invalid.");
            return AccessResult::None;
        }

        // ----------------------------------------------
        // Safely extract payload.
        // ----------------------------------------------

        const size_t payloadSize =
            sizeof(qrCodeData.payload);

        const char *payload =
            reinterpret_cast<const char *>(qrCodeData.payload);

        const char *terminator =
            static_cast<const char *>(
                memchr(payload, '\0', payloadSize));

        if (terminator == nullptr)
        {
            Serial.println("QR payload too long or unterminated.");
            return AccessResult::None;
        }

        Serial.print("QR payload: ");
        Serial.println(payload);

        // ----------------------------------------------
        // Compare authorization.
        // ----------------------------------------------

        const bool authorized =
            strcmp(payload, AUTHORIZED_QR) == 0;

        hasHandledQr = true;
        lastQrAction = nowMs;

        // ----------------------------------------------
        // ACCESS GRANTED.
        // ----------------------------------------------

        if (authorized)
        {
            Serial.println("========== ACCESS GRANTED ==========");

            showTimedMessage(
                "ACCESS GRANTED",
                "Welcome!",
                3000);

            // LED stays on for 2 seconds.
            startLedPattern(nowMs, 1, 2000, 150);

            Serial.println("QR authorization accepted.");

            return AccessResult::Granted;
        }

        // ----------------------------------------------
        // ACCESS DENIED.
        // ----------------------------------------------

        Serial.println("========== ACCESS DENIED ==========");

        showTimedMessage(
            "ACCESS DENIED",
            "Try again later",
            3000);

        // Three LED flashes.
        startLedPattern(nowMs, 3, 200, 150);

        Serial.println("QR authorization rejected.");

        return AccessResult::Denied;
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
