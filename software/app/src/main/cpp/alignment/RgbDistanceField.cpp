#include "RgbDistanceField.h"

cv::Mat RgbDistanceField::computeEdges(const cv::Mat &cam_source,
                                       int blur_ksize,
                                       float blur_sigma,
                                       int canny_low,
                                       int canny_high) {
    if (cam_source.empty()) {
        return cv::Mat();
    }

    cv::Mat gray;
    if (cam_source.channels() == 3) {
        cv::cvtColor(cam_source, gray, cv::COLOR_BGR2GRAY);
    } else if (cam_source.channels() == 4) {
        cv::cvtColor(cam_source, gray, cv::COLOR_BGRA2GRAY);
    } else {
        gray = cam_source;
    }

    cv::Mat blurred;
    cv::GaussianBlur(gray, blurred, cv::Size(blur_ksize, blur_ksize), blur_sigma);

    cv::Mat edges;
    cv::Canny(blurred, edges, canny_low, canny_high);
    return edges;
}

cv::Mat RgbDistanceField::compute(const cv::Mat &cam_source,
                                  int blur_ksize,
                                  float blur_sigma,
                                  int canny_low,
                                  int canny_high,
                                  const cv::Rect &mask_roi) {
    cv::Mat edges = computeEdges(cam_source, blur_ksize, blur_sigma, canny_low, canny_high);
    if (edges.empty()) {
        return cv::Mat();
    }

    // Optional ROI masking for HUD or borders
    if (mask_roi.width > 0 && mask_roi.height > 0) {
        cv::Rect valid_roi = mask_roi & cv::Rect(0, 0, edges.cols, edges.rows);
        if (valid_roi.width > 0 && valid_roi.height > 0) {
            edges(valid_roi).setTo(0);
        }
    }

    cv::Mat dist_map;
    // Euclidean distance transform (0 at edges, positive elsewhere)
    cv::distanceTransform(~edges, dist_map, cv::DIST_L2, 3);
    return dist_map;
}
