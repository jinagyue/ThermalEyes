#include "ThermalContourExtractor.h"
#include <cmath>
#include <algorithm>

ThermalContourExtractor::ThermalContourExtractor(int num_samples)
    : m_num_samples(num_samples) {}

ThermalContour ThermalContourExtractor::extractFromRaw(const uint8_t *therm_data,
                                                      int therm_w,
                                                      int therm_h,
                                                      int target_w,
                                                      int target_h,
                                                      bool flip_horizontal) {
    cv::Mat im_therm(therm_h, therm_w, CV_8UC1, const_cast<uint8_t *>(therm_data));
    return extract(im_therm, target_w, target_h, flip_horizontal);
}

ThermalContour ThermalContourExtractor::extract(const cv::Mat &therm_source,
                                                int target_w,
                                                int target_h,
                                                bool flip_horizontal) {
    ThermalContour out;
    out.valid = false;
    out.status = INTERNAL_ERROR;
    out.centroid = cv::Point2f(target_w / 2.0f, target_h / 2.0f);
    out.bbox = cv::Rect(0, 0, 0, 0);
    out.areaRatio = 0.0f;
    out.contrast = 0.0f;

    if (therm_source.empty()) {
        out.status = INTERNAL_ERROR;
        return out;
    }

    int therm_w = therm_source.cols;
    int therm_h = therm_source.rows;
    float scale_x = (float)target_w / (float)therm_w;
    float scale_y = (float)target_h / (float)therm_h;

    // 1. Hardware sensor normalization (MLX90640 horizontal mirror flip)
    cv::Mat therm;
    if (flip_horizontal) {
        cv::flip(therm_source, therm, 1);
    } else {
        therm = therm_source.clone();
    }

    // 2. Dynamic range and contrast check
    double min_val = 0.0, max_val = 0.0;
    cv::minMaxLoc(therm, &min_val, &max_val);
    out.contrast = (float)(max_val - min_val);

    cv::Mat therm_norm;
    if (therm.type() == CV_32FC1) {
        if (out.contrast < 2.0f) {
            out.status = LOW_THERMAL_CONTRAST;
            return out;
        }
        therm_norm = cv::Mat(therm_h, therm_w, CV_8UC1);
        for (int r = 0; r < therm_h; ++r) {
            const float *row_in = therm.ptr<float>(r);
            uint8_t *row_out = therm_norm.ptr<uint8_t>(r);
            for (int c = 0; c < therm_w; ++c) {
                float val = (row_in[c] - (float)min_val) / (out.contrast + 1e-6f) * 255.0f;
                row_out[c] = (uint8_t)std::max(0.0f, std::min(255.0f, val));
            }
        }
    } else if (therm.type() == CV_8UC1) {
        if (out.contrast < 3.0f) {
            out.status = LOW_THERMAL_CONTRAST;
            return out;
        }
        therm_norm = cv::Mat(therm_h, therm_w, CV_8UC1);
        for (int r = 0; r < therm_h; ++r) {
            const uint8_t *row_in = therm.ptr<uint8_t>(r);
            uint8_t *row_out = therm_norm.ptr<uint8_t>(r);
            for (int c = 0; c < therm_w; ++c) {
                float val = ((float)row_in[c] - (float)min_val) / (out.contrast + 1e-6f) * 255.0f;
                row_out[c] = (uint8_t)std::max(0.0f, std::min(255.0f, val));
            }
        }
    } else {
        out.status = INTERNAL_ERROR;
        return out;
    }

    // 3. Smoothing on native 32x24 grid (median filter to suppress detector noise)
    cv::Mat smooth;
    cv::medianBlur(therm_norm, smooth, 3);

    // 4. Adaptive segmentation with Otsu + lower extremity floor
    cv::Mat dummy;
    double otsu_val = cv::threshold(smooth, dummy, 0, 255, cv::THRESH_BINARY | cv::THRESH_OTSU);
    int floor_thresh = std::max(15, (int)(otsu_val * 0.70));

    cv::Mat binary_mask;
    cv::threshold(smooth, binary_mask, floor_thresh, 255, cv::THRESH_BINARY);

    // 5. Morphological cleanup (3x3 ellipse element)
    cv::Mat kernel = cv::getStructuringElement(cv::MORPH_ELLIPSE, cv::Size(3, 3));
    cv::morphologyEx(binary_mask, binary_mask, cv::MORPH_CLOSE, kernel);
    cv::morphologyEx(binary_mask, binary_mask, cv::MORPH_OPEN, kernel);

    // 6. Target FOV overflow check
    int total_pixels = therm_w * therm_h;
    int fg_pixels = cv::countNonZero(binary_mask);
    out.areaRatio = (float)fg_pixels / (float)total_pixels;

    if (out.areaRatio > 0.85f) {
        out.status = INVALID_THERMAL_CONTOUR;
        return out;
    }

    // 7. Contour extraction on native grid
    std::vector<std::vector<cv::Point>> contours;
    cv::findContours(binary_mask, contours, cv::RETR_EXTERNAL, cv::CHAIN_APPROX_NONE);
    if (contours.empty()) {
        out.status = INVALID_THERMAL_CONTOUR;
        return out;
    }

    // Find largest contour by area
    size_t best_idx = 0;
    double max_area = 0.0;
    for (size_t i = 0; i < contours.size(); ++i) {
        double a = cv::contourArea(contours[i]);
        if (a > max_area) {
            max_area = a;
            best_idx = i;
        }
    }

    const std::vector<cv::Point> &best_cnt = contours[best_idx];
    if (max_area < 10.0 || best_cnt.size() < 6) {
        out.status = INVALID_THERMAL_CONTOUR;
        return out;
    }

    // 8. Transform contour points to target (640x480) coordinate system
    std::vector<cv::Point2f> pts_scaled;
    pts_scaled.reserve(best_cnt.size());
    for (const auto &p : best_cnt) {
        float px = (float)p.x * scale_x + (scale_x / 2.0f);
        float py = (float)p.y * scale_y + (scale_y / 2.0f);
        pts_scaled.emplace_back(px, py);
    }

    // Uniform perimeter resampling
    out.points = resampleContour(pts_scaled, m_num_samples);

    // Centroid calculation via moments on native contour
    cv::Moments M = cv::moments(best_cnt);
    if (M.m00 > 1e-4) {
        float cx_therm = (float)(M.m10 / M.m00);
        float cy_therm = (float)(M.m01 / M.m00);
        out.centroid.x = cx_therm * scale_x + (scale_x / 2.0f);
        out.centroid.y = cy_therm * scale_y + (scale_y / 2.0f);
    } else {
        float sum_x = 0.0f, sum_y = 0.0f;
        for (const auto &pt : out.points) {
            sum_x += pt.x;
            sum_y += pt.y;
        }
        out.centroid.x = sum_x / (float)out.points.size();
        out.centroid.y = sum_y / (float)out.points.size();
    }

    // Bounding box in target space
    cv::Rect native_bbox = cv::boundingRect(best_cnt);
    out.bbox = cv::Rect(
        (int)(native_bbox.x * scale_x),
        (int)(native_bbox.y * scale_y),
        (int)(native_bbox.width * scale_x),
        (int)(native_bbox.height * scale_y)
    );

    out.valid = true;
    out.status = ALIGN_OK;
    return out;
}

std::vector<cv::Point2f> ThermalContourExtractor::resampleContour(const std::vector<cv::Point2f> &pts, int target_count) {
    if (pts.size() < 3 || target_count < 3) {
        return pts;
    }

    // Close polygon loop
    std::vector<cv::Point2f> pts_closed = pts;
    pts_closed.push_back(pts.front());

    size_t n = pts_closed.size();
    std::vector<float> cum_lens(n, 0.0f);
    float total_len = 0.0f;

    for (size_t i = 1; i < n; ++i) {
        float dx = pts_closed[i].x - pts_closed[i - 1].x;
        float dy = pts_closed[i].y - pts_closed[i - 1].y;
        total_len += std::sqrt(dx * dx + dy * dy);
        cum_lens[i] = total_len;
    }

    if (total_len < 1e-3f) {
        return pts;
    }

    std::vector<cv::Point2f> resampled;
    resampled.reserve(target_count);

    float step = total_len / (float)target_count;
    size_t cur_seg = 0;

    for (int i = 0; i < target_count; ++i) {
        float target_d = (float)i * step;

        while (cur_seg + 1 < n && cum_lens[cur_seg + 1] < target_d) {
            cur_seg++;
        }

        if (cur_seg + 1 >= n) {
            resampled.push_back(pts_closed.back());
            continue;
        }

        float seg_start_d = cum_lens[cur_seg];
        float seg_end_d = cum_lens[cur_seg + 1];
        float seg_len = seg_end_d - seg_start_d;

        float t = (seg_len > 1e-6f) ? ((target_d - seg_start_d) / seg_len) : 0.0f;
        t = std::max(0.0f, std::min(1.0f, t));

        float rx = pts_closed[cur_seg].x + t * (pts_closed[cur_seg + 1].x - pts_closed[cur_seg].x);
        float ry = pts_closed[cur_seg].y + t * (pts_closed[cur_seg + 1].y - pts_closed[cur_seg].y);

        resampled.emplace_back(rx, ry);
    }

    return resampled;
}
