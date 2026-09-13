#ifndef PHYSICS_ALIGNMENT_H
#define PHYSICS_ALIGNMENT_H

#include <opencv2/opencv.hpp>
#include "CalibrationModel.h"

class PhysicsAlignment {
public:
    static AlignmentResult align(const uint8_t *cam_y, const uint8_t *therm_data,
                                int cam_width, int cam_height,
                                int therm_width, int therm_height);
};

#endif // PHYSICS_ALIGNMENT_H
