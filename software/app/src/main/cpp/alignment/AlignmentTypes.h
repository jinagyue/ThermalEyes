#ifndef ALIGNMENT_TYPES_H
#define ALIGNMENT_TYPES_H

#include <vector>
#include <opencv2/core.hpp>

enum AlignStatus {
    ALIGN_OK = 0,
    LOW_THERMAL_CONTRAST = 1,
    INVALID_THERMAL_CONTOUR = 2,
    INSUFFICIENT_RGB_EDGES = 3,
    AMBIGUOUS_RESULT = 4,
    OUT_OF_RANGE = 5,
    INTERNAL_ERROR = 6
};

enum AlignAlgorithm {
    TCCA_FAST = 0,
    TCCA_PHYS = 1
};

struct ThermalContour {
    std::vector<cv::Point2f> points; // 640x480 coordinate space
    cv::Rect bbox;
    cv::Point2f centroid;
    float areaRatio;
    float contrast;
    bool valid;
    AlignStatus status;
};

struct AlignmentResult {
    bool success;
    float dx;
    float dy;
    float scale;
    float score;      // Mean Chamfer distance (px)
    float confidence; // [0.0, 1.0]
    float inlierRatio;
    int status;
    float runtimeMs;
};

struct TccaConfig {
    float scale_min = 0.70f;
    float scale_max = 1.40f;
    float scale_step_coarse = 0.05f;
    float scale_step_fine = 0.02f;

    float dx_min = -150.0f;
    float dx_max = 150.0f;
    float dx_step_coarse = 4.0f;
    float dx_step_fine = 1.0f;

    float dy_min = -80.0f;
    float dy_max = 80.0f;
    float dy_step_coarse = 4.0f;
    float dy_step_fine = 1.0f;

    float out_of_bounds_penalty = 60.0f;
    int num_contour_points = 45;

    // Physics constraint weights (TCCA-Phys)
    float lambda_phys = 0.0f;
    float expected_dx = 0.0f;
    float expected_dy = -10.0f;
};

#endif // ALIGNMENT_TYPES_H
