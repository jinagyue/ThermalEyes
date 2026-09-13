#include "ChamferAligner.h"
#include <chrono>
#include <cmath>
#include <algorithm>
#include <limits>

static inline float evaluateChamfer(const std::vector<cv::Point2f> &rel_pts,
                                    const cv::Point2f &centroid,
                                    float scale,
                                    float dx,
                                    float dy,
                                    const cv::Mat &dist_map,
                                    float oob_penalty,
                                    float lambda_phys,
                                    float exp_dx,
                                    float exp_dy) {
    int w = dist_map.cols;
    int h = dist_map.rows;
    size_t num_pts = rel_pts.size();

    float dist_sum = 0.0f;
    size_t valid_count = 0;

    for (size_t i = 0; i < num_pts; ++i) {
        float px = centroid.x + rel_pts[i].x * scale + dx;
        float py = centroid.y + rel_pts[i].y * scale + dy;

        int ix = (int)std::round(px);
        int iy = (int)std::round(py);

        if (ix >= 0 && ix < w && iy >= 0 && iy < h) {
            dist_sum += dist_map.at<float>(iy, ix);
            valid_count++;
        }
    }

    if (valid_count < (num_pts * 0.4f)) {
        return std::numeric_limits<float>::infinity();
    }

    size_t out_count = num_pts - valid_count;
    float score = (dist_sum + (float)out_count * oob_penalty) / (float)num_pts;

    if (lambda_phys > 0.0f) {
        float term_x = (dx - exp_dx) / 15.0f;
        float term_y = (dy - exp_dy) / 6.0f;
        score += lambda_phys * (term_x * term_x + 2.0f * term_y * term_y);
    }

    return score;
}

AlignmentResult ChamferAligner::alignReference(const ThermalContour &contour,
                                               const cv::Mat &dist_map,
                                               const TccaConfig &config) {
    auto start_time = std::chrono::high_resolution_clock::now();
    AlignmentResult res;
    res.success = false;
    res.status = INTERNAL_ERROR;
    res.dx = 0.0f;
    res.dy = 0.0f;
    res.scale = 1.0f;
    res.score = 999.0f;
    res.confidence = 0.0f;
    res.inlierRatio = 0.0f;
    res.runtimeMs = 0.0f;

    if (!contour.valid || contour.points.size() < 4 || dist_map.empty()) {
        res.status = (!contour.valid) ? contour.status : INSUFFICIENT_RGB_EDGES;
        return res;
    }

    size_t num_pts = contour.points.size();
    cv::Point2f c = contour.centroid;

    std::vector<cv::Point2f> rel_pts(num_pts);
    for (size_t i = 0; i < num_pts; ++i) {
        rel_pts[i] = contour.points[i] - c;
    }

    // -------------------------------------------------------------
    // Stage 1: Coarse Grid Search
    // -------------------------------------------------------------
    float best_score = std::numeric_limits<float>::infinity();
    float best_scale = 1.0f;
    float best_dx = 0.0f;
    float best_dy = 0.0f;

    for (float s = config.scale_min; s <= config.scale_max + 1e-4f; s += config.scale_step_coarse) {
        for (float dy = config.dy_min; dy <= config.dy_max + 1e-4f; dy += config.dy_step_coarse) {
            for (float dx = config.dx_min; dx <= config.dx_max + 1e-4f; dx += config.dx_step_coarse) {
                float score = evaluateChamfer(rel_pts, c, s, dx, dy, dist_map,
                                              config.out_of_bounds_penalty,
                                              config.lambda_phys,
                                              config.expected_dx,
                                              config.expected_dy);
                if (score < best_score) {
                    best_score = score;
                    best_scale = s;
                    best_dx = dx;
                    best_dy = dy;
                }
            }
        }
    }

    // -------------------------------------------------------------
    // Stage 2: Fine Grid Search around coarse optimum
    // -------------------------------------------------------------
    float fine_s_min = std::max(config.scale_min, best_scale - config.scale_step_coarse);
    float fine_s_max = std::min(config.scale_max, best_scale + config.scale_step_coarse);

    float fine_dx_min = std::max(config.dx_min, best_dx - config.dx_step_coarse);
    float fine_dx_max = std::min(config.dx_max, best_dx + config.dx_step_coarse);

    float fine_dy_min = std::max(config.dy_min, best_dy - config.dy_step_coarse);
    float fine_dy_max = std::min(config.dy_max, best_dy + config.dy_step_coarse);

    for (float s = fine_s_min; s <= fine_s_max + 1e-4f; s += config.scale_step_fine) {
        for (float dy = fine_dy_min; dy <= fine_dy_max + 1e-4f; dy += config.dy_step_fine) {
            for (float dx = fine_dx_min; dx <= fine_dx_max + 1e-4f; dx += config.dx_step_fine) {
                float score = evaluateChamfer(rel_pts, c, s, dx, dy, dist_map,
                                              config.out_of_bounds_penalty,
                                              config.lambda_phys,
                                              config.expected_dx,
                                              config.expected_dy);
                if (score < best_score) {
                    best_score = score;
                    best_scale = s;
                    best_dx = dx;
                    best_dy = dy;
                }
            }
        }
    }

    // Compute Inlier Ratio
    int w = dist_map.cols;
    int h = dist_map.rows;
    size_t inliers = 0;
    for (size_t i = 0; i < num_pts; ++i) {
        float px = c.x + rel_pts[i].x * best_scale + best_dx;
        float py = c.y + rel_pts[i].y * best_scale + best_dy;
        int ix = std::max(0, std::min(w - 1, (int)std::round(px)));
        int iy = std::max(0, std::min(h - 1, (int)std::round(py)));
        if (dist_map.at<float>(iy, ix) <= 4.0f) {
            inliers++;
        }
    }

    auto end_time = std::chrono::high_resolution_clock::now();
    res.runtimeMs = std::chrono::duration<float, std::milli>(end_time - start_time).count();

    res.success = true;
    res.status = ALIGN_OK;
    res.dx = best_dx;
    res.dy = best_dy;
    res.scale = best_scale;
    res.score = best_score;
    res.confidence = 1.0f / (1.0f + (best_score / 4.0f));
    res.inlierRatio = (float)inliers / (float)num_pts;
    return res;
}

AlignmentResult ChamferAligner::alignFast(const ThermalContour &contour,
                                          const cv::Mat &dist_map,
                                          const TccaConfig &config) {
    auto start_time = std::chrono::high_resolution_clock::now();
    AlignmentResult res;
    res.success = false;
    res.status = INTERNAL_ERROR;
    res.dx = 0.0f;
    res.dy = 0.0f;
    res.scale = 1.0f;
    res.score = 999.0f;
    res.confidence = 0.0f;
    res.inlierRatio = 0.0f;
    res.runtimeMs = 0.0f;

    if (!contour.valid || contour.points.size() < 4 || dist_map.empty()) {
        res.status = (!contour.valid) ? contour.status : INSUFFICIENT_RGB_EDGES;
        return res;
    }

    size_t num_pts = contour.points.size();
    cv::Point2f c = contour.centroid;

    std::vector<cv::Point2f> rel_pts(num_pts);
    for (size_t i = 0; i < num_pts; ++i) {
        rel_pts[i] = contour.points[i] - c;
    }

    // -------------------------------------------------------------
    // Mobile Stage 1: Half-Scale 320x240 Fast Coarse Search
    // -------------------------------------------------------------
    cv::Mat dist_half;
    cv::resize(dist_map, dist_half, cv::Size(dist_map.cols / 2, dist_map.rows / 2), 0, 0, cv::INTER_NEAREST);
    dist_half *= 0.5f; // Euclidean distance scales by 0.5

    cv::Point2f c_half(c.x * 0.5f, c.y * 0.5f);
    std::vector<cv::Point2f> rel_half(num_pts);
    for (size_t i = 0; i < num_pts; ++i) {
        rel_half[i] = rel_pts[i] * 0.5f;
    }

    float best_score_half = std::numeric_limits<float>::infinity();
    float best_scale = 1.0f;
    float best_dx_half = 0.0f;
    float best_dy_half = 0.0f;

    float s_min = std::max(0.80f, config.scale_min);
    float s_max = std::min(1.35f, config.scale_max);

    // Search step on half-resolution: coarse step = 2.0px (= 4.0px at 640x480)
    for (float s = s_min; s <= s_max + 1e-4f; s += 0.06f) {
        for (float dy_h = config.dy_min * 0.5f; dy_h <= config.dy_max * 0.5f + 1e-4f; dy_h += 2.0f) {
            for (float dx_h = config.dx_min * 0.5f; dx_h <= config.dx_max * 0.5f + 1e-4f; dx_h += 2.0f) {
                float score = evaluateChamfer(rel_half, c_half, s, dx_h, dy_h, dist_half,
                                              config.out_of_bounds_penalty * 0.5f,
                                              config.lambda_phys,
                                              config.expected_dx * 0.5f,
                                              config.expected_dy * 0.5f);
                if (score < best_score_half) {
                    best_score_half = score;
                    best_scale = s;
                    best_dx_half = dx_h;
                    best_dy_half = dy_h;
                }
            }
        }
    }

    // -------------------------------------------------------------
    // Mobile Stage 2: Full-Resolution 640x480 Fine Local Refine
    // -------------------------------------------------------------
    float init_dx = best_dx_half * 2.0f;
    float init_dy = best_dy_half * 2.0f;

    float fine_s_min = std::max(config.scale_min, best_scale - 0.04f);
    float fine_s_max = std::min(config.scale_max, best_scale + 0.04f);

    float fine_dx_min = std::max(config.dx_min, init_dx - 6.0f);
    float fine_dx_max = std::min(config.dx_max, init_dx + 6.0f);

    float fine_dy_min = std::max(config.dy_min, init_dy - 4.0f);
    float fine_dy_max = std::min(config.dy_max, init_dy + 4.0f);

    float best_score = std::numeric_limits<float>::infinity();
    float best_dx = init_dx;
    float best_dy = init_dy;

    for (float s = fine_s_min; s <= fine_s_max + 1e-4f; s += config.scale_step_fine) {
        for (float dy = fine_dy_min; dy <= fine_dy_max + 1e-4f; dy += config.dy_step_fine) {
            for (float dx = fine_dx_min; dx <= fine_dx_max + 1e-4f; dx += config.dx_step_fine) {
                float score = evaluateChamfer(rel_pts, c, s, dx, dy, dist_map,
                                              config.out_of_bounds_penalty,
                                              config.lambda_phys,
                                              config.expected_dx,
                                              config.expected_dy);
                if (score < best_score) {
                    best_score = score;
                    best_scale = s;
                    best_dx = dx;
                    best_dy = dy;
                }
            }
        }
    }

    // Compute Inlier Ratio
    int w = dist_map.cols;
    int h = dist_map.rows;
    size_t inliers = 0;
    for (size_t i = 0; i < num_pts; ++i) {
        float px = c.x + rel_pts[i].x * best_scale + best_dx;
        float py = c.y + rel_pts[i].y * best_scale + best_dy;
        int ix = std::max(0, std::min(w - 1, (int)std::round(px)));
        int iy = std::max(0, std::min(h - 1, (int)std::round(py)));
        if (dist_map.at<float>(iy, ix) <= 4.0f) {
            inliers++;
        }
    }

    auto end_time = std::chrono::high_resolution_clock::now();
    res.runtimeMs = std::chrono::duration<float, std::milli>(end_time - start_time).count();

    res.success = true;
    res.status = ALIGN_OK;
    res.dx = best_dx;
    res.dy = best_dy;
    res.scale = best_scale;
    res.score = best_score;
    res.confidence = 1.0f / (1.0f + (best_score / 4.0f));
    res.inlierRatio = (float)inliers / (float)num_pts;
    return res;
}

AlignmentResult ChamferAligner::align(const ThermalContour &contour,
                                     const cv::Mat &dist_map,
                                     AlignAlgorithm algo,
                                     const TccaConfig &config) {
    TccaConfig cfg = config;
    if (algo == TCCA_PHYS) {
        if (cfg.lambda_phys <= 0.0f) {
            cfg.lambda_phys = 0.5f;
        }
        return alignReference(contour, dist_map, cfg);
    }
    return alignFast(contour, dist_map, cfg);
}
