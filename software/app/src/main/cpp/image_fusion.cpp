#include <jni.h>
#include <math.h>
#include <android/log.h>
#include <setjmp.h>
#include <signal.h>
#include <mutex>
#include <opencv2/opencv.hpp>

using namespace cv;
using namespace std;

// Write C++ code here.
//
// Do not forget to dynamically load the C++ library into your application.
//
// For instance,
//
// In MainActivity.java:
//    static {
//       System.loadLibrary("img_algo");
//    }
//
// Or, in MainActivity.kt:
//    companion object {
//      init {
//         System.loadLibrary("img_algo")
//      }
//    }
#define LOGE(...) __android_log_print(ANDROID_LOG_ERROR, "jni_c", __VA_ARGS__)
#define LOGW(...) __android_log_print(ANDROID_LOG_WARN, "jni_c", __VA_ARGS__)
#define LOGI(...) __android_log_print(ANDROID_LOG_INFO, "jni_c", __VA_ARGS__)

#define CLIP(x, a, b) ((x) < (a) ? (a) : MIN(x, b))

#define RANGE 255

typedef enum {
    FUSION_MODE_FUSION = 0,     // 0: 双光融合 (MSX / Dual-spectrum Alpha + Detail)
    FUSION_MODE_THERMAL = 1,    // 1: 纯热像 (Pure Thermal Pseudocolor)
    FUSION_MODE_VISIBLE = 2,    // 2: 纯可见光 (Pure Visible Light Camera)
    FUSION_MODE_MAX = 3
} image_fusion_mode;

typedef enum {
    HIGH_FREQ_RATIO_NONE,
    HIGH_FREQ_RATIO_LOW,
    HIGH_FREQ_RATIO_MEDIUM,
    HIGH_FREQ_RATIO_HIGH,
    HIGH_FREQ_RATIO_MAX
} image_high_freq_ratio;

typedef struct {
    image_fusion_mode mode;
    int ratio;
    int color_tab;
    float cam_y_k, cam_uv_k;
    float therm_y_k, therm_uv_k;
    uint32_t parallax_offset;
    int offset_x;
    int offset_y;
    float scale;
    float rotation;
    uint32_t cam_width, cam_height;
    uint32_t therm_width, therm_height;
    uint8_t *result;
    const uint8_t *cam;
    const uint8_t *therm;
    // Industry Processing Fields
    int industry_mode; // 0: Standard, 1: Electrical, 2: Building, 3: PCB, 4: Medical
    bool enable_clahe;
    float clahe_clip;
    bool enable_diff;
    float diff_gain;
    bool enable_isotherm;
    uint8_t isotherm_threshold;
    bool isotherm_invert;
    bool mirror_x;
    bool mirror_y;
} image_data;

static image_data g_image = {
        .mode = FUSION_MODE_FUSION,
        .ratio = 50,
        .color_tab = COLORMAP_JET,
        .cam_y_k = 1,
        .cam_uv_k = 1,
        .therm_y_k = 1,
        .therm_uv_k = 1,
        .parallax_offset = 0,
        .offset_x = 25,
        .offset_y = -5,
        .scale = 1.0f,
        .rotation = 0.0f,
        .industry_mode = 0,
        .enable_clahe = false,
        .clahe_clip = 3.5f,
        .enable_diff = false,
        .diff_gain = 2.5f,
        .enable_isotherm = false,
        .isotherm_threshold = 128,
        .isotherm_invert = false,
        .mirror_x = false,
        .mirror_y = false
};

static cv::Mat g_baseline_therm;
static bool g_trigger_capture_baseline = false;
static bool g_has_baseline = false;

void color_map_fusion(uint8_t *fusion_data, const uint8_t *cam_data, const uint8_t *therm_bgr, const uint8_t *cam_blur)
{
    uint32_t width = g_image.cam_width;
    uint32_t height = g_image.cam_height;
    uint32_t total_pixels = width * height;

    uint8_t *cam_y = (uint8_t *)cam_data;
    uint8_t *cam_uv = (uint8_t *)cam_data + total_pixels;
    uint8_t *fusion_y = (uint8_t *)fusion_data;
    uint8_t *fusion_uv = (uint8_t *)fusion_data + total_pixels;

    // Mode 2: 纯可见光 (Pure Visible Camera)
    if (g_image.mode == FUSION_MODE_VISIBLE) {
        memcpy(fusion_data, cam_data, total_pixels * 3 / 2);
        return;
    }

    // Mode 1: 纯热像 (Pure Thermal Pseudocolor)
    if (g_image.mode == FUSION_MODE_THERMAL) {
        for (uint32_t pos = 0; pos < total_pixels; pos++) {
            uint8_t b = therm_bgr[pos * 3];
            uint8_t g = therm_bgr[pos * 3 + 1];
            uint8_t r = therm_bgr[pos * 3 + 2];
            int y = (int)(0.299f * r + 0.587f * g + 0.114f * b);
            fusion_y[pos] = (uint8_t)CLIP(y, 0, 255);
        }

        for (uint32_t i = 0; i < height / 2; i++) {
            for (uint32_t j = 0; j < width / 2; j++) {
                uint32_t bgr_pos = (i * 2 * width + j * 2) * 3;
                uint8_t b = therm_bgr[bgr_pos];
                uint8_t g = therm_bgr[bgr_pos + 1];
                uint8_t r = therm_bgr[bgr_pos + 2];

                int u = (int)(-0.169f * r - 0.331f * g + 0.500f * b + 128.0f);
                int v = (int)( 0.500f * r - 0.419f * g - 0.081f * b + 128.0f);

                uint32_t uv_idx = i * width + j * 2;
                fusion_uv[uv_idx]     = (uint8_t)CLIP(v, 0, 255); // NV21: V
                fusion_uv[uv_idx + 1] = (uint8_t)CLIP(u, 0, 255); // NV21: U
            }
        }
        return;
    }

    // Mode 0: 双光融合 (Dual-Spectrum Fusion / MSX)
    float blend = CLIP(g_image.ratio, 0, 100) / 100.0f; // 0.0 to 1.0 (default 0.5)

    // Luminance weights:
    // Visible camera details + Thermal heat intensity + MSX high-pass edge outlines
    float w_cam = 0.35f + blend * 0.40f; // 0.35 to 0.75
    float w_therm = 1.0f - w_cam;        // 0.65 to 0.25
    float edge_k = 1.2f + blend * 1.8f;  // 1.2 to 3.0

    for (uint32_t pos = 0; pos < total_pixels; pos++) {
        uint8_t b = therm_bgr[pos * 3];
        uint8_t g = therm_bgr[pos * 3 + 1];
        uint8_t r = therm_bgr[pos * 3 + 2];
        float therm_y = 0.299f * r + 0.587f * g + 0.114f * b;

        float edge = (float)cam_y[pos] - (float)cam_blur[pos];
        int y_val = (int)(w_cam * cam_y[pos] + w_therm * therm_y + edge_k * edge);
        fusion_y[pos] = (uint8_t)CLIP(y_val, 0, 255);
    }

    // Chrominance (Color) Blending:
    // Thermal colors dominate (red/blue/yellow) with subtle natural camera chrominance
    float w_color_cam = blend * 0.30f; // 0.0 to 0.30
    float w_color_therm = 1.0f - w_color_cam; // 1.0 to 0.70

    for (uint32_t i = 0; i < height / 2; i++) {
        for (uint32_t j = 0; j < width / 2; j++) {
            uint32_t bgr_pos = (i * 2 * width + j * 2) * 3;
            uint8_t b = therm_bgr[bgr_pos];
            uint8_t g = therm_bgr[bgr_pos + 1];
            uint8_t r = therm_bgr[bgr_pos + 2];

            float therm_u = -0.169f * r - 0.331f * g + 0.500f * b + 128.0f;
            float therm_v =  0.500f * r - 0.419f * g - 0.081f * b + 128.0f;

            uint32_t uv_idx = i * width + j * 2;
            int v_val = (int)(w_color_therm * therm_v + w_color_cam * cam_uv[uv_idx]);
            int u_val = (int)(w_color_therm * therm_u + w_color_cam * cam_uv[uv_idx + 1]);

            fusion_uv[uv_idx]     = (uint8_t)CLIP(v_val, 0, 255);
            fusion_uv[uv_idx + 1] = (uint8_t)CLIP(u_val, 0, 255);
        }
    }
}


void fusion_get_image(uint32_t *fusion_data, const uint32_t *cam_data, const uint32_t *therm_data)
{
    uint32_t width = g_image.cam_width;
    uint32_t height = g_image.cam_height;

    // Fast path: Pure Visible Mode
    if (g_image.mode == FUSION_MODE_VISIBLE) {
        memcpy(fusion_data, cam_data, width * height * 3 / 2);
        return;
    }

    Mat im_cam(height, width, CV_8UC1, (uint8_t *)cam_data);
    Mat im_therm(g_image.therm_height, g_image.therm_width, CV_8UC1, (uint8_t *)therm_data);

    // Hardware normalization: MLX90640 is physically mirrored horizontally relative to camera
    Mat im_therm_prep;
    flip(im_therm, im_therm_prep, 1);

    Mat im_therm_scale;
    resize(im_therm_prep, im_therm_scale, Size(width, height), 0, 0, INTER_LINEAR);

    // 4-DOF Affine Visual Calibration (Center-based rotation & scaling + translation)
    Point2f center(width / 2.0f, height / 2.0f);
    float scale_val = g_image.scale <= 0.1f ? 1.0f : g_image.scale;
    Mat rotMat = getRotationMatrix2D(center, g_image.rotation, scale_val);
    rotMat.at<double>(0, 2) += g_image.offset_x;
    rotMat.at<double>(1, 2) += g_image.offset_y;

    Mat im_therm_border;
    warpAffine(im_therm_scale, im_therm_border, rotMat, Size(width, height), INTER_LINEAR, BORDER_REPLICATE);

    // --- Industry Algorithm Enhancements ---

    // 1. PCB Baseline Subtraction / Differential
    if (g_trigger_capture_baseline) {
        im_therm_border.copyTo(g_baseline_therm);
        g_has_baseline = true;
        g_trigger_capture_baseline = false;
        LOGI("Baseline captured successfully");
    }
    if (g_image.industry_mode == 3 && g_has_baseline && !g_baseline_therm.empty() &&
        g_baseline_therm.size() == im_therm_border.size()) {
        Mat diff;
        absdiff(im_therm_border, g_baseline_therm, diff);
        float gain = g_image.diff_gain > 0.1f ? g_image.diff_gain : 2.5f;
        diff.convertTo(im_therm_border, -1, gain, 0);
    }

    // 2. Building / HVAC Micro-contrast CLAHE Enhancement
    if (g_image.industry_mode == 2 || g_image.enable_clahe) {
        Ptr<CLAHE> clahe = createCLAHE(g_image.clahe_clip > 0.1f ? g_image.clahe_clip : 3.5, Size(8, 8));
        clahe->apply(im_therm_border, im_therm_border);
    }

    // 3. Medical S-Curve Physiological Stretch (35~42C Focus)
    if (g_image.industry_mode == 4) {
        static uchar medical_lut[256];
        static bool lut_init = false;
        if (!lut_init) {
            for (int v = 0; v < 256; v++) {
                double x = (v - 135.0) / 25.0;
                double s = 1.0 / (1.0 + exp(-x));
                medical_lut[v] = (uchar)CLIP((int)(s * 255.0), 0, 255);
            }
            lut_init = true;
        }
        Mat lut_mat(1, 256, CV_8U, medical_lut);
        LUT(im_therm_border, lut_mat, im_therm_border);
    }

    // 4. Electrical / Isotherm Alarm Mask Preparation
    Mat mask_isotherm;
    bool apply_isotherm = (g_image.industry_mode == 1) || g_image.enable_isotherm;
    if (apply_isotherm) {
        if (!g_image.isotherm_invert) {
            threshold(im_therm_border, mask_isotherm, g_image.isotherm_threshold, 255, THRESH_BINARY);
        } else {
            threshold(im_therm_border, mask_isotherm, g_image.isotherm_threshold, 255, THRESH_BINARY_INV);
        }
    }

    // Thermal Pseudocolor Mapping
    Mat im_fusion_color;
    applyColorMap(im_therm_border, im_fusion_color, g_image.color_tab);

    // Apply Isotherm Color Mask: Desaturate non-alarm regions to clean grayscale
    if (apply_isotherm && !mask_isotherm.empty()) {
        for (int r = 0; r < im_fusion_color.rows; r++) {
            Vec3b *row_color = im_fusion_color.ptr<Vec3b>(r);
            const uchar *row_mask = mask_isotherm.ptr<uchar>(r);
            for (int c = 0; c < im_fusion_color.cols; c++) {
                if (row_mask[c] == 0) {
                    uchar gray = (uchar)(0.114f * row_color[c][0] + 0.587f * row_color[c][1] + 0.299f * row_color[c][2]);
                    row_color[c] = Vec3b(gray, gray, gray);
                }
            }
        }
    }

    // Visible High-Frequency Edge Extraction
    Mat im_cam_blur;
    GaussianBlur(im_cam, im_cam_blur, Size(7, 7), 2.5, 2.5);

    // Synthesize into output NV21 buffer
    color_map_fusion((uint8_t *)fusion_data, (uint8_t *)cam_data, im_fusion_color.data, im_cam_blur.data);
}

extern "C"
JNIEXPORT void JNICALL
Java_com_example_thermaleyes_ImageFusion_getFusionImage(JNIEnv *env, jobject thiz,
                                                        jbyteArray fusionData, jbyteArray camData,
                                                        jbyteArray thermData, jint cam_width,
                                                        jint cam_height, jint therm_width,
                                                        jint therm_height) {
    jbyte *fusion_data = (jbyte *)env->GetByteArrayElements(fusionData, 0);
    jbyte *cam_data = (jbyte *)env->GetByteArrayElements(camData, 0);
    jbyte *therm_data = (jbyte *)env->GetByteArrayElements(thermData, 0);

    /* 数据融合 */
    LOGI("fusion start");
    g_image.cam_width = cam_width;
    g_image.cam_height = cam_height;
    g_image.therm_width = therm_width;
    g_image.therm_height = therm_height;
    fusion_get_image((uint32_t *)fusion_data, (uint32_t *)cam_data, (uint32_t *)therm_data);
    LOGI("fusion end");

    env->ReleaseByteArrayElements(fusionData, fusion_data, 0);
    env->ReleaseByteArrayElements(camData, cam_data, 0);
    env->ReleaseByteArrayElements(thermData, therm_data, 0);
}

extern "C"
JNIEXPORT void JNICALL
Java_com_example_thermaleyes_ImageFusion_setFusionMode(JNIEnv *env, jobject thiz, jint fusion_mode) {
    if (fusion_mode >= FUSION_MODE_MAX) {
        LOGE("Not support fusion mode: %d\n", fusion_mode);
        return;
    }

    g_image.mode = (image_fusion_mode)fusion_mode;
}
extern "C"
JNIEXPORT jint JNICALL
Java_com_example_thermaleyes_ImageFusion_getFusionMode(JNIEnv *env, jobject thiz) {
    return g_image.mode;
}
extern "C"
JNIEXPORT void JNICALL
Java_com_example_thermaleyes_ImageFusion_setFusionHighFreqRatio(JNIEnv *env, jobject thiz,
                                                          jint high_freq_ratio) {
    g_image.ratio = CLIP(high_freq_ratio, 0, 100);
}
extern "C"
JNIEXPORT jint JNICALL
Java_com_example_thermaleyes_ImageFusion_getFusionHighFreqRatio(JNIEnv *env, jobject thiz) {
    return g_image.ratio;
}
extern "C"
JNIEXPORT void JNICALL
Java_com_example_thermaleyes_ImageFusion_setFusionColorTab(JNIEnv *env, jobject thiz, jint color_tab) {
    if (color_tab == 0 || color_tab == COLORMAP_PLASMA) {
        g_image.color_tab = COLORMAP_PLASMA; // 15: PLASMA
    } else if (color_tab == 1 || color_tab == COLORMAP_JET) {
        g_image.color_tab = COLORMAP_JET; // 2: JET (Rainbow)
    } else if (color_tab <= COLORMAP_DEEPGREEN) {
        g_image.color_tab = color_tab;
    }
}
extern "C"
JNIEXPORT jint JNICALL
Java_com_example_thermaleyes_ImageFusion_getFusionColorTab(JNIEnv *env, jobject thiz) {
    return g_image.color_tab;
}
extern "C"
JNIEXPORT void JNICALL
Java_com_example_thermaleyes_ImageFusion_setFusionParallaxOffset(JNIEnv *env, jobject thiz,
                                                                  jint offset) {
    if (offset > 50) {
        LOGE("Not support Parallax offset: %d\n", offset);
        return;
    }

    g_image.parallax_offset = offset;
}
extern "C"
JNIEXPORT jint JNICALL
Java_com_example_thermaleyes_ImageFusion_getFusionParallaxOffset(JNIEnv *env, jobject thiz) {
    return g_image.parallax_offset;
}
extern "C"
JNIEXPORT void JNICALL
Java_com_example_thermaleyes_ImageFusion_setFusionParams(JNIEnv *env, jobject thiz,
                                                        jfloatArray params) {
    jfloat *param_tab = (jfloat *)env->GetFloatArrayElements(params, 0);

    g_image.cam_y_k = param_tab[0];
    g_image.cam_uv_k = param_tab[1];
    g_image.therm_y_k = param_tab[2];
    g_image.therm_uv_k = param_tab[3];

    env->ReleaseFloatArrayElements(params, param_tab, 0);
}
extern "C"
JNIEXPORT void JNICALL
Java_com_example_thermaleyes_ImageFusion_getFusionParams(JNIEnv *env, jobject thiz,
                                                         jfloatArray params) {
    jfloat *param_tab = (jfloat *)env->GetFloatArrayElements(params, 0);

    param_tab[0] = g_image.cam_y_k;
    param_tab[1] = g_image.cam_uv_k;
    param_tab[2] = g_image.therm_y_k;
    param_tab[3] = g_image.therm_uv_k;

    env->ReleaseFloatArrayElements(params, param_tab, 0);
}

extern "C"
JNIEXPORT void JNICALL
Java_com_example_thermaleyes_ImageFusion_setCalibrationParams(JNIEnv *env, jobject thiz,
                                                             jint offset_x, jint offset_y,
                                                             jfloat scale, jfloat rotation) {
    g_image.offset_x = offset_x;
    g_image.offset_y = offset_y;
    g_image.scale = scale;
    g_image.rotation = rotation;
    g_image.parallax_offset = offset_y > 0 ? offset_y : 0;
}

extern "C"
JNIEXPORT void JNICALL
Java_com_example_thermaleyes_ImageFusion_getCalibrationParams(JNIEnv *env, jobject thiz,
                                                             jfloatArray params) {
    jfloat *param_tab = (jfloat *)env->GetFloatArrayElements(params, 0);
    param_tab[0] = (jfloat)g_image.offset_x;
    param_tab[1] = (jfloat)g_image.offset_y;
    param_tab[2] = g_image.scale;
    param_tab[3] = g_image.rotation;
    env->ReleaseFloatArrayElements(params, param_tab, 0);
}

extern "C"
JNIEXPORT void JNICALL
Java_com_example_thermaleyes_ImageFusion_nativeSetMirror(JNIEnv *env, jobject thiz,
                                                         jboolean mirror_x, jboolean mirror_y) {
    g_image.mirror_x = mirror_x;
    g_image.mirror_y = mirror_y;
    LOGI("setMirror: mirror_x=%d, mirror_y=%d", mirror_x, mirror_y);
}

extern "C"
JNIEXPORT jboolean JNICALL
Java_com_example_thermaleyes_ImageFusion_nativeGetMirrorX(JNIEnv *env, jobject thiz) {
    return g_image.mirror_x ? JNI_TRUE : JNI_FALSE;
}

extern "C"
JNIEXPORT jboolean JNICALL
Java_com_example_thermaleyes_ImageFusion_nativeGetMirrorY(JNIEnv *env, jobject thiz) {
    return g_image.mirror_y ? JNI_TRUE : JNI_FALSE;
}

enum AlignStatus {
    ALIGN_OK = 0,
    ALIGN_LOW_CONTRAST = 1,
    ALIGN_TARGET_CLIPPED = 2,
    ALIGN_TOO_FEW_FEATURES = 3,
    ALIGN_AMBIGUOUS = 4,
    ALIGN_RANGE_LIMITED = 5,
    ALIGN_INTERNAL_ERROR = 6
};

struct AlignResult {
    AlignStatus status;
    float dx;
    float dy;
    float scale;
    float estimatedDistance;
    float score;
    float psr;
};

// Physical parallax model: dx(Z) = ax / Z + bx
// Initial empirical parameters (Must be re-fitted using actual multi-distance calibration)
static const float DEFAULT_PARALLAX_AX = 22.1f;    // px * m (at 640x480)
static const float DEFAULT_PARALLAX_BX = 1.5f;     // px (at 640x480)
static const float DEFAULT_BASE_OFFSET_Y = -5.0f;  // px (at 640x480)

static const float MIN_DISTANCE_Z = 0.18f;         // meters
static const float MAX_DISTANCE_Z = 2.50f;         // meters

static std::vector<Point2f> sample_contour_points(const std::vector<Point>& raw_pts,
                                                  float sx, float sy, float max_step = 2.0f) {
    std::vector<Point2f> sampled;
    if (raw_pts.size() < 3) return sampled;

    std::vector<Point2f> base;
    base.reserve(raw_pts.size());
    for (const auto& p : raw_pts) {
        base.emplace_back((p.x + 0.5f) * sx, (p.y + 0.5f) * sy);
    }

    for (size_t i = 0; i < base.size(); ++i) {
        Point2f p1 = base[i];
        Point2f p2 = base[(i + 1) % base.size()];
        float seg_len = (float)norm(p2 - p1);
        int steps = std::max(1, (int)std::ceil(seg_len / max_step));
        for (int s = 0; s < steps; ++s) {
            float t = (float)s / (float)steps;
            sampled.push_back(p1 + t * (p2 - p1));
        }
    }
    return sampled;
}

static void build_distance_field(const Mat& cam_y, int target_w, int target_h,
                                 Mat& out_dist, Mat& out_field, float sigma = 2.5f) {
    Mat cam_resized;
    resize(cam_y, cam_resized, Size(target_w, target_h), 0, 0, INTER_AREA);

    Mat cam_blur, cam_edge;
    GaussianBlur(cam_resized, cam_blur, Size(3, 3), 1.0);
    Canny(cam_blur, cam_edge, 35, 100);

    distanceTransform(~cam_edge, out_dist, DIST_L2, 3);

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

static float evaluate_contour_score(const std::vector<Point2f>& pts, const Mat& field,
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

static AlignResult auto_align_depth_aware(const uint8_t *cam_y, const uint8_t *therm_data,
                                          int cam_width, int cam_height,
                                          int therm_width, int therm_height) {
    AlignResult res = { ALIGN_INTERNAL_ERROR, 0.0f, 0.0f, 1.0f, 0.0f, 0.0f, 0.0f };

    // --- STEP 1: Native 32x24 Thermal Processing & Observability Verification ---
    Mat im_therm(therm_height, therm_width, CV_8UC1, (uint8_t *)therm_data);
    double min_val, max_val;
    minMaxLoc(im_therm, &min_val, &max_val);
    LOGI("auto_align: therm dynamic range=[%.1f, %.1f], delta=%.1f", min_val, max_val, max_val - min_val);

    // 1A. Low Contrast Check
    if (max_val - min_val < 3.0) {
        LOGW("auto_align: thermal contrast too low (%.1f)", max_val - min_val);
        res.status = ALIGN_LOW_CONTRAST;
        return res;
    }

    // 1B. Sensor Hardware Horizontal Flip
    Mat im_therm_flipped;
    flip(im_therm, im_therm_flipped, 1);

    // 1C. Native Scale Filtering & Otsu Thermal Segmentation
    Mat therm_smooth;
    GaussianBlur(im_therm_flipped, therm_smooth, Size(3, 3), 0.8);

    Mat therm_bin;
    threshold(therm_smooth, therm_bin, 0, 255, THRESH_BINARY | THRESH_OTSU);

    Mat morph_elem = getStructuringElement(MORPH_ELLIPSE, Size(3, 3));
    morphologyEx(therm_bin, therm_bin, MORPH_CLOSE, morph_elem);
    morphologyEx(therm_bin, therm_bin, MORPH_OPEN, morph_elem);

    // 1D. Target Clipped / Field-of-View Overflow Check
    int total_pixels = therm_width * therm_height;
    int fg_pixels = countNonZero(therm_bin);
    float fill_ratio = (float)fg_pixels / (float)total_pixels;

    int border_touch = 0;
    for (int c = 0; c < therm_bin.cols; ++c) {
        if (therm_bin.at<uchar>(0, c) > 0) border_touch++;
        if (therm_bin.at<uchar>(therm_bin.rows - 1, c) > 0) border_touch++;
    }
    for (int r = 0; r < therm_bin.rows; ++r) {
        if (therm_bin.at<uchar>(r, 0) > 0) border_touch++;
        if (therm_bin.at<uchar>(r, therm_bin.cols - 1) > 0) border_touch++;
    }

    LOGI("auto_align: fill_ratio=%.2f, border_touch=%d", fill_ratio, border_touch);

    if (fill_ratio > 0.85f || (fill_ratio > 0.78f && border_touch > 14)) {
        LOGW("auto_align: target clipped or overflowing FOV (fill=%.2f, border=%d)", fill_ratio, border_touch);
        res.status = ALIGN_TARGET_CLIPPED;
        return res;
    }

    // 1E. Extract External Contour
    std::vector<std::vector<Point>> contours;
    findContours(therm_bin, contours, RETR_EXTERNAL, CHAIN_APPROX_NONE);
    if (contours.empty()) {
        LOGW("auto_align: no contours found");
        res.status = ALIGN_TOO_FEW_FEATURES;
        return res;
    }

    size_t best_c_idx = 0;
    double max_area = 0.0;
    for (size_t i = 0; i < contours.size(); ++i) {
        double a = contourArea(contours[i]);
        if (a > max_area) {
            max_area = a;
            best_c_idx = i;
        }
    }

    const std::vector<Point>& best_contour = contours[best_c_idx];
    if (best_contour.size() < 12 || max_area < 20.0) {
        LOGW("auto_align: contour too weak (pts=%zu, area=%.1f)", best_contour.size(), max_area);
        res.status = ALIGN_TOO_FEW_FEATURES;
        return res;
    }

    // --- STEP 2: Multi-Scale Stage 1 Coarse Search (160x120) ---
    const int W1 = 160, H1 = 120;
    Mat im_cam(cam_height, cam_width, CV_8UC1, (uint8_t *)cam_y);

    Mat dist1, field1;
    build_distance_field(im_cam, W1, H1, dist1, field1, 2.5f);

    std::vector<Point2f> pts160 = sample_contour_points(best_contour, (float)W1 / therm_width, (float)H1 / therm_height, 2.0f);
    if (pts160.empty()) {
        res.status = ALIGN_TOO_FEW_FEATURES;
        return res;
    }

    // Stage 1 Parallax Model (scaled by 0.25)
    float ax160 = DEFAULT_PARALLAX_AX * 0.25f;
    float bx160 = DEFAULT_PARALLAX_BX * 0.25f;
    float by160 = DEFAULT_BASE_OFFSET_Y * 0.25f;

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
    float min_q = 1.0f / MAX_DISTANCE_Z; // 0.40
    float max_q = 1.0f / MIN_DISTANCE_Z; // 5.556

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

                float sc = evaluate_contour_score(pts160, field1, dx160, dy160, W1, H1);
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
    Mat dist2, field2;
    build_distance_field(im_cam, W2, H2, dist2, field2, 2.5f);

    std::vector<Point2f> pts320 = sample_contour_points(best_contour, (float)W2 / therm_width, (float)H2 / therm_height, 2.0f);

    float ax320 = DEFAULT_PARALLAX_AX * 0.5f;
    float bx320 = DEFAULT_PARALLAX_BX * 0.5f;
    float by320 = DEFAULT_BASE_OFFSET_Y * 0.5f;

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

                float sc = evaluate_contour_score(pts320, field2, dx320, dy320, W2, H2);
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

    // Equivalent Full-Resolution (640x480) Parallax Displacements
    float dx_640 = refined_dx320 * 2.0f;
    float dy_640 = refined_dy320 * 2.0f;
    float Z_est = 1.0f / refined_q;

    // --- STEP 4: Confidence Analysis (Peak Gap & Peak-to-Sidelobe Ratio) ---
    float second_best_s1 = 0.0f;
    double side_sum = 0.0;
    double side_sq_sum = 0.0;
    int side_count = 0;

    for (const auto& c : candidates) {
        float dq = std::abs(c.q - best_c1.q);
        int drx = std::abs(c.rx - best_c1.rx);
        int dry = std::abs(c.ry - best_c1.ry);

        // Exclusion radius around primary peak
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

    LOGI("AUTO_ALIGN: Z=%.2fm, dx=%.1f, dy=%.1f, score=%.3f, peakGap=%.3f, psr=%.2f",
         Z_est, dx_640, dy_640, best_s2, peak_gap, psr);

    // --- STEP 5: Boundary Hit Protection & Triple Criteria Validation ---
    if (Z_est <= MIN_DISTANCE_Z + 0.015f || Z_est >= MAX_DISTANCE_Z - 0.04f ||
        dx_640 <= 11.0f || dx_640 >= 104.0f) {
        LOGW("AUTO_ALIGN: boundary hit detected (Z=%.2fm, dx=%.1f), rejecting as RANGE_LIMITED", Z_est, dx_640);
        res.status = ALIGN_RANGE_LIMITED;
        return res;
    }

    bool score_ok = (best_s2 >= 0.28f);
    bool gap_ok = (peak_gap >= 0.035f);
    bool psr_ok = (psr >= 3.8f);

    if (!score_ok || (!gap_ok && !psr_ok)) {
        LOGW("AUTO_ALIGN: confidence criteria not met (score=%.3f, gap=%.3f, psr=%.2f), rejecting as AMBIGUOUS",
             best_s2, peak_gap, psr);
        res.status = ALIGN_AMBIGUOUS;
        return res;
    }

    // Success: Update runtime parameters
    res.status = ALIGN_OK;
    res.dx = dx_640;
    res.dy = dy_640;
    res.scale = 1.0f;
    res.estimatedDistance = Z_est;
    res.score = best_s2;
    res.psr = psr;

    // Apply directly to active runtime warp without changing static hardware calibration
    g_image.offset_x = (int)std::round(dx_640);
    g_image.offset_y = (int)std::round(dy_640);
    g_image.scale = 1.0f;
    g_image.mirror_x = false;

    LOGI("AUTO_ALIGN SUCCESS: status=OK, Z=%.2fm, dx=%.1f, dy=%.1f, score=%.3f, psr=%.2f",
         Z_est, dx_640, dy_640, best_s2, psr);
    return res;
}

extern "C"
JNIEXPORT jint JNICALL
Java_com_example_thermaleyes_ImageFusion_nativeAutoCalibrate(JNIEnv *env, jobject thiz,
                                                             jbyteArray camData,
                                                             jbyteArray thermData,
                                                             jint cam_width, jint cam_height,
                                                             jint therm_width, jint therm_height,
                                                             jfloatArray results) {
    jbyte *cam_data = env->GetByteArrayElements(camData, 0);
    jbyte *therm_data = env->GetByteArrayElements(thermData, 0);

    AlignResult res = auto_align_depth_aware((const uint8_t *)cam_data, (const uint8_t *)therm_data,
                                             cam_width, cam_height, therm_width, therm_height);

    env->ReleaseByteArrayElements(camData, cam_data, JNI_ABORT);
    env->ReleaseByteArrayElements(thermData, therm_data, JNI_ABORT);

    if (results != nullptr) {
        jsize len = env->GetArrayLength(results);
        if (len >= 6) {
            jfloat res_tab[6] = {
                res.dx,
                res.dy,
                res.scale,
                res.estimatedDistance,
                res.score,
                res.psr
            };
            env->SetFloatArrayRegion(results, 0, 6, res_tab);
        }
    }

    return (jint)res.status;
}

static thread_local sigjmp_buf g_safe_jmp_buf;
static thread_local volatile sig_atomic_t g_in_safe_section = 0;
static struct sigaction g_prev_sigsegv;
static struct sigaction g_prev_sigbus;
static std::once_flag g_sig_init_flag;

static void safe_signal_handler(int sig, siginfo_t *info, void *ucontext) {
    if (g_in_safe_section) {
        siglongjmp(g_safe_jmp_buf, 1);
    }
    if (sig == SIGSEGV && g_prev_sigsegv.sa_sigaction) {
        g_prev_sigsegv.sa_sigaction(sig, info, ucontext);
    } else if (sig == SIGBUS && g_prev_sigbus.sa_sigaction) {
        g_prev_sigbus.sa_sigaction(sig, info, ucontext);
    } else {
        signal(sig, SIG_DFL);
        raise(sig);
    }
}

static void init_signal_handlers() {
    struct sigaction sa;
    memset(&sa, 0, sizeof(sa));
    sa.sa_sigaction = safe_signal_handler;
    sa.sa_flags = SA_SIGINFO;
    sigemptyset(&sa.sa_mask);

    sigaction(SIGSEGV, &sa, &g_prev_sigsegv);
    sigaction(SIGBUS, &sa, &g_prev_sigbus);
}

extern "C"
JNIEXPORT void JNICALL
Java_com_example_thermaleyes_ImageFusion_yuyvToNv21(JNIEnv *env, jclass clazz,
                                                    jobject yuyvBuffer, jbyteArray nv21Data,
                                                    jint width, jint height) {
    if (!yuyvBuffer || !nv21Data || width <= 0 || height <= 0 || (width % 2 != 0) || (height % 2 != 0)) return;

    jlong src_cap = env->GetDirectBufferCapacity(yuyvBuffer);
    if (src_cap > 0 && src_cap < (jlong)width * height * 2) {
        return;
    }

    uint8_t *src = (uint8_t *)env->GetDirectBufferAddress(yuyvBuffer);
    if (!src) return;

    jsize dst_len = env->GetArrayLength(nv21Data);
    if (dst_len < width * height * 3 / 2) return;

    jbyte *dst = env->GetByteArrayElements(nv21Data, 0);
    if (!dst) return;

    uint8_t *y_plane = (uint8_t *)dst;
    uint8_t *vu_plane = (uint8_t *)dst + width * height;

    std::call_once(g_sig_init_flag, init_signal_handlers);

    g_in_safe_section = 1;
    if (sigsetjmp(g_safe_jmp_buf, 1) == 0) {
        for (int r = 0; r < height; ++r) {
            const uint8_t *row_src = src + r * (width * 2);
            uint8_t *row_y = y_plane + r * width;
            bool is_even_row = (r % 2 == 0);
            uint8_t *row_vu = vu_plane + (r / 2) * width;

            for (int c = 0; c < width; c += 2) {
                // YUYV: Y0 U0 Y1 V0
                row_y[c]     = row_src[c * 2];
                row_y[c + 1] = row_src[c * 2 + 2];
                if (is_even_row) {
                    row_vu[c]     = row_src[c * 2 + 3]; // V
                    row_vu[c + 1] = row_src[c * 2 + 1]; // U
                }
            }
        }
    } else {
        LOGW("yuyvToNv21: safely intercepted corrupted/truncated frame buffer without crash");
    }
    g_in_safe_section = 0;

    env->ReleaseByteArrayElements(nv21Data, dst, 0);
}

extern "C"
JNIEXPORT void JNICALL
Java_com_example_thermaleyes_ImageFusion_nativeSetIndustryMode(JNIEnv *env, jobject thiz,
                                                              jint mode, jint isotherm_thresh,
                                                              jfloat param) {
    g_image.industry_mode = mode;
    g_image.isotherm_threshold = (uint8_t)CLIP(isotherm_thresh, 0, 255);
    if (mode == 1) { // ⚡ Electrical: High-temp Isotherm
        g_image.enable_isotherm = true;
        g_image.isotherm_invert = false;
        g_image.enable_clahe = false;
        g_image.enable_diff = false;
    } else if (mode == 2) { // 🏢 Building / HVAC: Micro-contrast CLAHE
        g_image.enable_clahe = true;
        g_image.clahe_clip = param > 0.1f ? param : 3.5f;
        g_image.enable_isotherm = false;
        g_image.enable_diff = false;
    } else if (mode == 3) { // 💻 PCB: Baseline Difference
        g_image.enable_diff = true;
        g_image.diff_gain = param > 0.1f ? param : 2.5f;
        g_image.enable_isotherm = false;
        g_image.enable_clahe = false;
    } else if (mode == 4) { // 🩺 Medical: S-curve
        g_image.enable_isotherm = false;
        g_image.enable_clahe = false;
        g_image.enable_diff = false;
    } else { // ⚙️ Standard
        g_image.enable_isotherm = false;
        g_image.enable_clahe = false;
        g_image.enable_diff = false;
    }
    LOGI("nativeSetIndustryMode: mode=%d, isotherm_thresh=%d, param=%.2f", mode, isotherm_thresh, param);
}

extern "C"
JNIEXPORT void JNICALL
Java_com_example_thermaleyes_ImageFusion_nativeCaptureBaseline(JNIEnv *env, jobject thiz) {
    g_trigger_capture_baseline = true;
    LOGI("nativeCaptureBaseline: triggered");
}

extern "C"
JNIEXPORT void JNICALL
Java_com_example_thermaleyes_ImageFusion_nativeClearBaseline(JNIEnv *env, jobject thiz) {
    g_has_baseline = false;
    g_baseline_therm.release();
    LOGI("nativeClearBaseline: cleared");
}

extern "C"
JNIEXPORT jboolean JNICALL
Java_com_example_thermaleyes_ImageFusion_nativeHasBaseline(JNIEnv *env, jobject thiz) {
    return (jboolean)g_has_baseline;
}



