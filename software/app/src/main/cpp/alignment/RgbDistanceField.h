#ifndef RGB_DISTANCE_FIELD_H
#define RGB_DISTANCE_FIELD_H

#include <opencv2/opencv.hpp>

class RgbDistanceField {
public:
    // Computes Euclidean Distance Transform (CV_32FC1, L2 norm) from visible image
    static cv::Mat compute(const cv::Mat &cam_source,
                           int blur_ksize = 5,
                           float blur_sigma = 1.2f,
                           int canny_low = 40,
                           int canny_high = 120,
                           const cv::Rect &mask_roi = cv::Rect());

    // Computes binary Canny edges (CV_8UC1)
    static cv::Mat computeEdges(const cv::Mat &cam_source,
                                int blur_ksize = 5,
                                float blur_sigma = 1.2f,
                                int canny_low = 40,
                                int canny_high = 120);
};

#endif // RGB_DISTANCE_FIELD_H
