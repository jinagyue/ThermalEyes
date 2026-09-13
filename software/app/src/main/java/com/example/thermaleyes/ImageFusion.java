package com.example.thermaleyes;

import android.graphics.Point;
import android.os.Build;
import android.util.Log;

import androidx.annotation.RequiresApi;

import java.nio.ByteBuffer;
import java.util.LinkedList;
import java.util.Map;

public abstract class ImageFusion extends Thread {
    private static final int QUEUE_LEN = 2;
    private volatile boolean mThreadRun = true;
    private final LinkedList<FrameInfo> mCameraQueue = new LinkedList<>();
    private final LinkedList<FrameInfo> mThermalQueue = new LinkedList<>();
    private static final String TAG = ImageFusion.class.getSimpleName();
    private final int mCamWidth;
    private final int mCamHeight;
    private final int mThermWidth;
    private final int mThermHeight;
    private final AlgorithmConfig mAlgoConfig;
    private final Object mFrameLock = new Object();
    private byte[] mLatestCamData;
    private byte[] mLatestThermData;
    private float mLatestMaxTemp = 0.0f;
    private float mLatestMinTemp = 0.0f;

    public static final int ALIGN_OK = 0;
    public static final int ALIGN_LOW_CONTRAST = 1;
    public static final int ALIGN_TARGET_CLIPPED = 2;
    public static final int ALIGN_NO_TARGET = 3;
    public static final int ALIGN_TOO_FEW_FEATURES = 3; // Backward-compatible alias
    public static final int ALIGN_AMBIGUOUS = 4;
    public static final int ALIGN_RANGE_LIMITED = 5;
    public static final int ALIGN_INTERNAL_ERROR = 6;

    // Dual-algorithm modes
    public static final int ALIGN_MODE_TGA = 0;   // 🚀 工程模式 (Thermal Guided Alignment, 快速稳定)
    public static final int ALIGN_MODE_PCTVA = 1; // 🧪 论文模式 (Physics-Constrained Thermal-Visible Alignment, 逆深度能量优化)

    public interface OnAutoCalibrateCallback {
        void onSuccess(int offsetX, int offsetY, float scale, float distance, float score);
        void onFailed(int status, String reason);
    }

    public static final int FUSION_MODE_FUSION = 0;
    public static final int FUSION_MODE_THERMAL = 1;
    public static final int FUSION_MODE_VISIBLE = 2;

    public static final int FUSION_MODE_COLOR_MAP = 0;
    public static final int FUSION_MODE_HIGH_FREQ_EXTRACT = 1;

    public static final int HIGH_FREQ_RATIO_LOW = 0;
    public static final int HIGH_FREQ_RATIO_MEDIUM = 1;
    public static final int HIGH_FREQ_RATIO_HIGH = 2;

    public static final int PSEUDO_COLOR_TAB_JET = 2;
    public static final int PSEUDO_COLOR_TAB_PLASMA = 15;

    public static final int INDUSTRY_STANDARD = 0;   // ⚙️ 通用工业
    public static final int INDUSTRY_ELECTRICAL = 1; // ⚡ 电力巡检 (超温等温线)
    public static final int INDUSTRY_BUILDING = 2;   // 🏢 建筑暖通 (小温差CLAHE)
    public static final int INDUSTRY_PCB = 3;        // 💻 电子排障 (基准差分)
    public static final int INDUSTRY_MEDICAL = 4;    // 🩺 医疗体温 (生理拉伸)

    private int mIndustryMode = INDUSTRY_STANDARD;
    private float mIsothermTemp = 50.0f;
    private float mIndustryParam = 0.0f;

    // Dynamic Runtime Alignment (decoupled from persistent static calibration)
    private volatile int mRuntimeOffsetX = 25;
    private volatile int mRuntimeOffsetY = -5;
    private volatile float mEstimatedDistance = 0.8f;

    // NV21
    public abstract void onFrame(FrameInfo frame);

    public ImageFusion(int camWidth, int camHeight, int thermWidth, int thermHeight) {
        mCamWidth = camWidth;
        mCamHeight = camHeight;
        mThermWidth = thermWidth;
        mThermHeight = thermHeight;
        mAlgoConfig = getConfig();
    }

    public void putCameraImage(FrameInfo frame) {
        if (frame == null || frame.data == null || frame.data.length != mCamWidth * mCamHeight * 3 / 2) {
            Log.e(TAG, "Invalid camera frame");
            return;
        }

        synchronized(mFrameLock) {
            if (mLatestCamData == null || mLatestCamData.length != frame.data.length) {
                mLatestCamData = new byte[frame.data.length];
            }
            System.arraycopy(frame.data, 0, mLatestCamData, 0, frame.data.length);
        }

        synchronized(mCameraQueue) {
            putImage(mCameraQueue, frame);
        }
    }

    public void putThermalImage(FrameInfo frame) {
        if (frame == null || frame.data == null || frame.data.length != mThermWidth * mThermHeight) {
            Log.e(TAG, "Invalid thermal frame");
            return;
        }

        synchronized(mFrameLock) {
            if (mLatestThermData == null || mLatestThermData.length != frame.data.length) {
                mLatestThermData = new byte[frame.data.length];
            }
            System.arraycopy(frame.data, 0, mLatestThermData, 0, frame.data.length);
            mLatestMaxTemp = frame.maxVal;
            mLatestMinTemp = frame.minVal;
        }

        synchronized(mThermalQueue) {
            putImage(mThermalQueue, frame);
        }
    }

    public void setCamYK(float camYK) {
        mAlgoConfig.camYK = camYK;
        setConfig(mAlgoConfig);
    }

    public float getCamYK() {
        return mAlgoConfig.camYK;
    }

    public void setCamUVK(float camUVK) {
        mAlgoConfig.camUVK = camUVK;
        setConfig(mAlgoConfig);
    }

    public float getCamUVK() {
        return mAlgoConfig.camUVK;
    }

    public void setThermYK(float thermYK) {
        mAlgoConfig.thermYK = thermYK;
        setConfig(mAlgoConfig);
    }

    public float getThermYK() {
        return mAlgoConfig.thermYK;
    }

    public void setThermUVK(float thermUVK) {
        mAlgoConfig.thermUVK = thermUVK;
        setConfig(mAlgoConfig);
    }

    public float getThermUVK() {
        return mAlgoConfig.thermUVK;
    }

    public void setMode(int mode) {
        mAlgoConfig.fusionMode = mode;
        setConfig(mAlgoConfig);
    }

    public int getMode() {
        return mAlgoConfig.fusionMode;
    }

    public void setHighFreqRatio(int ratio) {
        mAlgoConfig.highFreqRatio = ratio;
        setConfig(mAlgoConfig);
    }

    public int getHighFreqRatio() {
        return mAlgoConfig.highFreqRatio;
    }

    public void setColorTab(int colorTab) {
        mAlgoConfig.pseudoColorTab = colorTab;
        setConfig(mAlgoConfig);
    }

    public int getColorTab() {
        return mAlgoConfig.pseudoColorTab;
    }

    public void setParallaxOffset(int offset) {
        mAlgoConfig.parallaxOffset = offset;
        mAlgoConfig.offsetY = offset;
        setConfig(mAlgoConfig);
    }

    public int getParallaxOffset() {
        return mAlgoConfig.parallaxOffset;
    }

    public void setCalibration(int offsetX, int offsetY, float scale, float rotation) {
        mAlgoConfig.offsetX = offsetX;
        mAlgoConfig.offsetY = offsetY;
        mAlgoConfig.scale = scale;
        mAlgoConfig.rotation = rotation;
        mAlgoConfig.parallaxOffset = offsetY > 0 ? offsetY : 0;
        mRuntimeOffsetX = offsetX;
        mRuntimeOffsetY = offsetY;
        setCalibrationParams(offsetX, offsetY, scale, rotation);
    }

    public void setRuntimeAlignment(int runtimeDx, int runtimeDy, float distance) {
        mRuntimeOffsetX = runtimeDx;
        mRuntimeOffsetY = runtimeDy;
        mEstimatedDistance = distance;
        setCalibrationParams(runtimeDx, runtimeDy, mAlgoConfig.scale, mAlgoConfig.rotation);
    }

    public int getRuntimeOffsetX() {
        return mRuntimeOffsetX;
    }

    public int getRuntimeOffsetY() {
        return mRuntimeOffsetY;
    }

    public float getEstimatedDistance() {
        return mEstimatedDistance;
    }

    public int getOffsetX() {
        return mAlgoConfig.offsetX;
    }

    public int getOffsetY() {
        return mAlgoConfig.offsetY;
    }

    public float getScale() {
        return mAlgoConfig.scale;
    }

    public float getRotation() {
        return mAlgoConfig.rotation;
    }

    private void setConfig(AlgorithmConfig config) {
        setFusionMode(config.fusionMode);               // RadioButton
        setFusionHighFreqRatio(config.highFreqRatio);   // SeekBar
        setFusionColorTab(config.pseudoColorTab);       // RadioButton
        setFusionParallaxOffset(config.parallaxOffset); // SeekBar
        setCalibrationParams(config.offsetX, config.offsetY, config.scale, config.rotation);

        float[] paramTab = { config.camYK, config.camUVK, config.thermYK, config.thermUVK };
        setFusionParams(paramTab);                      // SeekBar
    }

    private AlgorithmConfig getConfig() {
        AlgorithmConfig config = new AlgorithmConfig();

        config.fusionMode = getFusionMode();
        config.highFreqRatio = getFusionHighFreqRatio();
        config.pseudoColorTab = getFusionColorTab();
        config.parallaxOffset = getFusionParallaxOffset();

        float[] calibParams = new float[4];
        getCalibrationParams(calibParams);
        config.offsetX = (int) calibParams[0];
        config.offsetY = (int) calibParams[1];
        config.scale = calibParams[2] <= 0.1f ? 1.0f : calibParams[2];
        config.rotation = calibParams[3];

        float[] paramTab = new float[4];
        getFusionParams(paramTab);
        config.camYK = paramTab[0];
        config.camUVK = paramTab[1];
        config.thermYK = paramTab[2];
        config.thermUVK = paramTab[3];

        return config;
    }

    public void resetConfig() {

    }

    private Point mapThermalPointToCamera(Point pt, int thermW, int thermH, int camW, int camH,
                                          int offsetX, int offsetY, float scale, float rotation) {
        float x0 = pt.x * ((float) camW / thermW);
        float y0 = pt.y * ((float) camH / thermH);
        float cx = camW / 2.0f;
        float cy = camH / 2.0f;
        double rad = Math.toRadians(rotation);
        double cos = Math.cos(rad);
        double sin = Math.sin(rad);
        double alpha = (scale <= 0.1f ? 1.0f : scale) * cos;
        double beta = (scale <= 0.1f ? 1.0f : scale) * sin;
        double dx = x0 - cx;
        double dy = y0 - cy;
        int xAligned = (int) Math.round(cx + alpha * dx + beta * dy + offsetX);
        int yAligned = (int) Math.round(cy - beta * dx + alpha * dy + offsetY);
        return new Point(xAligned, yAligned);
    }

    @RequiresApi(api = Build.VERSION_CODES.O)
    @Override
    public void run() {
        FrameInfo cameraFrame = null, thermalFrame = null;

        while (mThreadRun) {
            synchronized(mCameraQueue) {
                cameraFrame = getImage(mCameraQueue);
            }

            if (cameraFrame == null || cameraFrame.data == null) {
                continue;
            }

            synchronized(mThermalQueue) {
                if (!mThermalQueue.isEmpty()) {
                    thermalFrame = mThermalQueue.poll();
                } else if (thermalFrame == null) {
                    thermalFrame = getImage(mThermalQueue);
                }
            }

            if (thermalFrame == null || thermalFrame.data == null) {
                continue;
            }

            FrameInfo fusionFrame = new FrameInfo();
            fusionFrame.data = new byte[cameraFrame.data.length];
            fusionFrame.width = cameraFrame.width;
            fusionFrame.height = cameraFrame.height;
            fusionFrame.maxVal = thermalFrame.maxVal;
            fusionFrame.minVal = thermalFrame.minVal;
            fusionFrame.centerVal = thermalFrame.centerVal;

            fusionFrame.maxLoc = mapThermalPointToCamera(thermalFrame.maxLoc,
                    thermalFrame.width, thermalFrame.height,
                    cameraFrame.width, cameraFrame.height,
                    mRuntimeOffsetX, mRuntimeOffsetY,
                    mAlgoConfig.scale, mAlgoConfig.rotation);

            fusionFrame.minLoc = mapThermalPointToCamera(thermalFrame.minLoc,
                    thermalFrame.width, thermalFrame.height,
                    cameraFrame.width, cameraFrame.height,
                    mRuntimeOffsetX, mRuntimeOffsetY,
                    mAlgoConfig.scale, mAlgoConfig.rotation);

            Point rawCenter = (thermalFrame.centerLoc != null)
                    ? thermalFrame.centerLoc
                    : new Point(thermalFrame.width / 2, thermalFrame.height / 2);
            fusionFrame.centerLoc = mapThermalPointToCamera(rawCenter,
                    thermalFrame.width, thermalFrame.height,
                    cameraFrame.width, cameraFrame.height,
                    mRuntimeOffsetX, mRuntimeOffsetY,
                    mAlgoConfig.scale, mAlgoConfig.rotation);

            updateIndustryNative();
            getFusionImage(fusionFrame.data, cameraFrame.data, thermalFrame.data,
                    mCamWidth, mCamHeight, mThermWidth, mThermHeight);

            onFrame(fusionFrame);
        }
    }

    public void autoCalibrate(OnAutoCalibrateCallback callback) {
        new Thread(() -> {
            byte[] camSnap = null;
            byte[] thermSnap = null;
            float maxTemp = 0.0f;
            float minTemp = 0.0f;
            synchronized(mFrameLock) {
                if (mLatestCamData != null && mLatestThermData != null) {
                    camSnap = mLatestCamData.clone();
                    thermSnap = mLatestThermData.clone();
                    maxTemp = mLatestMaxTemp;
                    minTemp = mLatestMinTemp;
                }
            }

            if (camSnap == null || thermSnap == null) {
                if (callback != null) {
                    callback.onFailed(ALIGN_INTERNAL_ERROR, "尚未接收到相机或热成像完整数据，请稍候");
                }
                return;
            }

            float deltaT = maxTemp - minTemp;
            if (deltaT > 0 && deltaT < 2.5f) {
                if (callback != null) {
                    callback.onFailed(ALIGN_LOW_CONTRAST, String.format(java.util.Locale.CHINA,
                            "当前目标温差过小(仅 %.1f℃)，请将手掌靠近镜头或使用热水杯", deltaT));
                }
                return;
            }

            float[] results = new float[6];
            int status = nativeAutoCalibrate(camSnap, thermSnap, mCamWidth, mCamHeight,
                    mThermWidth, mThermHeight, results);
            if (status == ALIGN_OK) {
                int ox = Math.round(results[0]);
                int oy = Math.round(results[1]);
                float sc = results[2];
                float distance = results[3];
                float score = results[4];

                // Decoupled: Update dynamic runtime alignment without corrupting persistent static hardware calibration
                setRuntimeAlignment(ox, oy, distance);

                if (callback != null) {
                    callback.onSuccess(ox, oy, sc, distance, score);
                }
            } else {
                String reason;
                switch (status) {
                    case ALIGN_LOW_CONTRAST:
                        reason = "目标温差过小，请对准具有明显温差的目标（如手掌或温水杯）";
                        break;
                    case ALIGN_TARGET_CLIPPED:
                        reason = "目标过近，请稍微后移，使目标完整进入画面";
                        break;
                    case ALIGN_NO_TARGET:
                        reason = "未检测到有效热目标，请将目标（手掌/温水杯）置于视野中央";
                        break;
                    case ALIGN_AMBIGUOUS:
                        reason = "背景结构过于复杂，请将目标置于中央区域后重试";
                        break;
                    case ALIGN_RANGE_LIMITED:
                        reason = "目标距离超出可靠对齐范围(0.2~2.5m)，请调整物距后重试";
                        break;
                    default:
                        reason = "光轴自动配准未收敛，请微调目标位置后重试";
                        break;
                }
                if (callback != null) {
                    callback.onFailed(status, reason);
                }
            }
        }).start();
    }

    public void exit() {
        mThreadRun = false;
        synchronized(mCameraQueue) {
            mCameraQueue.notifyAll();
        }
        synchronized(mThermalQueue) {
            mThermalQueue.notifyAll();
        }
    }

    private void putImage(LinkedList<FrameInfo> queue, FrameInfo frame) {
        if (queue.size() >= QUEUE_LEN) {
            queue.poll();
        }
        queue.offer(frame);
        queue.notifyAll();
    }

    private FrameInfo getImage(LinkedList<FrameInfo> queue) {
        synchronized(queue) {
            while (queue.size() == 0 && mThreadRun) {
                try {
                    queue.wait(200);
                } catch (InterruptedException e) {
                    Log.e(TAG, "Queue wait interrupt");
                }
            }

            return queue.poll();
        }
    }

    private class AlgorithmConfig {
        public int fusionMode;
        public int highFreqRatio;
        public int pseudoColorTab;
        public int parallaxOffset;
        public int offsetX = 25;
        public int offsetY = -5;
        public float scale = 1.0f;
        public float rotation = 0.0f;
        public float camYK;
        public float camUVK;
        public float thermYK;
        public float thermUVK;
    }

    static {
        System.loadLibrary("img_algo");
    }

    private native void getFusionImage(byte[] fusionData, byte[] camData, byte[] thermData,
                                       int camWidth, int camHeight, int thermWidth, int thermHeight);
    private native void setFusionMode(int fusionMode);
    private native int getFusionMode();
    private native void setFusionHighFreqRatio(int highFreqRatio);
    private native int getFusionHighFreqRatio();
    private native void setFusionColorTab(int colorTab);
    private native int getFusionColorTab();
    private native void setFusionParallaxOffset(int offset);
    private native int getFusionParallaxOffset();
    private native void setCalibrationParams(int offsetX, int offsetY, float scale, float rotation);
    private native void getCalibrationParams(float[] params);
    private native void setFusionParams(float[] params);
    private native void getFusionParams(float[] params);
    private native void nativeSetMirror(boolean mirrorX, boolean mirrorY);
    private native boolean nativeGetMirrorX();
    private native boolean nativeGetMirrorY();
    private native void nativeSetAlignMode(int mode);
    private native int nativeGetAlignMode();
    private native int nativeAutoCalibrate(byte[] camData, byte[] thermData,
                                           int camWidth, int camHeight,
                                           int thermWidth, int thermHeight,
                                           float[] results);

    public void setAlignMode(int mode) {
        nativeSetAlignMode(mode);
    }

    public int getAlignMode() {
        return nativeGetAlignMode();
    }

    private volatile boolean mMirrorX = false;
    private volatile boolean mMirrorY = false;

    public void setMirror(boolean mirrorX, boolean mirrorY) {
        mMirrorX = mirrorX;
        mMirrorY = mirrorY;
        nativeSetMirror(mirrorX, mirrorY);
    }

    public boolean isMirrorX() {
        return mMirrorX;
    }

    public boolean isMirrorY() {
        return mMirrorY;
    }

    public void setMirrorX(boolean mirrorX) {
        setMirror(mirrorX, mMirrorY);
    }

    public void setMirrorY(boolean mirrorY) {
        setMirror(mMirrorX, mirrorY);
    }
    public void setIndustryMode(int mode, float isothermTemp, float param) {
        mIndustryMode = mode;
        mIsothermTemp = isothermTemp;
        mIndustryParam = param;
        updateIndustryNative();
    }

    public int getIndustryMode() {
        return mIndustryMode;
    }

    public void setIsothermTemp(float temp) {
        mIsothermTemp = temp;
        updateIndustryNative();
    }

    public float getIsothermTemp() {
        return mIsothermTemp;
    }

    public void captureBaseline() {
        nativeCaptureBaseline();
    }

    public void clearBaseline() {
        nativeClearBaseline();
    }

    public boolean hasBaseline() {
        return nativeHasBaseline();
    }

    private int mLastSentMode = -1;
    private int mLastSentThresh = -1;
    private float mLastSentParam = -1f;

    public void updateIndustryNative() {
        int byteThresh = 128;
        float diff = mLatestMaxTemp - mLatestMinTemp;
        if (diff > 0.5f) {
            float ratio = (mIsothermTemp - mLatestMinTemp) / diff;
            byteThresh = Math.round(Math.max(0f, Math.min(1f, ratio)) * 255f);
        }
        if (mIndustryMode != mLastSentMode || byteThresh != mLastSentThresh || Math.abs(mIndustryParam - mLastSentParam) > 0.01f) {
            mLastSentMode = mIndustryMode;
            mLastSentThresh = byteThresh;
            mLastSentParam = mIndustryParam;
            nativeSetIndustryMode(mIndustryMode, byteThresh, mIndustryParam);
        }
    }

    private native void nativeSetIndustryMode(int mode, int isothermThresh, float param);
    private native void nativeCaptureBaseline();
    private native void nativeClearBaseline();
    private native boolean nativeHasBaseline();

    public static native void yuyvToNv21(ByteBuffer yuyvBuffer, byte[] nv21Data, int width, int height);
}
