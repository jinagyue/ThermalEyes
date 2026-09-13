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

#include "alignment/CalibrationModel.h"
#include "alignment/ThermalGuidedAligner.h"
#include "alignment/PhysicsAlignment.h"

static int g_align_mode = ALIGN_MODE_TGA; // Default: 0 = TGA (Engineering Mode)

extern "C"
JNIEXPORT void JNICALL
Java_com_example_thermaleyes_ImageFusion_nativeSetAlignMode(JNIEnv *env, jobject thiz, jint mode) {
    if (mode == ALIGN_MODE_PCTVA) {
        g_align_mode = ALIGN_MODE_PCTVA;
        LOGI("nativeSetAlignMode: Set to RESEARCH (PCTVA)");
    } else {
        g_align_mode = ALIGN_MODE_TGA;
        LOGI("nativeSetAlignMode: Set to APP (TGA)");
    }
}

extern "C"
JNIEXPORT jint JNICALL
Java_com_example_thermaleyes_ImageFusion_nativeGetAlignMode(JNIEnv *env, jobject thiz) {
    return g_align_mode;
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

    AlignmentResult res;
    if (g_align_mode == ALIGN_MODE_PCTVA) {
        LOGI("nativeAutoCalibrate: Running PCTVA (Research Mode)...");
        res = PhysicsAlignment::align((const uint8_t *)cam_data, (const uint8_t *)therm_data,
                                      cam_width, cam_height, therm_width, therm_height);
    } else {
        LOGI("nativeAutoCalibrate: Running TGA (Engineering Mode)...");
        res = ThermalGuidedAligner::align((const uint8_t *)cam_data, (const uint8_t *)therm_data,
                                          cam_width, cam_height, therm_width, therm_height);
    }

    env->ReleaseByteArrayElements(camData, cam_data, JNI_ABORT);
    env->ReleaseByteArrayElements(thermData, therm_data, JNI_ABORT);

    if (res.success && res.status == ALIGN_OK) {
        // Apply directly to active runtime warp without corrupting static hardware calibration
        g_image.offset_x = (int)std::round(res.dx);
        g_image.offset_y = (int)std::round(res.dy);
        g_image.scale = res.scale <= 0.1f ? 1.0f : res.scale;
        g_image.mirror_x = false;
    }

    if (results != nullptr) {
        jsize len = env->GetArrayLength(results);
        if (len >= 6) {
            jfloat res_tab[6] = {
                res.dx,
                res.dy,
                res.scale,
                res.distance,
                res.confidence,
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



