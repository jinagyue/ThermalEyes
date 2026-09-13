#include "ThermalGuidedAligner.h"
#include <android/log.h>
#include <cmath>
#include <algorithm>

#define LOGI(...) __android_log_print(ANDROID_LOG_INFO, "TGA", __VA_ARGS__)
#define LOGW(...) __android_log_print(ANDROID_LOG_WARN, "TGA", __VA_ARGS__)

AlignmentResult ThermalGuidedAligner::align(const uint8_t *cam_y, const uint8_t *therm_data,
                                           int cam_width, int cam_height,
                                           int therm_width, int therm_height) {
    AlignmentResult res = { false, ALIGN_INTERNAL_ERROR, 25.0f, -5.0f, 1.0f, 0.8f, 0.0f, 0.0f };

    // --- STEP 1: Thermal Preprocessing & Target Detection ---
    ThermalPreprocess::ProcessedThermal prep = ThermalPreprocess::process(therm_data, therm_width, therm_height);
    ThermalTarget target = ThermalTargetDetector::detect(prep);

    if (!target.valid || target.status != ALIGN_OK) {
        LOGW("TGA: Thermal target rejected with status %d", target.status);
        res.status = target.status;
        return res;
    }

    // --- STEP 2: Predict Projected Visible Coordinates ---
    float scale_x = (float)cam_width / (float)therm_width;   // e.g. 640 / 32 = 20.0f
    float scale_y = (float)cam_height / (float)therm_height; // e.g. 480 / 24 = 20.0f

    float x0 = target.cx * scale_x;
    float y0 = target.cy * scale_y;

    // Nominal physical parallax displacement: dx ~ 40px, dy ~ -5px
    float nominal_dx = 40.0f;
    float nominal_dy = CalibrationModel::DEFAULT_BASE_Y;

    float pred_xv = x0 + nominal_dx;
    float pred_yv = y0 + nominal_dy;

    // Target dimensions in visible scale
    float exp_w = (float)target.bbox.width * scale_x;
    float exp_h = (float)target.bbox.height * scale_y;
    float exp_area = exp_w * exp_h;

    // --- STEP 3: Define Visible Search ROI ---
    // Expanded ROI around the predicted center to capture range dx in [10, 105], dy in [-20, 25]
    int roi_w = std::min(cam_width, (int)std::round(exp_w + 160.0f));
    int roi_h = std::min(cam_height, (int)std::round(exp_h + 120.0f));

    int roi_x = (int)std::round(pred_xv - (float)roi_w / 2.0f);
    int roi_y = (int)std::round(pred_yv - (float)roi_h / 2.0f);

    // Clamp ROI strictly within visible bounds
    roi_x = std::max(0, std::min(cam_width - roi_w, roi_x));
    roi_y = std::max(0, std::min(cam_height - roi_h, roi_y));

    if (roi_w <= 20 || roi_h <= 20) {
        LOGW("TGA: Invalid ROI dimensions [%d, %d]", roi_w, roi_h);
        res.status = ALIGN_AMBIGUOUS;
        return res;
    }

    cv::Rect search_roi(roi_x, roi_y, roi_w, roi_h);
    cv::Mat im_cam(cam_height, cam_width, CV_8UC1, const_cast<uint8_t *>(cam_y));
    cv::Mat cam_sub = im_cam(search_roi);

    // --- STEP 4: Visible Contour Detection in ROI ---
    cv::Mat cam_blur, cam_edges;
    cv::GaussianBlur(cam_sub, cam_blur, cv::Size(3, 3), 1.0);
    cv::Canny(cam_blur, cam_edges, 35, 95);

    std::vector<std::vector<cv::Point>> v_contours;
    cv::findContours(cam_edges, v_contours, cv::RETR_EXTERNAL, cv::CHAIN_APPROX_NONE);

    if (v_contours.empty()) {
        LOGW("TGA: No visible contours in ROI");
        res.status = ALIGN_AMBIGUOUS;
        return res;
    }

    // Match candidate contour closest in size, shape, and proximity
    float best_match_score = -1e9f;
    float best_xv = pred_xv;
    float best_yv = pred_yv;
    bool found_candidate = false;

    float exp_aspect = exp_w / std::max(1.0f, exp_h);

    for (const auto& c : v_contours) {
        if (c.size() < 12) continue;
        double a = cv::contourArea(c);
        cv::Rect r = cv::boundingRect(c);
        if (r.width < 10 || r.height < 10) continue;

        float c_aspect = (float)r.width / (float)r.height;
        float aspect_diff = std::abs(c_aspect - exp_aspect);

        // Centroid of contour in full visible image coordinates
        cv::Moments m = cv::moments(c);
        float cx_local = (m.m00 > 1e-4) ? (float)(m.m10 / m.m00) : ((float)r.x + (float)r.width / 2.0f);
        float cy_local = (m.m00 > 1e-4) ? (float)(m.m01 / m.m00) : ((float)r.y + (float)r.height / 2.0f);

        float cand_xv = (float)roi_x + cx_local;
        float cand_yv = (float)roi_y + cy_local;

        float cand_dx = cand_xv - x0;
        float cand_dy = cand_yv - y0;

        // Strict physical bounds pruning
        if (!CalibrationModel::isPhysicallyFeasible(cand_dx, cand_dy)) {
            continue;
        }

        // Feature scoring: size similarity + shape similarity - deviation from epipolar prediction
        float area_ratio = (float)a / std::max(1.0f, exp_area);
        if (area_ratio > 4.0f || area_ratio < 0.15f) continue;

        float size_penalty = std::abs(std::log(std::max(0.1f, area_ratio)));
        float dist_penalty = (std::abs(cand_dy - nominal_dy) / 20.0f) + (std::abs(cand_dx - nominal_dx) / 60.0f);
        float score = 2.0f - (size_penalty * 0.5f + aspect_diff * 0.5f + dist_penalty * 0.4f);

        if (score > best_match_score) {
            best_match_score = score;
            best_xv = cand_xv;
            best_yv = cand_yv;
            found_candidate = true;
        }
    }

    if (!found_candidate) {
        LOGW("TGA: No physically feasible visible contour matched");
        res.status = ALIGN_AMBIGUOUS;
        return res;
    }

    // --- STEP 5: Disparity Calculation & Boundary Checks ---
    float final_dx = best_xv - x0;
    float final_dy = best_yv - y0;

    LOGI("TGA: Computed (dx=%.1f, dy=%.1f), best_score=%.2f", final_dx, final_dy, best_match_score);

    if (final_dx <= 11.0f || final_dx >= 104.0f || final_dy <= -19.0f || final_dy >= 24.0f) {
        LOGW("TGA: Result at search boundary (dx=%.1f, dy=%.1f) -> ALIGN_RANGE_LIMITED", final_dx, final_dy);
        res.status = ALIGN_RANGE_LIMITED;
        return res;
    }

    float Z = CalibrationModel::estimateDistance(final_dx);

    res.success = true;
    res.status = ALIGN_OK;
    res.dx = final_dx;
    res.dy = final_dy;
    res.scale = 1.0f;
    res.distance = Z;
    res.confidence = std::max(0.40f, std::min(0.95f, 0.5f + best_match_score * 0.25f));
    res.psr = 6.0f + best_match_score * 2.0f;

    LOGI("TGA SUCCESS: Z=%.2fm, dx=%.1f, dy=%.1f, conf=%.2f", Z, final_dx, final_dy, res.confidence);
    return res;
}
