#ifndef THERMAL_PREPROCESS_H
#define THERMAL_PREPROCESS_H

#include <opencv2/opencv.hpp>
#include "CalibrationModel.h"

class ThermalPreprocess {
public:
    struct ProcessedThermal {
        cv::Mat raw_flipped;     // 32x24 uint8 horizontally flipped
        cv::Mat smooth;          // 32x24 smoothed
        cv::Mat binary_mask;     // 32x24 Otsu thresholded binary mask
        double min_val;
        double max_val;
        double contrast;
        bool low_contrast;
    };

    static ProcessedThermal process(const uint8_t *therm_data, int width, int height);
};

#endif // THERMAL_PREPROCESS_H
