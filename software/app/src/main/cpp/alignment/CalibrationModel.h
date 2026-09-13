#ifndef CALIBRATION_MODEL_H
#define CALIBRATION_MODEL_H

#include <opencv2/opencv.hpp>

enum AlignStatus {
    ALIGN_OK = 0,
    ALIGN_LOW_CONTRAST = 1,
    ALIGN_TARGET_CLIPPED = 2,
    ALIGN_NO_TARGET = 3,
    ALIGN_AMBIGUOUS = 4,
    ALIGN_RANGE_LIMITED = 5,
    ALIGN_INTERNAL_ERROR = 6
};

enum AlignMode {
    ALIGN_MODE_TGA = 0,    // Engineering Mode (Thermal Guided Alignment)
    ALIGN_MODE_PCTVA = 1   // Research Mode (Physics-Constrained Thermal-Visible Alignment)
};

struct AlignmentResult {
    bool success;
    int status;
    float dx;
    float dy;
    float scale;
    float distance;
    float confidence;
    float psr;
};

namespace CalibrationModel {
    // Parallax model: dx(Z) = ax / Z + bx
    // Initial empirical parameters (Subject to multi-distance calibration experiment re-fitting)
    constexpr float DEFAULT_AX = 22.1f;     // px * m (evaluated at 640x480)
    constexpr float DEFAULT_BX = 1.5f;      // px (evaluated at 640x480)
    constexpr float DEFAULT_BASE_Y = -10.0f; // px (evaluated at 640x480)
    constexpr float MIN_Z = 0.18f;          // meters
    constexpr float MAX_Z = 2.50f;          // meters

    float predictDx(float Z, float ax = DEFAULT_AX, float bx = DEFAULT_BX);
    float estimateDistance(float dx, float ax = DEFAULT_AX, float bx = DEFAULT_BX);
    bool isPhysicallyFeasible(float dx, float dy);
}

#endif // CALIBRATION_MODEL_H
