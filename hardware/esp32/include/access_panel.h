#pragma once

#include <Arduino.h>
#include <stddef.h>
#include <math.h>

namespace board {

enum class AccessResult {
    None,
    Granted,
    Denied
};

struct PanelTelemetry {
    float temperature = NAN;
    float humidity = NAN;
    float soundRMS = 0.0f;
};

class AccessPanel {
public:
    bool begin();

    void update(uint32_t nowMs);

    AccessResult processQr();

    const PanelTelemetry& telemetry() const;

    void showMessage(const char* line1, const char* line2);
};

} // namespace board