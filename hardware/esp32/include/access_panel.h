#pragma once

#include <Arduino.h>
#include <ostium_core.h>
#include <stddef.h>
#include <math.h>

namespace board {

struct PanelTelemetry {
    float temperature = NAN;
    float humidity = NAN;
    float soundRMS = 0.0f;
};

// The door's camera, LCD, and sensors (SCRUM-68). It shows each QR
// decision that QrAccess makes (docs/firmware/qr-access-v0.md, camera handoff).
class AccessPanel : public ostium::QrFeedback {
public:
    bool begin();

    void update(uint32_t nowMs);

    // Shows one QR decision on the LCD. The LED patterns run only when the
    // panel's LED isn't the lock stand-in on OSTIUM_LOCK_PIN.
    void show(ostium::QrSignal signal) override;

    const PanelTelemetry& telemetry() const;

    void showMessage(const char* line1, const char* line2);
};

// The camera's decoded QR codes, for QrAccess (Q1). next() never blocks: it
// takes the oldest decode the camera task queued, skips failed decodes, and
// copies exactly the decoded bytes. It returns false until the panel's begin()
// has started the camera. Call it only from the loop task.
class CameraScanSource : public ostium::ScanSource {
public:
    bool next(ostium::QrScan* scan) override;
};

} // namespace board
