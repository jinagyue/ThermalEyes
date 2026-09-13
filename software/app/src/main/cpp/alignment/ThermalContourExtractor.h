#ifndef THERMAL_CONTOUR_EXTRACTOR_H
#define THERMAL_CONTOUR_EXTRACTOR_H

#include <opencv2/opencv.hpp>
#include "AlignmentTypes.h"

class ThermalContourExtractor {
public:
    explicit ThermalContourExtractor(int num_samples = 45);

    // Extracts contour from CV_32FC1 (Celsius) or CV_8UC1 (normalized) 32x24 thermal image
    ThermalContour extract(const cv::Mat &therm_source,
                           int target_w = 640,
                           int target_h = 480,
                           bool flip_horizontal = true);

    // Convenience wrapper for raw uint8 or float buffer
    ThermalContour extractFromRaw(const uint8_t *therm_data,
                                  int therm_w,
                                  int therm_h,
                                  int target_w = 640,
                                  int target_h = 480,
                                  bool flip_horizontal = true);

private:
    int m_num_samples;

    static std::vector<cv::Point2f> resampleContour(const std::vector<cv::Point2f> &pts, int target_count);
};

#endif // THERMAL_CONTOUR_EXTRACTOR_H
