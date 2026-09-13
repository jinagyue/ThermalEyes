#ifndef THERMAL_GUIDED_ALIGNER_H
#define THERMAL_GUIDED_ALIGNER_H

#include <opencv2/opencv.hpp>
#include "CalibrationModel.h"
#include "ThermalTargetDetector.h"

class ThermalGuidedAligner {
public:
    static AlignmentResult align(const uint8_t *cam_y, const uint8_t *therm_data,
                                int cam_width, int cam_height,
                                int therm_width, int therm_height);
};

#endif // THERMAL_GUIDED_ALIGNER_H
