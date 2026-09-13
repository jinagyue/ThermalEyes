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

    // --- STEP 2: Scale Thermal Contour to Visible Coordinates ---
    float scale_x = (float)cam_width / (float)therm_width;   // e.g. 640 / 32 = 20.0f
    float scale_y = (float)cam_height / (float)therm_height; // e.g. 480 / 24 = 20.0f

    size_t cnt_size = target.contour.size();
    if (cnt_size < 4) {
        LOGW("TGA: Target contour too few points (%zu)", cnt_size);
        res.status = ALIGN_NO_TARGET;
        return res;
    }

    // Uniformly sample ~40 contour points along perimeter
    std::vector<cv::Point2f> therm_pts;
    int step = std::max(1, (int)cnt_size / 40);
    float min_tx = 1e9f, max_tx = -1e9f, min_ty = 1e9f, max_ty = -1e9f;

    for (size_t i = 0; i < cnt_size; i += step) {
        float px = (float)target.contour[i].x * scale_x;
        float py = (float)target.contour[i].y * scale_y;
        therm_pts.emplace_back(px, py);
        min_tx = std::min(min_tx, px);
        max_tx = std::max(max_tx, px);
        min_ty = std::min(min_ty, py);
        max_ty = std::max(max_ty, py);
    }

    // --- STEP 3: Define Visible Search ROI ---
    // Physical disparity constraints: dx in [10, 105], dy in [-20, 25]
    int roi_x1 = std::max(0, (int)std::floor(min_tx + 10.0f - 16.0f));
    int roi_y1 = std::max(0, (int)std::floor(min_ty - 20.0f - 16.0f));
    int roi_x2 = std::min(cam_width, (int)std::ceil(max_tx + 105.0f + 16.0f));
    int roi_y2 = std::min(cam_height, (int)std::ceil(max_ty + 25.0f + 16.0f));

    int roi_w = roi_x2 - roi_x1;
    int roi_h = roi_y2 - roi_y1;

    if (roi_w < 30 || roi_h < 30) {
        LOGW("TGA: Search ROI too small [%d, %d]", roi_w, roi_h);
        res.status = ALIGN_AMBIGUOUS;
        return res;
    }

    cv::Rect search_roi(roi_x1, roi_y1, roi_w, roi_h);
    cv::Mat im_cam(cam_height, cam_width, CV_8UC1, const_cast<uint8_t *>(cam_y));
    cv::Mat cam_sub = im_cam(search_roi);

    // --- STEP 4: Local Distance Transform on Visible ROI Edges ---
    cv::Mat cam_blur, cam_edges, dist_roi;
    cv::GaussianBlur(cam_sub, cam_blur, cv::Size(3, 3), 1.0);
    cv::Canny(cam_blur, cam_edges, 30, 90);

    // Euclidean distance transform (0 at edges, positive elsewhere)
    cv::distanceTransform(~cam_edges, dist_roi, cv::DIST_L2, 3);

    // --- STEP 5: Chamfer Distance Field Grid Search ---
    const float sigma = 3.5f;
    const float two_sig_sq = 2.0f * sigma * sigma;

    float best_score = -1e9f;
    float best_dx = 35.0f;
    float best_dy = CalibrationModel::DEFAULT_BASE_Y;

    std::vector<float> all_scores;
    all_scores.reserve(50 * 25);

    // Coarse search: step = 2px
    for (int idx = 12; idx <= 102; idx += 2) {
        float dx = (float)idx;
        for (int idy = -18; idy <= 22; idy += 2) {
            float dy = (float)idy;

            float point_sum = 0.0f;
            int in_bounds = 0;

            for (const auto& pt : therm_pts) {
                float rx = pt.x + dx - (float)roi_x1;
                float ry = pt.y + dy - (float)roi_y1;
                int ix = (int)std::round(rx);
                int iy = (int)std::round(ry);

                if (ix >= 0 && ix < roi_w && iy >= 0 && iy < roi_h) {
                    float d = dist_roi.at<float>(iy, ix);
                    point_sum += (d < 12.0f) ? std::exp(-(d * d) / two_sig_sq) : 0.0f;
                    in_bounds++;
                }
            }

            if (in_bounds < (int)(therm_pts.size() * 0.45f)) {
                continue;
            }

            float raw_score = point_sum / (float)therm_pts.size();
            // Small regularizer to gently penalize large vertical drift from DEFAULT_BASE_Y
            float reg_score = raw_score - 0.0003f * (dy - CalibrationModel::DEFAULT_BASE_Y) * (dy - CalibrationModel::DEFAULT_BASE_Y);
            all_scores.push_back(raw_score);

            if (reg_score > best_score) {
                best_score = reg_score;
                best_dx = dx;
                best_dy = dy;
            }
        }
    }

    if (all_scores.empty() || best_score < 0.08f) {
        LOGW("TGA: Low overall match score (%.3f)", best_score);
        res.status = ALIGN_AMBIGUOUS;
        return res;
    }

    // Fine local search: step = 1px around best_dx, best_dy
    float fine_dx = best_dx;
    float fine_dy = best_dy;
    float fine_best_score = best_score;

    for (float fdx = best_dx - 2.0f; fdx <= best_dx + 2.0f; fdx += 1.0f) {
        for (float fdy = best_dy - 2.0f; fdy <= best_dy + 2.0f; fdy += 1.0f) {
            float point_sum = 0.0f;
            int in_bounds = 0;

            for (const auto& pt : therm_pts) {
                float rx = pt.x + fdx - (float)roi_x1;
                float ry = pt.y + fdy - (float)roi_y1;
                int ix = (int)std::round(rx);
                int iy = (int)std::round(ry);

                if (ix >= 0 && ix < roi_w && iy >= 0 && iy < roi_h) {
                    float d = dist_roi.at<float>(iy, ix);
                    point_sum += (d < 12.0f) ? std::exp(-(d * d) / two_sig_sq) : 0.0f;
                    in_bounds++;
                }
            }

            if (in_bounds >= (int)(therm_pts.size() * 0.45f)) {
                float raw_sc = point_sum / (float)therm_pts.size();
                float reg_sc = raw_sc - 0.0003f * (fdy - CalibrationModel::DEFAULT_BASE_Y) * (fdy - CalibrationModel::DEFAULT_BASE_Y);
                if (reg_sc > fine_best_score) {
                    fine_best_score = reg_sc;
                    fine_dx = fdx;
                    fine_dy = fdy;
                }
            }
        }
    }

    // --- STEP 6: Confidence (PSR) & Validation ---
    float sum_sc = 0.0f;
    for (float s : all_scores) sum_sc += s;
    float mean_sc = sum_sc / (float)all_scores.size();

    float var_sc = 0.0f;
    for (float s : all_scores) var_sc += (s - mean_sc) * (s - mean_sc);
    float std_sc = std::sqrt(var_sc / (float)all_scores.size());
    float psr = (std_sc > 1e-4f) ? (fine_best_score - mean_sc) / std_sc : 3.0f;

    LOGI("TGA EVAL: dx=%.1f, dy=%.1f, score=%.3f, psr=%.2f, mean=%.3f",
         fine_dx, fine_dy, fine_best_score, psr, mean_sc);

    // Boundary check
    if (fine_dx <= 11.0f || fine_dx >= 104.0f || fine_dy <= -19.0f || fine_dy >= 24.0f) {
        LOGW("TGA: Boundary hit (dx=%.1f, dy=%.1f) -> ALIGN_RANGE_LIMITED", fine_dx, fine_dy);
        res.status = ALIGN_RANGE_LIMITED;
        return res;
    }

    // Reject ambiguous matches only when both score and PSR are excessively poor
    if (fine_best_score < 0.10f && psr < 1.8f) {
        LOGW("TGA: Ambiguous match rejected (score=%.3f, psr=%.2f)", fine_best_score, psr);
        res.status = ALIGN_AMBIGUOUS;
        return res;
    }

    float Z = CalibrationModel::estimateDistance(fine_dx);

    res.success = true;
    res.status = ALIGN_OK;
    res.dx = fine_dx;
    res.dy = fine_dy;
    res.scale = 1.0f;
    res.distance = Z;
    res.confidence = std::max(0.40f, std::min(0.98f, fine_best_score * 1.5f));
    res.psr = psr;

    LOGI("TGA SUCCESS: Z=%.2fm, dx=%.1f, dy=%.1f, conf=%.2f, psr=%.2f",
         Z, fine_dx, fine_dy, res.confidence, psr);
    return res;
}
