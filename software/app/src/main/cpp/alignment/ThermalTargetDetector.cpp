#include "ThermalTargetDetector.h"
#include <android/log.h>

#define LOGI(...) __android_log_print(ANDROID_LOG_INFO, "ThermalTargetDetector", __VA_ARGS__)
#define LOGW(...) __android_log_print(ANDROID_LOG_WARN, "ThermalTargetDetector", __VA_ARGS__)

ThermalTarget ThermalTargetDetector::detect(const ThermalPreprocess::ProcessedThermal &prep) {
    ThermalTarget target;
    target.valid = false;
    target.status = ALIGN_INTERNAL_ERROR;
    target.cx = 16.0f;
    target.cy = 12.0f;
    target.bbox = cv::Rect(0, 0, 0, 0);
    target.area = 0.0f;
    target.fill_ratio = 0.0f;
    target.confidence = 0.0f;

    // 1. Contrast Verification
    if (prep.low_contrast || prep.binary_mask.empty()) {
        LOGW("detect: Low contrast target");
        target.status = ALIGN_LOW_CONTRAST;
        return target;
    }

    // 2. Target Clipped / Field-of-View Overflow Check
    int total_pixels = prep.binary_mask.rows * prep.binary_mask.cols;
    int fg_pixels = cv::countNonZero(prep.binary_mask);
    target.fill_ratio = (float)fg_pixels / (float)total_pixels;

    int border_touch = 0;
    for (int c = 0; c < prep.binary_mask.cols; ++c) {
        if (prep.binary_mask.at<uchar>(0, c) > 0) border_touch++;
        if (prep.binary_mask.at<uchar>(prep.binary_mask.rows - 1, c) > 0) border_touch++;
    }
    for (int r = 0; r < prep.binary_mask.rows; ++r) {
        if (prep.binary_mask.at<uchar>(r, 0) > 0) border_touch++;
        if (prep.binary_mask.at<uchar>(r, prep.binary_mask.cols - 1) > 0) border_touch++;
    }

    LOGI("detect: fill_ratio=%.2f, border_touch=%d", target.fill_ratio, border_touch);

    if (target.fill_ratio > 0.85f || (target.fill_ratio > 0.78f && border_touch > 14)) {
        LOGW("detect: Target clipped or overflowing FOV (fill=%.2f, border=%d)",
             target.fill_ratio, border_touch);
        target.status = ALIGN_TARGET_CLIPPED;
        return target;
    }

    // 3. Contour Extraction
    std::vector<std::vector<cv::Point>> contours;
    cv::findContours(prep.binary_mask, contours, cv::RETR_EXTERNAL, cv::CHAIN_APPROX_NONE);
    if (contours.empty()) {
        LOGW("detect: No external contours found");
        target.status = ALIGN_NO_TARGET;
        return target;
    }

    // Select the largest thermal target contour by area
    size_t best_idx = 0;
    double max_area = 0.0;
    for (size_t i = 0; i < contours.size(); ++i) {
        double a = cv::contourArea(contours[i]);
        if (a > max_area) {
            max_area = a;
            best_idx = i;
        }
    }

    const std::vector<cv::Point>& best_contour = contours[best_idx];
    if (best_contour.size() < 10 || max_area < 15.0) {
        LOGW("detect: Target too small (pts=%zu, area=%.1f)", best_contour.size(), max_area);
        target.status = ALIGN_NO_TARGET;
        return target;
    }

    // 4. Centroid and Bounding Box calculation via spatial moments
    cv::Moments m = cv::moments(best_contour);
    if (m.m00 > 1e-4) {
        target.cx = (float)(m.m10 / m.m00);
        target.cy = (float)(m.m01 / m.m00);
    } else {
        cv::Rect r = cv::boundingRect(best_contour);
        target.cx = (float)r.x + (float)r.width / 2.0f;
        target.cy = (float)r.y + (float)r.height / 2.0f;
    }

    target.bbox = cv::boundingRect(best_contour);
    target.area = (float)max_area;
    target.confidence = std::min(1.0f, (float)(max_area / (float)total_pixels * 2.5f));
    target.contour = best_contour;
    target.valid = true;
    target.status = ALIGN_OK;

    LOGI("detect SUCCESS: center=(%.1f, %.1f), bbox=[%d, %d, %d, %d], area=%.1f",
         target.cx, target.cy, target.bbox.x, target.bbox.y, target.bbox.width, target.bbox.height, target.area);

    return target;
}
