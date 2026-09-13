#include "CalibrationModel.h"
#include <algorithm>
#include <cmath>

namespace CalibrationModel {

float predictDx(float Z, float ax, float bx) {
    if (Z <= 0.05f) Z = 0.05f;
    return (ax / Z) + bx;
}

float estimateDistance(float dx, float ax, float bx) {
    float net_dx = dx - bx;
    if (net_dx <= 0.5f) return MAX_Z;
    float Z = ax / net_dx;
    return std::max(MIN_Z, std::min(MAX_Z, Z));
}

bool isPhysicallyFeasible(float dx, float dy) {
    // Horizontal parallax dx: 10px (~2.5m) to 105px (~0.2m)
    // Vertical offset dy: [-20px, +25px]
    if (dx < 10.0f || dx > 105.0f) return false;
    if (dy < -20.0f || dy > 25.0f) return false;
    return true;
}

} // namespace CalibrationModel
