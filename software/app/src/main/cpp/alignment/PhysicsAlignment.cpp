#include "PhysicsAlignment.h"
#include "ThermalPreprocess.h"
#include "ThermalTargetDetector.h"
#include <android/log.h>
#include <cmath>
#include <vector>
#include <algorithm>

#define LOGI(...) __android_log_print(ANDROID_LOG_INFO, "PCTVA", __VA_ARGS__)
#define LOGW(...) __android_log_print(ANDROID_LOG_WARN, "PCTVA", __VA_ARGS__)

static std::vector<cv::Point2f> sampleContourPoints(const std::vector<cv::Point>& raw_pts,
                                                    float sx, float sy, float max_step = 2.0f) {
    std::vector<cv::Point2f> sampled;
    if (raw_pts.size() < 3) return sampled;

    std::vector<cv::Point2f> base;
    base.reserve(raw_pts.size());
    for (const auto& p : raw_pts) {
        base.emplace_back((p.x + 0.5f) * sx, (p.y + 0.5f) * sy);
    }

    for (size_t i = 0; i < base.size(); ++i) {
        cv::Point2f p1 = base[i];
        cv::Point2f p2 = base[(i + 1) % base.size()];
        float seg_len = (float)cv::norm(p2 - p1);
        int steps = std::max(1, (int)std::ceil(seg_len / max_step));
        for (int s = 0; s < steps; ++s) {
            float t = (float)s / (float)steps;
            sampled.push_back(p1 + t * (p2 - p1));
        }
    }
    return sampled;
}

static void buildGaussianDistanceField(const cv::Mat& cam_y, int target_w, int target_h,
                                      cv::Mat& out_dist, cv::Mat& out_field, float sigma = 2.5f) {
    cv::Mat cam_resized;
    cv::resize(cam_y, cam_resized, cv::Size(target_w, target_h), 0, 0, cv::INTER_AREA);

    cv::Mat cam_blur, cam_edge;
    cv::GaussianBlur(cam_resized, cam_blur, cv::Size(3, 3), 1.0);
    cv::Canny(cam_blur, cam_edge, 35, 100);

    cv::distanceTransform(~cam_edge, out_dist, cv::DIST_L2, 3);

    out_field.create(target_h, target_w, CV_32FC1);
    float two_sig_sq = 2.0f * sigma * sigma;
    float max_eval_dist = 3.5f * sigma;

    for (int r = 0; r < target_h; ++r) {
        const float* d_row = out_dist.ptr<float>(r);
        float* f_row = out_field.ptr<float>(r);
        for (int c = 0; c < target_w; ++c) {
            float d = d_row[c];
            f_row[c] = (d < max_eval_dist) ? expf(-(d * d) / two_sig_sq) : 0.0f;
        }
    }
}

static float evaluateFieldScore(const std::vector<cv::Point2f>& pts, const cv::Mat& field,
                                float dx, float dy, int width, int height) {
    if (pts.empty()) return 0.0f;
    float total_score = 0.0f;
    int inside_count = 0;

    for (const auto& pt : pts) {
        float x = pt.x + dx;
        float y = pt.y + dy;
        int ix = (int)std::round(x);
        int iy = (int)std::round(y);

        if (ix >= 0 && ix < width && iy >= 0 && iy < height) {
            total_score += field.at<float>(iy, ix);
            inside_count++;
        }
    }

    if (inside_count < (int)(pts.size() * 0.40f)) {
        return 0.0f;
    }

    return total_score / (float)pts.size();
}

AlignmentResult PhysicsAlignment::align(const uint8_t *cam_y, const uint8_t *therm_data,
                                       int cam_width, int cam_height,
                                       int therm_width, int therm_height) {
    AlignmentResult res = { false, ALIGN_INTERNAL_ERROR, 25.0f, -5.0f, 1.0f, 0.8f, 0.0f, 0.0f };

    // --- STEP 1: Thermal Preprocessing & Observability Verification ---
    ThermalPreprocess::ProcessedThermal prep = ThermalPreprocess::process(therm_data, therm_width, therm_height);
    ThermalTarget target = ThermalTargetDetector::detect(prep);

    if (!target.valid || target.status != ALIGN_OK) {
        LOGW("PCTVA: Thermal target rejected with status %d", target.status);
        res.status = target.status;
        return res;
    }

    const std::vector<cv::Point>& best_contour = target.contour;

    // --- STEP 2: Multi-Scale Stage 1 Coarse Search (160x120) ---
    const int W1 = 160, H1 = 120;
    cv::Mat im_cam(cam_height, cam_width, CV_8UC1, const_cast<uint8_t *>(cam_y));

    cv::Mat dist1, field1;
    buildGaussianDistanceField(im_cam, W1, H1, dist1, field1, 2.5f);

    std::vector<cv::Point2f> pts160 = sampleContourPoints(best_contour, (float)W1 / therm_width, (float)H1 / therm_height, 2.0f);
    if (pts160.empty()) {
        res.status = ALIGN_NO_TARGET;
        return res;
    }

    // Parallax Model parameters at 160x120 (scaled by 0.25)
    float ax160 = CalibrationModel::DEFAULT_AX * 0.25f;
    float bx160 = CalibrationModel::DEFAULT_BX * 0.25f;
    float by160 = CalibrationModel::DEFAULT_BASE_Y * 0.25f;

    struct Candidate {
        float q;
        float Z;
        int rx;
        int ry;
        float dx160;
        float dy160;
        float score;
        float reg_score;
    };

    std::vector<Candidate> candidates;
    candidates.reserve(32 * 5 * 5);

    const int N_Q = 32;
    float min_q = 1.0f / CalibrationModel::MAX_Z; // 0.40
    float max_q = 1.0f / CalibrationModel::MIN_Z; // 5.556

    float best_s1 = -1.0f;
    Candidate best_c1 = { 0, 0, 0, 0, 0, 0, 0, 0 };

    for (int iq = 0; iq < N_Q; ++iq) {
        float q = min_q + (float)iq * (max_q - min_q) / (float)(N_Q - 1);
        float Z = 1.0f / q;
        float pred_dx = ax160 * q + bx160;
        float pred_dy = by160;

        for (int rx = -2; rx <= 2; ++rx) {
            for (int ry = -2; ry <= 2; ++ry) {
                float dx160 = pred_dx + (float)rx;
                float dy160 = pred_dy + (float)ry;

                float sc = evaluateFieldScore(pts160, field1, dx160, dy160, W1, H1);
                float reg_sc = sc - 0.003f * (float)(rx * rx + ry * ry);

                Candidate c = { q, Z, rx, ry, dx160, dy160, sc, reg_sc };
                candidates.push_back(c);

                if (reg_sc > best_s1) {
                    best_s1 = reg_sc;
                    best_c1 = c;
                }
            }
        }
    }

    // --- STEP 3: Multi-Scale Stage 2 Refinement (320x240) ---
    const int W2 = 320, H2 = 240;
    cv::Mat dist2, field2;
    buildGaussianDistanceField(im_cam, W2, H2, dist2, field2, 2.5f);

    std::vector<cv::Point2f> pts320 = sampleContourPoints(best_contour, (float)W2 / therm_width, (float)H2 / therm_height, 2.0f);

    float ax320 = CalibrationModel::DEFAULT_AX * 0.5f;
    float bx320 = CalibrationModel::DEFAULT_BX * 0.5f;
    float by320 = CalibrationModel::DEFAULT_BASE_Y * 0.5f;

    float best_q = best_c1.q;
    float q_min_ref = std::max(min_q, best_q * 0.85f);
    float q_max_ref = std::min(max_q, best_q * 1.15f);

    float best_s2 = -1.0f;
    float refined_q = best_q;
    float refined_dx320 = best_c1.dx160 * 2.0f;
    float refined_dy320 = best_c1.dy160 * 2.0f;

    const int N_Q_REF = 9;
    for (int iq = 0; iq < N_Q_REF; ++iq) {
        float q = q_min_ref + (float)iq * (q_max_ref - q_min_ref) / (float)(N_Q_REF - 1);
        float pred_dx = ax320 * q + bx320;
        float pred_dy = by320;

        for (int rx = -2; rx <= 2; ++rx) {
            for (int ry = -2; ry <= 2; ++ry) {
                float dx320 = pred_dx + (float)rx;
                float dy320 = pred_dy + (float)ry;

                float sc = evaluateFieldScore(pts320, field2, dx320, dy320, W2, H2);
                float reg_sc = sc - 0.003f * (float)(rx * rx + ry * ry);

                if (reg_sc > best_s2) {
                    best_s2 = reg_sc;
                    refined_q = q;
                    refined_dx320 = dx320;
                    refined_dy320 = dy320;
                }
            }
        }
    }

    // Full-Resolution (640x480) Parallax & Distance
    float dx_640 = refined_dx320 * 2.0f;
    float dy_640 = refined_dy320 * 2.0f;
    float Z_est = 1.0f / refined_q;

    // --- STEP 4: Peak Confidence & PSR Analysis ---
    float second_best_s1 = 0.0f;
    double side_sum = 0.0;
    double side_sq_sum = 0.0;
    int side_count = 0;

    for (const auto& c : candidates) {
        float dq = std::abs(c.q - best_c1.q);
        int drx = std::abs(c.rx - best_c1.rx);
        int dry = std::abs(c.ry - best_c1.ry);

        if (dq > 0.45f || drx >= 2 || dry >= 2) {
            if (c.score > second_best_s1) {
                second_best_s1 = c.score;
            }
            side_sum += c.score;
            side_sq_sum += (double)c.score * c.score;
            side_count++;
        }
    }

    float peak_gap = best_s1 - second_best_s1;
    float psr = 0.0f;
    if (side_count > 10) {
        double mu = side_sum / side_count;
        double var = (side_sq_sum / side_count) - (mu * mu);
        double stddev = std::sqrt(std::max(1e-7, var));
        psr = (float)((best_s1 - mu) / stddev);
    }

    LOGI("PCTVA: Z=%.2fm, dx=%.1f, dy=%.1f, score=%.3f, peakGap=%.3f, psr=%.2f",
         Z_est, dx_640, dy_640, best_s2, peak_gap, psr);

    // --- STEP 5: Boundary Rejection & Acceptance Criteria ---
    if (Z_est <= CalibrationModel::MIN_Z + 0.015f || Z_est >= CalibrationModel::MAX_Z - 0.04f ||
        dx_640 <= 11.0f || dx_640 >= 104.0f) {
        LOGW("PCTVA: Boundary hit detected -> ALIGN_RANGE_LIMITED");
        res.status = ALIGN_RANGE_LIMITED;
        return res;
    }

    bool score_ok = (best_s2 >= 0.28f);
    bool gap_ok = (peak_gap >= 0.035f);
    bool psr_ok = (psr >= 3.8f);

    if (!score_ok || (!gap_ok && !psr_ok)) {
        LOGW("PCTVA: Confidence not met (score=%.3f, gap=%.3f, psr=%.2f) -> ALIGN_AMBIGUOUS",
             best_s2, peak_gap, psr);
        res.status = ALIGN_AMBIGUOUS;
        return res;
    }

    res.success = true;
    res.status = ALIGN_OK;
    res.dx = dx_640;
    res.dy = dy_640;
    res.scale = 1.0f;
    res.distance = Z_est;
    res.confidence = best_s2;
    res.psr = psr;

    LOGI("PCTVA SUCCESS: status=OK, Z=%.2fm, dx=%.1f, dy=%.1f, score=%.3f, psr=%.2f",
         Z_est, dx_640, dy_640, best_s2, psr);
    return res;
}
