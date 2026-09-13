#ifndef THERMAL_TARGET_DETECTOR_H
#define THERMAL_TARGET_DETECTOR_H

#include <opencv2/opencv.hpp>
#include <vector>
#include "CalibrationModel.h"
#include "ThermalPreprocess.h"

struct ThermalTarget {
    bool valid;
    AlignStatus status;
    float cx;           // Centroid X (0 to width-1 in 32x24)
    float cy;           // Centroid Y (0 to height-1 in 32x24)
    cv::Rect bbox;      // Bounding box in 32x24
    float area;
    float fill_ratio;
    float confidence;
    std::vector<cv::Point> contour;
};

class ThermalTargetDetector {
public:
    static ThermalTarget detect(const ThermalPreprocess::ProcessedThermal &prep);
};

#endif // THERMAL_TARGET_DETECTOR_H
