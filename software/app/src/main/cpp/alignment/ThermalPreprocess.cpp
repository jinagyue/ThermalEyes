#include "ThermalPreprocess.h"
#include <android/log.h>

#define LOGI(...) __android_log_print(ANDROID_LOG_INFO, "ThermalPreprocess", __VA_ARGS__)
#define LOGW(...) __android_log_print(ANDROID_LOG_WARN, "ThermalPreprocess", __VA_ARGS__)

ThermalPreprocess::ProcessedThermal ThermalPreprocess::process(const uint8_t *therm_data, int width, int height) {
    ProcessedThermal out;
    cv::Mat im_therm(height, width, CV_8UC1, const_cast<uint8_t *>(therm_data));

    // Dynamic range check
    cv::minMaxLoc(im_therm, &out.min_val, &out.max_val);
    out.contrast = out.max_val - out.min_val;
    out.low_contrast = (out.contrast < 3.0);

    // Sensor Hardware Normalization: MLX90640 is physically mirrored horizontally
    cv::flip(im_therm, out.raw_flipped, 1);

    // Smoothing on native 32x24 grid to remove detector sensor noise
    cv::medianBlur(out.raw_flipped, out.smooth, 3);

    if (out.low_contrast) {
        out.binary_mask = cv::Mat::zeros(height, width, CV_8UC1);
        LOGW("ThermalPreprocess: Low thermal contrast (%.1f)", out.contrast);
        return out;
    }

    // Otsu thresholding for prominent thermal target segmentation
    cv::threshold(out.smooth, out.binary_mask, 0, 255, cv::THRESH_BINARY | cv::THRESH_OTSU);

    // Morphological close to bridge holes + open to clean isolated speckles
    cv::Mat morph_elem = cv::getStructuringElement(cv::MORPH_ELLIPSE, cv::Size(3, 3));
    cv::morphologyEx(out.binary_mask, out.binary_mask, cv::MORPH_CLOSE, morph_elem);
    cv::morphologyEx(out.binary_mask, out.binary_mask, cv::MORPH_OPEN, morph_elem);

    return out;
}
