#include <vector>
#include <cmath>
#include <algorithm>
#include <chrono>
#include <limits>
#include <cstdint>

struct Point2D {
    float x;
    float y;
};

struct AlignmentOutput {
    float dx;
    float dy;
    float scale;
    float score;
    float confidence;
    float inlier_ratio;
    float runtime_ms;
};

static inline float evaluateChamfer(const Point2D *rel_pts,
                                    size_t num_pts,
                                    Point2D centroid,
                                    float scale,
                                    float dx,
                                    float dy,
                                    const float *dist_map,
                                    int w,
                                    int h,
                                    float oob_penalty,
                                    float lambda_phys,
                                    float exp_dx,
                                    float exp_dy) {
    float dist_sum = 0.0f;
    size_t valid_count = 0;

    for (size_t i = 0; i < num_pts; ++i) {
        float px = centroid.x + rel_pts[i].x * scale + dx;
        float py = centroid.y + rel_pts[i].y * scale + dy;

        int ix = (int)std::round(px);
        int iy = (int)std::round(py);

        if (ix >= 0 && ix < w && iy >= 0 && iy < h) {
            dist_sum += dist_map[iy * w + ix];
            valid_count++;
        }
    }

    if (valid_count < (size_t)(num_pts * 0.4f)) {
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

extern "C" {

#ifdef _WIN32
#define EXPORT_API __declspec(dllexport)
#else
#define EXPORT_API __attribute__((visibility("default")))
#endif

EXPORT_API void tcca_align_reference(const float *pts_x,
                                     const float *pts_y,
                                     int num_pts,
                                     float cx,
                                     float cy,
                                     const float *dist_map,
                                     int w,
                                     int h,
                                     float scale_min,
                                     float scale_max,
                                     float scale_step_coarse,
                                     float scale_step_fine,
                                     float dx_min,
                                     float dx_max,
                                     float dx_step_coarse,
                                     float dx_step_fine,
                                     float dy_min,
                                     float dy_max,
                                     float dy_step_coarse,
                                     float dy_step_fine,
                                     float oob_penalty,
                                     float lambda_phys,
                                     float exp_dx,
                                     float exp_dy,
                                     AlignmentOutput *out) {
    auto start_time = std::chrono::high_resolution_clock::now();

    Point2D centroid{cx, cy};
    std::vector<Point2D> rel_pts(num_pts);
    for (int i = 0; i < num_pts; ++i) {
        rel_pts[i].x = pts_x[i] - cx;
        rel_pts[i].y = pts_y[i] - cy;
    }

    // Coarse Grid Search
    float best_score = std::numeric_limits<float>::infinity();
    float best_scale = 1.0f;
    float best_dx = 0.0f;
    float best_dy = 0.0f;

    for (float s = scale_min; s <= scale_max + 1e-4f; s += scale_step_coarse) {
        for (float dy = dy_min; dy <= dy_max + 1e-4f; dy += dy_step_coarse) {
            for (float dx = dx_min; dx <= dx_max + 1e-4f; dx += dx_step_coarse) {
                float score = evaluateChamfer(rel_pts.data(), num_pts, centroid, s, dx, dy,
                                              dist_map, w, h, oob_penalty, lambda_phys, exp_dx, exp_dy);
                if (score < best_score) {
                    best_score = score;
                    best_scale = s;
                    best_dx = dx;
                    best_dy = dy;
                }
            }
        }
    }

    // Fine Grid Search around coarse optimum
    float fine_s_min = std::max(scale_min, best_scale - scale_step_coarse);
    float fine_s_max = std::min(scale_max, best_scale + scale_step_coarse);

    float fine_dx_min = std::max(dx_min, best_dx - dx_step_coarse);
    float fine_dx_max = std::min(dx_max, best_dx + dx_step_coarse);

    float fine_dy_min = std::max(dy_min, best_dy - dy_step_coarse);
    float fine_dy_max = std::min(dy_max, best_dy + dy_step_coarse);

    for (float s = fine_s_min; s <= fine_s_max + 1e-4f; s += scale_step_fine) {
        for (float dy = fine_dy_min; dy <= fine_dy_max + 1e-4f; dy += dy_step_fine) {
            for (float dx = fine_dx_min; dx <= fine_dx_max + 1e-4f; dx += dx_step_fine) {
                float score = evaluateChamfer(rel_pts.data(), num_pts, centroid, s, dx, dy,
                                              dist_map, w, h, oob_penalty, lambda_phys, exp_dx, exp_dy);
                if (score < best_score) {
                    best_score = score;
                    best_scale = s;
                    best_dx = dx;
                    best_dy = dy;
                }
            }
        }
    }

    // Inlier ratio
    size_t inliers = 0;
    for (int i = 0; i < num_pts; ++i) {
        float px = cx + rel_pts[i].x * best_scale + best_dx;
        float py = cy + rel_pts[i].y * best_scale + best_dy;
        int ix = std::max(0, std::min(w - 1, (int)std::round(px)));
        int iy = std::max(0, std::min(h - 1, (int)std::round(py)));
        if (dist_map[iy * w + ix] <= 4.0f) {
            inliers++;
        }
    }

    auto end_time = std::chrono::high_resolution_clock::now();
    out->runtime_ms = std::chrono::duration<float, std::milli>(end_time - start_time).count();
    out->dx = best_dx;
    out->dy = best_dy;
    out->scale = best_scale;
    out->score = best_score;
    out->confidence = 1.0f / (1.0f + (best_score / 4.0f));
    out->inlier_ratio = (float)inliers / (float)num_pts;
}

EXPORT_API void tcca_align_fast(const float *pts_x,
                                const float *pts_y,
                                int num_pts,
                                float cx,
                                float cy,
                                const float *dist_map,
                                int w,
                                int h,
                                float scale_min,
                                float scale_max,
                                float dx_min,
                                float dx_max,
                                float dy_min,
                                float dy_max,
                                float oob_penalty,
                                float lambda_phys,
                                float exp_dx,
                                float exp_dy,
                                AlignmentOutput *out) {
    auto start_time = std::chrono::high_resolution_clock::now();

    Point2D centroid{cx, cy};
    std::vector<Point2D> rel_pts(num_pts);
    for (int i = 0; i < num_pts; ++i) {
        rel_pts[i].x = pts_x[i] - cx;
        rel_pts[i].y = pts_y[i] - cy;
    }

    // Stage 1: Half resolution (320x240) downsampled grid
    int hw = w / 2;
    int hh = h / 2;
    std::vector<float> dist_half(hw * hh);
    for (int r = 0; r < hh; ++r) {
        for (int c = 0; c < hw; ++c) {
            dist_half[r * hw + c] = dist_map[(r * 2) * w + (c * 2)] * 0.5f;
        }
    }

    Point2D c_half{cx * 0.5f, cy * 0.5f};
    std::vector<Point2D> rel_half(num_pts);
    for (int i = 0; i < num_pts; ++i) {
        rel_half[i].x = rel_pts[i].x * 0.5f;
        rel_half[i].y = rel_pts[i].y * 0.5f;
    }

    float best_score_half = std::numeric_limits<float>::infinity();
    float best_scale = 1.0f;
    float best_dx_half = 0.0f;
    float best_dy_half = 0.0f;

    float s_min = std::max(0.80f, scale_min);
    float s_max = std::min(1.35f, scale_max);

    // Fast search with coarse step 2px on half-res (= 4px at full-res)
    for (float s = s_min; s <= s_max + 1e-4f; s += 0.06f) {
        for (float dy_h = dy_min * 0.5f; dy_h <= dy_max * 0.5f + 1e-4f; dy_h += 2.0f) {
            for (float dx_h = dx_min * 0.5f; dx_h <= dx_max * 0.5f + 1e-4f; dx_h += 2.0f) {
                float score = evaluateChamfer(rel_half.data(), num_pts, c_half, s, dx_h, dy_h,
                                              dist_half.data(), hw, hh, oob_penalty * 0.5f,
                                              lambda_phys, exp_dx * 0.5f, exp_dy * 0.5f);
                if (score < best_score_half) {
                    best_score_half = score;
                    best_scale = s;
                    best_dx_half = dx_h;
                    best_dy_half = dy_h;
                }
            }
        }
    }

    // Stage 2: Full-Resolution 640x480 Local Refine
    float init_dx = best_dx_half * 2.0f;
    float init_dy = best_dy_half * 2.0f;

    float fine_s_min = std::max(scale_min, best_scale - 0.04f);
    float fine_s_max = std::min(scale_max, best_scale + 0.04f);

    float fine_dx_min = std::max(dx_min, init_dx - 6.0f);
    float fine_dx_max = std::min(dx_max, init_dx + 6.0f);

    float fine_dy_min = std::max(dy_min, init_dy - 4.0f);
    float fine_dy_max = std::min(dy_max, init_dy + 4.0f);

    float best_score = std::numeric_limits<float>::infinity();
    float best_dx = init_dx;
    float best_dy = init_dy;

    for (float s = fine_s_min; s <= fine_s_max + 1e-4f; s += 0.02f) {
        for (float dy = fine_dy_min; dy <= fine_dy_max + 1e-4f; dy += 1.0f) {
            for (float dx = fine_dx_min; dx <= fine_dx_max + 1e-4f; dx += 1.0f) {
                float score = evaluateChamfer(rel_pts.data(), num_pts, centroid, s, dx, dy,
                                              dist_map, w, h, oob_penalty, lambda_phys, exp_dx, exp_dy);
                if (score < best_score) {
                    best_score = score;
                    best_scale = s;
                    best_dx = dx;
                    best_dy = dy;
                }
            }
        }
    }

    // Inlier ratio
    size_t inliers = 0;
    for (int i = 0; i < num_pts; ++i) {
        float px = cx + rel_pts[i].x * best_scale + best_dx;
        float py = cy + rel_pts[i].y * best_scale + best_dy;
        int ix = std::max(0, std::min(w - 1, (int)std::round(px)));
        int iy = std::max(0, std::min(h - 1, (int)std::round(py)));
        if (dist_map[iy * w + ix] <= 4.0f) {
            inliers++;
        }
    }

    auto end_time = std::chrono::high_resolution_clock::now();
    out->runtime_ms = std::chrono::duration<float, std::milli>(end_time - start_time).count();
    out->dx = best_dx;
    out->dy = best_dy;
    out->scale = best_scale;
    out->score = best_score;
    out->confidence = 1.0f / (1.0f + (best_score / 4.0f));
    out->inlier_ratio = (float)inliers / (float)num_pts;
}

}
