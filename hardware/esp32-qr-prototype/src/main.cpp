// Ostium firmware v1.0
/*
  Ostium Gateway
  Hardware:
  - Freenove ESP32-S3-WROOM camera board
  - DHT11 temperature and humidity sensor
  - KY-037 analog sound sensor
  - LCD1602, 4-bit parallel mode (status streaming)
  - SG90 servo (lock)
*/
#include <Arduino.h>
#include <esp_camera.h>
#include <ESP32QRCodeReader.h>
#include <ESP32Servo.h>
#include <LiquidCrystal.h>
#include <DHT.h>
#include <math.h>
#include <string.h>

// ==================================================
// PIN CONFIGURATION
// ==================================================

// DHT11
#define DHTPIN 19
#define DHTTYPE DHT11

// KY-037 analog output
#define SOUND_ANALOG_PIN 1

// SG90 servo
#define SERVO_PIN 14

// LCD1602: RS, E, D4, D5, D6, D7
#define LCD_RS 2
#define LCD_EN 41
#define LCD_D4 42
#define LCD_D5 47
#define LCD_D6 21
#define LCD_D7 20

// ==================================================
// AUTHORIZED QR CODE
// ==================================================

const char* AUTHORIZED_QR =
  "https://www.youtube.com/watch?v=dQw4w9WgXcQ";

// ==================================================
// CAMERA PIN MAPPING: FREENOVE ESP32-S3 WROOM
// These are the pins from your working camera test.
// ==================================================

CameraPins cameraPins = {
  -1,  // PWDN
  -1,  // RESET
  15,  // XCLK
  4,   // SIOD
  5,   // SIOC
  16,  // Y9
  17,  // Y8
  18,  // Y7
  12,  // Y6
  10,  // Y5
  8,   // Y4
  9,   // Y3
  11,  // Y2
  6,   // VSYNC
  7,   // HREF
  13   // PCLK
};

ESP32QRCodeReader reader(cameraPins, FRAMESIZE_QVGA);

// ==================================================
// SENSOR AND SERVO OBJECTS
// ==================================================

DHT dht(DHTPIN, DHTTYPE);
Servo servo;

// IMPORTANT:
// Attach servo first, then dynamically construct LCD.
// Camera is initialized before the servo is attached.
LiquidCrystal* lcd = nullptr;

// ==================================================
// SOUND RMS CONFIGURATION
// ==================================================

#define MAX_SAMPLES 2048
#define SAMPLE_WINDOW_MS 50

uint16_t samples[MAX_SAMPLES];
float soundRMS = 0.0;

// ==================================================
// SENSOR VALUES AND TIMERS
// ==================================================

float temperature = NAN;
float humidity = NAN;

unsigned long lastSensorRead = 0;
unsigned long lastDisplayUpdate = 0;
unsigned long lastQrAction = 0;

const unsigned long SENSOR_INTERVAL_MS = 2000;
const unsigned long DISPLAY_INTERVAL_MS = 500;
const unsigned long QR_COOLDOWN_MS = 5000;

bool actionInProgress = false;
bool hasHandledQr = false;

int servoAngle = 0;

// ==================================================
// SOUND RMS CALCULATOR
// ==================================================

float readSoundRMS() {
  unsigned long startTime = millis();
  int sampleCount = 0;
  double sum = 0.0;

  while (millis() - startTime < SAMPLE_WINDOW_MS &&
         sampleCount < MAX_SAMPLES) {
    uint16_t reading = analogRead(SOUND_ANALOG_PIN);

    samples[sampleCount] = reading;
    sum += reading;
    sampleCount++;
  }

  if (sampleCount < 2) {
    return 0.0;
  }

  double mean = sum / sampleCount;
  double sumSquares = 0.0;

  for (int i = 0; i < sampleCount; i++) {
    double difference = samples[i] - mean;
    sumSquares += difference * difference;
  }

  return sqrt(sumSquares / sampleCount);
}

// ==================================================
// LCD MESSAGE HELPER
// ==================================================

void showMessage(const char* line1, const char* line2) {
  if (lcd == nullptr) {
    return;
  }

  lcd->clear();

  lcd->setCursor(0, 0);
  lcd->print(line1);

  lcd->setCursor(0, 1);
  lcd->print(line2);
}

// ==================================================
// SENSOR LCD UPDATE
// ==================================================

void updateLCD() {
  if (lcd == nullptr) {
    return;
  }

  char line1[17];
  char line2[17];

  if (isnan(temperature) || isnan(humidity)) {
    snprintf(line1, sizeof(line1), "T:--.-C H:--%%");
  } else {
    snprintf(
      line1, sizeof(line1),
      "T:%4.1fC H:%2.0f%%",
      temperature, humidity
    );
  }

  snprintf(line2, sizeof(line2), "Sound RMS:%5.1f", soundRMS);

  // Pad each row to remove leftover characters.
  int len1 = strlen(line1);
  while (len1 < 16) {
    line1[len1++] = ' ';
  }
  line1[16] = '\0';

  int len2 = strlen(line2);
  while (len2 < 16) {
    line2[len2++] = ' ';
  }
  line2[16] = '\0';

  lcd->setCursor(0, 0);
  lcd->print(line1);

  lcd->setCursor(0, 1);
  lcd->print(line2);
}

// ==================================================
// SERVO CONTROL
// ==================================================

void setServoAngle(int angle) {
  servoAngle = constrain(angle, 0, 180);
  servo.write(servoAngle);

  Serial.print("Servo angle: ");
  Serial.println(servoAngle);
}

// ==================================================
// DISCARD OLD QR DETECTIONS
// ==================================================

void discardQueuedQrCodes() {
  QRCodeData qrCodeData;

  while (reader.receiveQrCode(&qrCodeData, 0)) {
    // Intentionally discard queued detections.
  }
}

// ==================================================
// ACCESS GRANTED
// ==================================================

void grantAccess() {
  actionInProgress = true;

  Serial.println();
  Serial.println("========== ACCESS GRANTED ==========");

  showMessage("ACCESS GRANTED", "Opening...");

  setServoAngle(180);

  Serial.println("Holding at 180 degrees for 5 seconds.");
  delay(5000);

  showMessage("Returning...", "Please wait");

  setServoAngle(0);

  // Give the servo time to return to its starting position.
  delay(1000);

  Serial.println("Servo returned to 0 degrees.");
  Serial.println("===================================");

  discardQueuedQrCodes();

  lastQrAction = millis();
  hasHandledQr = true;

  // Force sensor values and LCD to refresh after the action.
  lastSensorRead = 0;
  lastDisplayUpdate = 0;

  actionInProgress = false;
}

// ==================================================
// ACCESS DENIED
// ==================================================

void denyAccess() {
  actionInProgress = true;

  Serial.println();
  Serial.println("========== ACCESS DENIED ==========");

  showMessage("ACCESS DENIED", "Try again later");

  delay(3000);

  discardQueuedQrCodes();

  lastQrAction = millis();
  hasHandledQr = true;

  lastSensorRead = 0;
  lastDisplayUpdate = 0;

  Serial.println("Returning to sensor monitor.");
  Serial.println("===================================");

  actionInProgress = false;
}

// ==================================================
// QR CODE PROCESSING
// ==================================================

void processQrCodes() {
  if (actionInProgress) {
    return;
  }

  QRCodeData qrCodeData;

  // Prevent the same QR held in front of the camera
  // from repeatedly triggering access.
  if (hasHandledQr &&
      millis() - lastQrAction < QR_COOLDOWN_MS) {
    while (reader.receiveQrCode(&qrCodeData, 0)) {
      // Discard detections during cooldown.
    }
    return;
  }

  if (!reader.receiveQrCode(&qrCodeData, 10)) {
    return;
  }

  if (!qrCodeData.valid) {
    Serial.println("QR detected but could not be decoded.");
    return;
  }

  // Ensure payload is null-terminated before comparing.
  qrCodeData.payload[sizeof(qrCodeData.payload) - 1] = '\0';

  const char* payload = (const char*)qrCodeData.payload;

  Serial.print("QR payload: ");
  Serial.println(payload);

  if (strcmp(payload, AUTHORIZED_QR) == 0) {
    grantAccess();
  } else {
    denyAccess();
  }
}

// ==================================================
// SETUP
// ==================================================

void setup() {
  Serial.begin(115200);
  delay(1500);

  Serial.println();
  Serial.println("==================================");
  Serial.println("Ostium Core QR Access Control");
  Serial.println("==================================");

  Serial.printf(
    "PSRAM: %s\n",
    psramFound() ? "detected" : "NOT detected"
  );

  // ----------------------------------------------
  // 1. Initialize camera FIRST.
  // ----------------------------------------------

  Serial.println("Initializing QR camera...");

  reader.cameraConfig.fb_location = CAMERA_FB_IN_PSRAM;
  reader.cameraConfig.grab_mode = CAMERA_GRAB_WHEN_EMPTY;

  QRCodeReaderSetupErr cameraResult = reader.setup();

  if (cameraResult != SETUP_OK) {
    Serial.printf(
      "QR camera setup failed. Error: %d\n",
      (int)cameraResult
    );

    while (true) {
      delay(1000);
    }
  }

  // Keep the orientation from your working camera test.
  sensor_t* sensor = esp_camera_sensor_get();

  if (sensor != nullptr) {
    sensor->set_hmirror(sensor, 1);
    sensor->set_vflip(sensor, 0);
  }

  Serial.println("QR camera initialized.");

  // ----------------------------------------------
  // 2. Attach servo AFTER camera initialization.
  // ----------------------------------------------

  Serial.println("Setting servo PWM frequency.");
  servo.setPeriodHertz(50);

  Serial.println("Attaching servo.");

  int servoAttached = servo.attach(SERVO_PIN, 500, 2400);

  Serial.print("Servo attach result: ");
  Serial.println(servoAttached);

  if (servoAttached == 0) {
    Serial.println("WARNING: Servo attachment failed.");
  } else {
    setServoAngle(0);
  }

  // ----------------------------------------------
  // 3. Construct LCD AFTER servo.attach().
  // ----------------------------------------------

  Serial.println("Creating LCD object.");

  lcd = new LiquidCrystal(
    LCD_RS, LCD_EN, LCD_D4, LCD_D5, LCD_D6, LCD_D7
  );

  lcd->begin(16, 2);
  showMessage("Ostium Core", "Starting...");

  Serial.println("LCD initialized.");

  // ----------------------------------------------
  // 4. Initialize sensors.
  // ----------------------------------------------

  analogReadResolution(12);
  pinMode(SOUND_ANALOG_PIN, INPUT);

  dht.begin();

  delay(2000);

  temperature = dht.readTemperature();
  humidity = dht.readHumidity();
  soundRMS = readSoundRMS();

  updateLCD();

  // ----------------------------------------------
  // 5. Start QR decoding task.
  // ----------------------------------------------

  Serial.println("Starting QR decoder task.");

  reader.beginOnCore(1);

  showMessage("Ready to scan", "Show QR code");

  Serial.println("Ostium Core started.");
  Serial.println("Monitoring sensors and QR codes.");
}

// ==================================================
// MAIN LOOP
// ==================================================

void loop() {
  // QR decoder runs in its own task.
  processQrCodes();

  // Pause sensor readings and display updates during
  // access granted/denied messages and servo movement.
  if (actionInProgress) {
    delay(10);
    return;
  }

  unsigned long currentTime = millis();

  // ----------------------------------------------
  // Temperature and humidity every 2 seconds.
  // ----------------------------------------------

  if (currentTime - lastSensorRead >= SENSOR_INTERVAL_MS) {
    lastSensorRead = currentTime;

    float newTemperature = dht.readTemperature();
    float newHumidity = dht.readHumidity();

    if (!isnan(newTemperature) && !isnan(newHumidity)) {
      temperature = newTemperature;
      humidity = newHumidity;
    } else {
      Serial.println("Warning: DHT11 reading failed.");
    }
  }

  // ----------------------------------------------
  // Sound RMS, LCD, and serial output every 500 ms.
  // ----------------------------------------------

  if (currentTime - lastDisplayUpdate >= DISPLAY_INTERVAL_MS) {
    lastDisplayUpdate = currentTime;

    soundRMS = readSoundRMS();

    updateLCD();

    Serial.print("Temperature: ");
    Serial.print(temperature);
    Serial.print(" C | Humidity: ");
    Serial.print(humidity);
    Serial.print(" % | Sound RMS: ");
    Serial.println(soundRMS, 1);
  }

  delay(1);
}