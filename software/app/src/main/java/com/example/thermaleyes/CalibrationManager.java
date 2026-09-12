package com.example.thermaleyes;

import android.content.Context;
import android.content.SharedPreferences;

public class CalibrationManager {
    private static final String PREF_NAME = "thermal_calibration_prefs";

    public static final String KEY_OFFSET_X = "calib_offset_x";
    public static final String KEY_OFFSET_Y = "calib_offset_y";
    public static final String KEY_SCALE = "calib_scale";
    public static final String KEY_ROTATION = "calib_rotation";
    public static final String KEY_HIGH_FREQ = "calib_high_freq";
    public static final String KEY_COLOR_TAB = "calib_color_tab";
    public static final String KEY_FUSION_MODE = "calib_fusion_mode";
    public static final String KEY_FPS = "calib_fps";
    public static final String KEY_INDUSTRY_MODE = "calib_industry_mode";
    public static final String KEY_ISOTHERM_TEMP = "calib_isotherm_temp";
    public static final String KEY_MIRROR_X = "calib_mirror_x";
    public static final String KEY_MIRROR_Y = "calib_mirror_y";

    // Defaults
    public static final int DEFAULT_OFFSET_X = 80;
    public static final int DEFAULT_OFFSET_Y = 15;
    public static final float DEFAULT_SCALE = 1.0f;
    public static final float DEFAULT_ROTATION = 0.0f;
    public static final int DEFAULT_HIGH_FREQ = ImageFusion.HIGH_FREQ_RATIO_MEDIUM;
    public static final int DEFAULT_COLOR_TAB = ImageFusion.PSEUDO_COLOR_TAB_PLASMA;
    public static final int DEFAULT_FUSION_MODE = ImageFusion.FUSION_MODE_COLOR_MAP;
    public static final int DEFAULT_FPS = ThermalDevice.FPS_8;
    public static final int DEFAULT_INDUSTRY_MODE = ImageFusion.INDUSTRY_STANDARD;
    public static final float DEFAULT_ISOTHERM_TEMP = 50.0f;
    public static final boolean DEFAULT_MIRROR_X = false;
    public static final boolean DEFAULT_MIRROR_Y = false;

    public static class CalibrationData {
        public int offsetX = DEFAULT_OFFSET_X;
        public int offsetY = DEFAULT_OFFSET_Y;
        public float scale = DEFAULT_SCALE;
        public float rotation = DEFAULT_ROTATION;
        public int highFreq = DEFAULT_HIGH_FREQ;
        public int colorTab = DEFAULT_COLOR_TAB;
        public int fusionMode = DEFAULT_FUSION_MODE;
        public int fps = DEFAULT_FPS;
        public int industryMode = DEFAULT_INDUSTRY_MODE;
        public float isothermTemp = DEFAULT_ISOTHERM_TEMP;
        public boolean mirrorX = DEFAULT_MIRROR_X;
        public boolean mirrorY = DEFAULT_MIRROR_Y;
    }

    public static CalibrationData load(Context context) {
        CalibrationData data = new CalibrationData();
        SharedPreferences sp = context.getSharedPreferences(PREF_NAME, Context.MODE_PRIVATE);
        data.offsetX = sp.getInt(KEY_OFFSET_X, DEFAULT_OFFSET_X);
        data.offsetY = sp.getInt(KEY_OFFSET_Y, DEFAULT_OFFSET_Y);
        data.scale = sp.getFloat(KEY_SCALE, DEFAULT_SCALE);
        data.rotation = sp.getFloat(KEY_ROTATION, DEFAULT_ROTATION);
        data.highFreq = sp.getInt(KEY_HIGH_FREQ, DEFAULT_HIGH_FREQ);
        data.colorTab = sp.getInt(KEY_COLOR_TAB, DEFAULT_COLOR_TAB);
        data.fusionMode = sp.getInt(KEY_FUSION_MODE, DEFAULT_FUSION_MODE);
        data.fps = sp.getInt(KEY_FPS, DEFAULT_FPS);
        data.industryMode = sp.getInt(KEY_INDUSTRY_MODE, DEFAULT_INDUSTRY_MODE);
        data.isothermTemp = sp.getFloat(KEY_ISOTHERM_TEMP, DEFAULT_ISOTHERM_TEMP);
        data.mirrorX = sp.getBoolean(KEY_MIRROR_X, DEFAULT_MIRROR_X);
        data.mirrorY = sp.getBoolean(KEY_MIRROR_Y, DEFAULT_MIRROR_Y);
        return data;
    }

    public static void save(Context context, CalibrationData data) {
        SharedPreferences sp = context.getSharedPreferences(PREF_NAME, Context.MODE_PRIVATE);
        sp.edit()
                .putInt(KEY_OFFSET_X, data.offsetX)
                .putInt(KEY_OFFSET_Y, data.offsetY)
                .putFloat(KEY_SCALE, data.scale)
                .putFloat(KEY_ROTATION, data.rotation)
                .putInt(KEY_HIGH_FREQ, data.highFreq)
                .putInt(KEY_COLOR_TAB, data.colorTab)
                .putInt(KEY_FUSION_MODE, data.fusionMode)
                .putInt(KEY_FPS, data.fps)
                .putInt(KEY_INDUSTRY_MODE, data.industryMode)
                .putFloat(KEY_ISOTHERM_TEMP, data.isothermTemp)
                .putBoolean(KEY_MIRROR_X, data.mirrorX)
                .putBoolean(KEY_MIRROR_Y, data.mirrorY)
                .apply();
    }

    public static void reset(Context context) {
        SharedPreferences sp = context.getSharedPreferences(PREF_NAME, Context.MODE_PRIVATE);
        sp.edit().clear().apply();
    }
}
