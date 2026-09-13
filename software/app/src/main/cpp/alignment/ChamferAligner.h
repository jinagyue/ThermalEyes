#ifndef CHAMFER_ALIGNER_H
#define CHAMFER_ALIGNER_H

#include <opencv2/opencv.hpp>
#include "AlignmentTypes.h"

class ChamferAligner {
public:
    // 1. Golden Reference Implementation (Strictly equivalent to Python chamfer_alignment.py)
    static AlignmentResult alignReference(const ThermalContour &contour,
                                          const cv::Mat &dist_map,
                                          const TccaConfig &config = TccaConfig());

    // 2. Engineered Mobile Implementation (Multi-scale hierarchical coarse-to-fine for Android NDK)
    static AlignmentResult alignFast(const ThermalContour &contour,
                                     const cv::Mat &dist_map,
                                     const TccaConfig &config = TccaConfig());

    // 3. Unified Dispatcher
    static AlignmentResult align(const ThermalContour &contour,
                                 const cv::Mat &dist_map,
                                 AlignAlgorithm algo = TCCA_FAST,
                                 const TccaConfig &config = TccaConfig());
};

#endif // CHAMFER_ALIGNER_H
