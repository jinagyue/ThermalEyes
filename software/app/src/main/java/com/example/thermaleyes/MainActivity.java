package com.example.thermaleyes;

import androidx.annotation.RequiresApi;
import androidx.appcompat.app.AppCompatActivity;

import android.Manifest;
import android.content.BroadcastReceiver;
import android.content.Context;
import android.content.ContentValues;
import android.content.Intent;
import android.content.IntentFilter;
import android.graphics.Bitmap;
import android.graphics.Canvas;
import android.graphics.Color;
import android.graphics.LinearGradient;
import android.graphics.Matrix;
import android.graphics.Paint;
import android.graphics.Point;
import android.graphics.Rect;
import android.graphics.RectF;
import android.graphics.Shader;
import android.graphics.Typeface;
import android.hardware.usb.UsbDevice;
import android.hardware.usb.UsbManager;
import android.net.Uri;
import android.os.Build;
import android.os.Bundle;
import android.os.Environment;
import android.os.VibrationEffect;
import android.os.Vibrator;
import android.provider.MediaStore;
import android.util.DisplayMetrics;
import android.util.Log;
import java.io.OutputStream;
import java.util.Locale;
import android.view.Menu;
import android.view.MenuItem;
import android.view.View;
import android.widget.Button;
import android.widget.ImageView;
import android.widget.SeekBar;
import android.widget.TextView;
import android.widget.Toast;

import com.herohan.uvcapp.CameraHelper;
import com.herohan.uvcapp.ICameraHelper;
import com.hjq.permissions.XXPermissions;
import com.serenegiant.usb.Size;
import com.serenegiant.usb.UVCCamera;

import java.io.IOException;
import java.nio.ByteBuffer;
import java.util.ArrayList;
import java.util.HashMap;
import java.util.List;

@RequiresApi(api = Build.VERSION_CODES.O)
public class MainActivity extends AppCompatActivity implements View.OnClickListener {
    private static final boolean CAM_DISPLAY = true;
    private static final boolean TEMP_DISPLAY = true;
    private static final String TAG = MainActivity.class.getSimpleName();

    private static final int DEFAULT_WIDTH = 640;
    private static final int DEFAULT_HEIGHT = 480;

    private ICameraHelper mCameraHelper;
    private ImageView mFusionImagePreview;
    private TextView mMaxTempTestView, mCenterTempTextView, mMinTempTestView, mDiffTempTextView, mConnectTextView;
    private View mDeviceOfflineLayout;
    private SeekBar mSbQuickFusionBlend;
    private TextView mTvQuickBlendRatio;

    private TextView mBtnIndStandard, mBtnIndElectrical, mBtnIndBuilding, mBtnIndPcb, mBtnIndMedical;
    private View mPcbActionStrip;
    private TextView mTvPcbStatus;
    private TextView mBtnPcbCapture, mBtnPcbClear;
    private int mCurrentIndustryMode = ImageFusion.INDUSTRY_STANDARD;
    private float mIsothermAlarmTemp = 50.0f;

    private TextView mTargetStatusTextView;
    private TextView mEmissivityTextView;
    private TextView mBtnModeFusion, mBtnModeThermal, mBtnModeVisible, mBtnQuickPalette;
    private View mQuickAlignButton, mShutterButton, mQuickSettingsButton;
    private TextView mBtnQuickMirror;
    private ThermalTrendView mThermalTrendView;

    private volatile Bitmap mLatestFusedBitmap = null;
    private int mCurrentColorTab = 0; // 0: PLASMA, 1: JET
    private int mCurrentMode = 0; // 0: Fusion, 1: Thermal, 2: Visible
    private int mEmissivityIndex = 0;
    private float mCurrentEmissivity = 0.95f;
    private final float[] EMISSIVITY_VALUES = { 0.95f, 0.98f, 0.85f, 0.30f };
    private final String[] EMISSIVITY_LABELS = { "0.95 哑光", "0.98 人体", "0.85 橡胶", "0.30 金属" };

    private NV21ToBitmap mNv21ToBitmap;
    private ParameterDialogFragment mControlsDialog;
    private boolean mIsCameraConnected = false;
    private volatile boolean mIsCameraOpened = false;
    private ImageFusion mImageFusion;

    private final ThermalDevice mThermalDevice = new ThermalDevice() {
        @Override
        public void onFrame(ByteBuffer frame, float maxTemp, float minTemp, float centerTemp, int maxLoc, int minLoc) {
            ImageFusion fusion = mImageFusion;
            if (fusion == null || frame == null) {
                return;
            }
            try {
                int remaining = frame.remaining();
                if (remaining <= 0) return;

                FrameInfo thermInfo = new FrameInfo();
                thermInfo.data = new byte[remaining];
                thermInfo.width = ThermalDevice.IMAGE_WIDTH;
                thermInfo.height = ThermalDevice.IMAGE_HEIGHT;
                thermInfo.maxVal = maxTemp;
                thermInfo.minVal = minTemp;
                thermInfo.centerVal = centerTemp;
                /* Thermal location horizontally flipped to match hardware sensor orientation */
                int mx = (ThermalDevice.IMAGE_WIDTH - 1) - (maxLoc % ThermalDevice.IMAGE_WIDTH);
                int my = maxLoc / ThermalDevice.IMAGE_WIDTH;
                int nx = (ThermalDevice.IMAGE_WIDTH - 1) - (minLoc % ThermalDevice.IMAGE_WIDTH);
                int ny = minLoc / ThermalDevice.IMAGE_WIDTH;
                thermInfo.maxLoc = new Point(mx, my);
                thermInfo.minLoc = new Point(nx, ny);
                thermInfo.centerLoc = new Point(ThermalDevice.IMAGE_WIDTH / 2, ThermalDevice.IMAGE_HEIGHT / 2);
                frame.get(thermInfo.data);

                fusion.putThermalImage(thermInfo);

                if (TEMP_DISPLAY) {
                    runOnUiThread(() -> {
                        String maxStr = String.format(Locale.getDefault(), "%.1f ℃", maxTemp);
                        String centerStr = String.format(Locale.getDefault(), "%.1f ℃", centerTemp);
                        String minStr = String.format(Locale.getDefault(), "%.1f ℃", minTemp);
                        String diffStr = String.format(Locale.getDefault(), "%.1f ℃", maxTemp - minTemp);

                        if (mMaxTempTestView != null) mMaxTempTestView.setText(maxStr);
                        if (mCenterTempTextView != null) mCenterTempTextView.setText(centerStr);
                        if (mMinTempTestView != null) mMinTempTestView.setText(minStr);
                        if (mDiffTempTextView != null) mDiffTempTextView.setText("ΔT: " + diffStr);

                        if (mThermalTrendView != null) {
                            mThermalTrendView.addTemperaturePoint(maxTemp, centerTemp, minTemp);
                        }

                        if (mTargetStatusTextView != null) {
                            float diff = maxTemp - minTemp;
                            ImageFusion currentFusion = mImageFusion;
                            if (mCurrentIndustryMode == ImageFusion.INDUSTRY_ELECTRICAL && maxTemp >= mIsothermAlarmTemp) {
                                mTargetStatusTextView.setText("⚡ 超温警报");
                                mTargetStatusTextView.setTextColor(Color.parseColor("#FF6B4A"));
                            } else if (mCurrentIndustryMode == ImageFusion.INDUSTRY_MEDICAL && maxTemp >= 37.3f) {
                                mTargetStatusTextView.setText("🚨 发烧预警");
                                mTargetStatusTextView.setTextColor(Color.parseColor("#FF6B4A"));
                            } else if (mCurrentIndustryMode == ImageFusion.INDUSTRY_PCB && currentFusion != null && currentFusion.hasBaseline()) {
                                mTargetStatusTextView.setText("💻 差分对比中");
                                mTargetStatusTextView.setTextColor(Color.parseColor("#38BDF8"));
                            } else if (maxTemp > 50.0f) {
                                mTargetStatusTextView.setText("高温预警");
                                mTargetStatusTextView.setTextColor(Color.parseColor("#FF6B4A"));
                            } else if (diff > 20.0f) {
                                mTargetStatusTextView.setText("高对比度");
                                mTargetStatusTextView.setTextColor(Color.parseColor("#FF6B4A"));
                            } else {
                                mTargetStatusTextView.setText("温差正常");
                                mTargetStatusTextView.setTextColor(Color.parseColor("#00D2A0"));
                            }
                        }
                    });
                }
            } catch (Throwable t) {
                Log.e(TAG, "Thermal onFrame error: " + t.getMessage());
            }
        }
    };

    private boolean mHudDiffDiffTemp() { return true; }

    @Override
    protected void onCreate(Bundle savedInstanceState) {
        super.onCreate(savedInstanceState);
        setContentView(R.layout.activity_main);
        initViews();

        List<String> needPermissions = new ArrayList<>();
        needPermissions.add(Manifest.permission.CAMERA);

        XXPermissions.with(this)
                .permission(needPermissions)
                .request((permissions, all) -> {
                    if (!all) {
                        Log.w(TAG, "Camera permission denied by user");
                        return;
                    }
                    Log.i(TAG, "Camera permission granted");
                    checkAndConnectDevice();
                });

        mNv21ToBitmap = new NV21ToBitmap(this);
        mThermalDevice.setUsbManager((UsbManager)getSystemService(USB_SERVICE));

        BroadcastReceiver cmdReceiver = new BroadcastReceiver() {
            @Override
            public void onReceive(Context context, Intent intent) {
                if (intent == null) return;
                String cmd = intent.getStringExtra("cmd");
                Log.i(TAG, "Received test command: " + cmd);
                if ("auto_align".equals(cmd)) {
                    if (mQuickAlignButton != null) mQuickAlignButton.performClick();
                } else if ("toggle_mirror".equals(cmd)) {
                    if (mBtnQuickMirror != null) mBtnQuickMirror.performClick();
                } else if ("toggle_palette".equals(cmd)) {
                    if (mBtnQuickPalette != null) mBtnQuickPalette.performClick();
                }
            }
        };
        if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.TIRAMISU) {
            registerReceiver(cmdReceiver, new IntentFilter("com.example.thermaleyes.CMD"), Context.RECEIVER_EXPORTED);
        } else {
            registerReceiver(cmdReceiver, new IntentFilter("com.example.thermaleyes.CMD"));
        }
    }

    private void drawTemptationTrack(Bitmap bitmap, FrameInfo frame) {
        if (bitmap == null || frame == null || frame.width <= 0 || frame.height <= 0) return;
        Canvas canvas = new Canvas(bitmap);

        float xScale = (float) bitmap.getWidth() / frame.width;
        float yScale = (float) bitmap.getHeight() / frame.height;

        Paint reticlePaint = new Paint(Paint.ANTI_ALIAS_FLAG);
        reticlePaint.setStyle(Paint.Style.STROKE);
        reticlePaint.setStrokeWidth(2.5f);

        Paint badgePaint = new Paint(Paint.ANTI_ALIAS_FLAG);
        badgePaint.setStyle(Paint.Style.FILL);
        badgePaint.setColor(Color.parseColor("#B30E1116"));

        Paint badgeStroke = new Paint(Paint.ANTI_ALIAS_FLAG);
        badgeStroke.setStyle(Paint.Style.STROKE);
        badgeStroke.setStrokeWidth(1.2f);

        Paint textPaint = new Paint(Paint.ANTI_ALIAS_FLAG);
        textPaint.setTypeface(Typeface.create("sans-serif-medium", Typeface.BOLD));
        textPaint.setTextSize(22f);

        // 1. Max & Min temperature reticles
        Point[] tempCoordinate = { frame.maxLoc, frame.minLoc };
        for (int i = 0; i < tempCoordinate.length; i++) {
            if (tempCoordinate[i] == null) continue;
            int x = (int) (tempCoordinate[i].x * xScale);
            int y = (int) (tempCoordinate[i].y * yScale);

            int color = (i == 0) ? Color.parseColor("#FF6B4A") : Color.parseColor("#38BDF8");
            reticlePaint.setColor(color);
            badgeStroke.setColor(color);
            textPaint.setColor(color);

            int crossLen = 14;
            int crossGap = 4;
            int boxR = 18;
            int bracketLen = 6;

            canvas.drawLine(x - crossLen, y, x - crossGap, y, reticlePaint);
            canvas.drawLine(x + crossGap, y, x + crossLen, y, reticlePaint);
            canvas.drawLine(x, y - crossLen, x, y - crossGap, reticlePaint);
            canvas.drawLine(x, y + crossGap, x, y + crossLen, reticlePaint);

            canvas.drawLine(x - boxR, y - boxR, x - boxR + bracketLen, y - boxR, reticlePaint);
            canvas.drawLine(x - boxR, y - boxR, x - boxR, y - boxR + bracketLen, reticlePaint);
            canvas.drawLine(x + boxR, y - boxR, x + boxR - bracketLen, y - boxR, reticlePaint);
            canvas.drawLine(x + boxR, y - boxR, x + boxR, y - boxR + bracketLen, reticlePaint);
            canvas.drawLine(x - boxR, y + boxR, x - boxR + bracketLen, y + boxR, reticlePaint);
            canvas.drawLine(x - boxR, y + boxR, x - boxR, y + boxR - bracketLen, reticlePaint);
            canvas.drawLine(x + boxR, y + boxR, x + boxR - bracketLen, y + boxR, reticlePaint);
            canvas.drawLine(x + boxR, y + boxR, x + boxR, y + boxR - bracketLen, reticlePaint);

            float tempVal = (i == 0) ? frame.maxVal : frame.minVal;
            String label = (i == 0) ? String.format(Locale.getDefault(), "▲ %.1f℃", tempVal)
                                    : String.format(Locale.getDefault(), "▼ %.1f℃", tempVal);
            float textWidth = textPaint.measureText(label);
            Paint.FontMetrics fm = textPaint.getFontMetrics();
            float textHeight = fm.descent - fm.ascent;

            float badgeX = x + boxR + 6;
            float badgeY = (i == 0) ? (y - boxR + 4) : (y + 4);

            if (badgeX + textWidth + 14 > bitmap.getWidth()) {
                badgeX = x - boxR - textWidth - 18;
            }
            if (badgeY - textHeight < 4) {
                badgeY = textHeight + 6;
            } else if (badgeY + 8 > bitmap.getHeight()) {
                badgeY = bitmap.getHeight() - 10;
            }

            RectF badgeRect = new RectF(badgeX, badgeY - textHeight + 2, badgeX + textWidth + 12, badgeY + 6);
            canvas.drawRoundRect(badgeRect, 6f, 6f, badgePaint);
            canvas.drawRoundRect(badgeRect, 6f, 6f, badgeStroke);
            canvas.drawText(label, badgeX + 6, badgeY - 2, textPaint);
        }

        // 2. Center Point Spot Metering Reticle (mapped with consistent thermal-to-camera transformation)
        if (frame.centerVal > -50f && frame.centerVal < 150f) {
            int cx = (frame.centerLoc != null) ? (int) (frame.centerLoc.x * xScale) : (bitmap.getWidth() / 2);
            int cy = (frame.centerLoc != null) ? (int) (frame.centerLoc.y * yScale) : (bitmap.getHeight() / 2);
            int tealColor = Color.parseColor("#00D2A0");
            reticlePaint.setColor(tealColor);
            badgeStroke.setColor(tealColor);
            textPaint.setColor(tealColor);

            int crossLen = 16;
            int crossGap = 5;
            canvas.drawLine(cx - crossLen, cy, cx - crossGap, cy, reticlePaint);
            canvas.drawLine(cx + crossGap, cy, cx + crossLen, cy, reticlePaint);
            canvas.drawLine(cx, cy - crossLen, cx, cy - crossGap, reticlePaint);
            canvas.drawLine(cx, cy + crossGap, cx, cy + crossLen, reticlePaint);

            String centerLabel = String.format(Locale.getDefault(), "+ %.1f℃", frame.centerVal);
            float cTextW = textPaint.measureText(centerLabel);
            Paint.FontMetrics cfm = textPaint.getFontMetrics();
            float cTextH = cfm.descent - cfm.ascent;
            float cBadgeX = cx + crossGap + 4;
            float cBadgeY = cy - crossGap - 2;
            RectF cRect = new RectF(cBadgeX, cBadgeY - cTextH + 2, cBadgeX + cTextW + 12, cBadgeY + 6);
            canvas.drawRoundRect(cRect, 6f, 6f, badgePaint);
            canvas.drawRoundRect(cRect, 6f, 6f, badgeStroke);
            canvas.drawText(centerLabel, cBadgeX + 6, cBadgeY - 2, textPaint);
        }

        // 3. Vertical Thermal Palette Scale Bar on Right Margin
        int barWidth = 14;
        int barHeight = (int) (bitmap.getHeight() * 0.62f);
        int barRight = bitmap.getWidth() - 18;
        int barLeft = barRight - barWidth;
        int barTop = (bitmap.getHeight() - barHeight) / 2;
        int barBottom = barTop + barHeight;

        int[] colors;
        boolean isPlasma = (mCurrentColorTab == 0 || mCurrentColorTab == ImageFusion.PSEUDO_COLOR_TAB_PLASMA);
        if (isPlasma) { // PLASMA
            colors = new int[] {
                Color.parseColor("#FEE838"), // top (hot)
                Color.parseColor("#F1824A"),
                Color.parseColor("#B43C88"),
                Color.parseColor("#63189E"),
                Color.parseColor("#0D0887")  // bottom (cold)
            };
        } else { // JET
            colors = new int[] {
                Color.parseColor("#FF0000"), // top (hot)
                Color.parseColor("#FFFF00"),
                Color.parseColor("#00FF00"),
                Color.parseColor("#00FFFF"),
                Color.parseColor("#0000FF")  // bottom (cold)
            };
        }
        float[] positions = new float[] { 0.0f, 0.25f, 0.5f, 0.75f, 1.0f };
        LinearGradient gradient = new LinearGradient(
            barLeft, barTop, barLeft, barBottom,
            colors, positions, Shader.TileMode.CLAMP
        );
        Paint barPaint = new Paint(Paint.ANTI_ALIAS_FLAG);
        barPaint.setShader(gradient);
        RectF barRect = new RectF(barLeft, barTop, barRight, barBottom);
        canvas.drawRoundRect(barRect, 4f, 4f, barPaint);

        Paint barBorder = new Paint(Paint.ANTI_ALIAS_FLAG);
        barBorder.setStyle(Paint.Style.STROKE);
        barBorder.setStrokeWidth(1.2f);
        barBorder.setColor(Color.parseColor("#800E1116"));
        canvas.drawRoundRect(barRect, 4f, 4f, barBorder);

        // Scale numerical markers
        Paint scaleText = new Paint(Paint.ANTI_ALIAS_FLAG);
        scaleText.setTextSize(20f);
        scaleText.setTypeface(Typeface.create("sans-serif-medium", Typeface.BOLD));
        scaleText.setColor(Color.parseColor("#F0F6FC"));
        scaleText.setShadowLayer(4f, 0, 0, Color.BLACK);

        String maxStr = String.format(Locale.getDefault(), "%.1f", frame.maxVal);
        float maxW = scaleText.measureText(maxStr);
        canvas.drawText(maxStr, barLeft - maxW - 6, barTop + 14, scaleText);

        String minStr = String.format(Locale.getDefault(), "%.1f", frame.minVal);
        float minW = scaleText.measureText(minStr);
        canvas.drawText(minStr, barLeft - minW - 6, barBottom, scaleText);
    }

    private void initViews() {
        mFusionImagePreview = findViewById(R.id.ivFusionImagePreview);
        mMaxTempTestView = findViewById(R.id.tvMaxTemperature);
        mCenterTempTextView = findViewById(R.id.tvCenterTemperature);
        mMinTempTestView = findViewById(R.id.tvMinTemperature);
        mDiffTempTextView = findViewById(R.id.tvDiffTemperature);
        mConnectTextView = findViewById(R.id.tvConnectHip);
        mDeviceOfflineLayout = findViewById(R.id.llDeviceOffline);
        mThermalTrendView = findViewById(R.id.thermalTrendView);
        mSbQuickFusionBlend = findViewById(R.id.sbQuickFusionBlend);
        mTvQuickBlendRatio = findViewById(R.id.tvQuickBlendRatio);

        mTargetStatusTextView = findViewById(R.id.tvTargetStatus);
        mEmissivityTextView = findViewById(R.id.tvEmissivity);

        mBtnModeFusion = findViewById(R.id.btnModeFusion);
        mBtnModeThermal = findViewById(R.id.btnModeThermal);
        mBtnModeVisible = findViewById(R.id.btnModeVisible);
        mBtnQuickPalette = findViewById(R.id.btnQuickPalette);

        mQuickAlignButton = findViewById(R.id.llQuickAlign);
        mShutterButton = findViewById(R.id.flShutterButton);
        mQuickSettingsButton = findViewById(R.id.llQuickSettings);
        mBtnQuickMirror = findViewById(R.id.btnQuickMirror);

        mBtnIndStandard = findViewById(R.id.btnIndStandard);
        mBtnIndElectrical = findViewById(R.id.btnIndElectrical);
        mBtnIndBuilding = findViewById(R.id.btnIndBuilding);
        mBtnIndPcb = findViewById(R.id.btnIndPcb);
        mBtnIndMedical = findViewById(R.id.btnIndMedical);
        mPcbActionStrip = findViewById(R.id.llPcbActionStrip);
        mTvPcbStatus = findViewById(R.id.tvPcbStatus);
        mBtnPcbCapture = findViewById(R.id.btnPcbCapture);
        mBtnPcbClear = findViewById(R.id.btnPcbClear);

        setupWorkspaceListeners();
    }

    private void setupWorkspaceListeners() {
        // Quick Fusion Blend Slider
        if (mSbQuickFusionBlend != null) {
            CalibrationManager.CalibrationData calib = CalibrationManager.load(this);
            int initialRatio = calib.highFreq > 0 ? calib.highFreq : 50;
            mSbQuickFusionBlend.setProgress(initialRatio);
            if (mTvQuickBlendRatio != null) {
                mTvQuickBlendRatio.setText(initialRatio + "%");
            }

            mSbQuickFusionBlend.setOnSeekBarChangeListener(new SeekBar.OnSeekBarChangeListener() {
                @Override
                public void onProgressChanged(SeekBar seekBar, int progress, boolean fromUser) {
                    if (mTvQuickBlendRatio != null) {
                        mTvQuickBlendRatio.setText(progress + "%");
                    }
                    if (fromUser && mImageFusion != null) {
                        mCurrentMode = 0;
                        updateModeButtons(0);
                        mImageFusion.setMode(0);
                        mImageFusion.setHighFreqRatio(progress);
                    }
                }

                @Override
                public void onStartTrackingTouch(SeekBar seekBar) {}

                @Override
                public void onStopTrackingTouch(SeekBar seekBar) {
                    CalibrationManager.CalibrationData data = CalibrationManager.load(MainActivity.this);
                    data.highFreq = seekBar.getProgress();
                    CalibrationManager.save(MainActivity.this, data);
                }
            });
        }

        // Emissivity cycling
        View chipEmissivity = findViewById(R.id.llChipEmissivity);
        if (chipEmissivity != null) {
            chipEmissivity.setOnClickListener(v -> {
                mEmissivityIndex = (mEmissivityIndex + 1) % EMISSIVITY_VALUES.length;
                mCurrentEmissivity = EMISSIVITY_VALUES[mEmissivityIndex];
                mEmissivityTextView.setText(EMISSIVITY_LABELS[mEmissivityIndex]);
                Toast.makeText(this, "辐射率已设置为: " + EMISSIVITY_LABELS[mEmissivityIndex], Toast.LENGTH_SHORT).show();
            });
        }

        // Mode Switching
        if (mBtnModeFusion != null) {
            mBtnModeFusion.setOnClickListener(v -> {
                mCurrentMode = ImageFusion.FUSION_MODE_FUSION;
                updateModeButtons(ImageFusion.FUSION_MODE_FUSION);
                if (mImageFusion != null) {
                    int ratio = mSbQuickFusionBlend != null ? mSbQuickFusionBlend.getProgress() : 50;
                    if (ratio <= 0) ratio = 50;
                    mImageFusion.setMode(ImageFusion.FUSION_MODE_FUSION);
                    mImageFusion.setHighFreqRatio(ratio);
                }
            });
        }

        if (mBtnModeThermal != null) {
            mBtnModeThermal.setOnClickListener(v -> {
                mCurrentMode = ImageFusion.FUSION_MODE_THERMAL;
                updateModeButtons(ImageFusion.FUSION_MODE_THERMAL);
                if (mImageFusion != null) {
                    mImageFusion.setMode(ImageFusion.FUSION_MODE_THERMAL);
                }
            });
        }

        if (mBtnModeVisible != null) {
            mBtnModeVisible.setOnClickListener(v -> {
                mCurrentMode = ImageFusion.FUSION_MODE_VISIBLE;
                updateModeButtons(ImageFusion.FUSION_MODE_VISIBLE);
                if (mImageFusion != null) {
                    mImageFusion.setMode(ImageFusion.FUSION_MODE_VISIBLE);
                }
            });
        }

        // Palette Switching
        if (mBtnQuickPalette != null) {
            mBtnQuickPalette.setOnClickListener(v -> {
                boolean isPlasma = (mCurrentColorTab == 0 || mCurrentColorTab == ImageFusion.PSEUDO_COLOR_TAB_PLASMA);
                mCurrentColorTab = isPlasma ? ImageFusion.PSEUDO_COLOR_TAB_JET : ImageFusion.PSEUDO_COLOR_TAB_PLASMA;
                if (mImageFusion != null) {
                    mImageFusion.setColorTab(mCurrentColorTab);
                    CalibrationManager.CalibrationData data = CalibrationManager.load(this);
                    data.colorTab = mCurrentColorTab;
                    CalibrationManager.save(this, data);
                }
                updatePaletteDisplay();
            });
        }

        // Auto Align
        if (mQuickAlignButton != null) {
            mQuickAlignButton.setOnClickListener(v -> {
                if (mImageFusion == null) {
                    Toast.makeText(this, "设备未连接", Toast.LENGTH_SHORT).show();
                    return;
                }
                Toast.makeText(this, R.string.auto_calib_running, Toast.LENGTH_SHORT).show();
                mImageFusion.autoCalibrate(new ImageFusion.OnAutoCalibrateCallback() {
                    @Override
                    public void onSuccess(int offsetX, int offsetY, float scale, float distance, float score) {
                        runOnUiThread(() -> {
                            // Decoupled architecture: do NOT overwrite persistent hardware calibration in CalibrationManager!
                            String msg = String.format(Locale.getDefault(),
                                    "光轴智能对齐完成\n距离: %.2f m | X: %d px, Y: %d px", distance, offsetX, offsetY);
                            Toast.makeText(MainActivity.this, msg, Toast.LENGTH_SHORT).show();
                        });
                    }

                    @Override
                    public void onFailed(int status, String reason) {
                        runOnUiThread(() -> Toast.makeText(MainActivity.this, reason, Toast.LENGTH_LONG).show());
                    }
                });
            });
        }

        // Quick Mirror Toggle (Hidden per user request: "不要镜像，就是解决对齐问题")
        if (mBtnQuickMirror != null) {
            mBtnQuickMirror.setVisibility(View.GONE);
        }

        // Shutter Button
        if (mShutterButton != null) {
            mShutterButton.setOnClickListener(v -> takeSnapshot());
        }

        // Quick Settings
        if (mQuickSettingsButton != null) {
            mQuickSettingsButton.setOnClickListener(v -> showCameraControlsDialog());
        }

        // Industry Scenario Buttons
        if (mBtnIndStandard != null) mBtnIndStandard.setOnClickListener(v -> switchIndustryMode(ImageFusion.INDUSTRY_STANDARD));
        if (mBtnIndElectrical != null) mBtnIndElectrical.setOnClickListener(v -> switchIndustryMode(ImageFusion.INDUSTRY_ELECTRICAL));
        if (mBtnIndBuilding != null) mBtnIndBuilding.setOnClickListener(v -> switchIndustryMode(ImageFusion.INDUSTRY_BUILDING));
        if (mBtnIndPcb != null) mBtnIndPcb.setOnClickListener(v -> switchIndustryMode(ImageFusion.INDUSTRY_PCB));
        if (mBtnIndMedical != null) mBtnIndMedical.setOnClickListener(v -> switchIndustryMode(ImageFusion.INDUSTRY_MEDICAL));

        if (mBtnPcbCapture != null) {
            mBtnPcbCapture.setOnClickListener(v -> {
                if (mImageFusion != null) {
                    mImageFusion.captureBaseline();
                    Toast.makeText(this, "📸 已截取冷态基准，差分高亮已启动", Toast.LENGTH_SHORT).show();
                    updatePcbStatus(true);
                }
            });
        }
        if (mBtnPcbClear != null) {
            mBtnPcbClear.setOnClickListener(v -> {
                if (mImageFusion != null) {
                    mImageFusion.clearBaseline();
                    Toast.makeText(this, "✕ 已重置基准", Toast.LENGTH_SHORT).show();
                    updatePcbStatus(false);
                }
            });
        }

        // Initial states
        CalibrationManager.CalibrationData data = CalibrationManager.load(this);
        mCurrentColorTab = data.colorTab;
        mCurrentIndustryMode = data.industryMode;
        mIsothermAlarmTemp = data.isothermTemp;
        updatePaletteDisplay();
        updateModeButtons(0);
        updateIndustryButtons(mCurrentIndustryMode);
    }

    private void updatePcbStatus(boolean hasBaseline) {
        if (mTvPcbStatus != null) {
            if (hasBaseline) {
                mTvPcbStatus.setText("✓ 差分对比中 (高亮发热器件)");
                mTvPcbStatus.setTextColor(Color.parseColor("#38BDF8"));
            } else {
                mTvPcbStatus.setText("冷态未采样 (通电前先点击采样)");
                mTvPcbStatus.setTextColor(Color.parseColor("#8B949E"));
            }
        }
        if (mBtnPcbCapture != null) {
            mBtnPcbCapture.setText(hasBaseline ? "🔄 重新采样" : "📸 采样冷态基准");
        }
    }

    private void updateIndustryButtons(int mode) {
        int activeColor = Color.parseColor("#00D2A0");
        int inactiveColor = Color.parseColor("#8B949E");
        if (mBtnIndStandard != null) {
            mBtnIndStandard.setTextColor(mode == ImageFusion.INDUSTRY_STANDARD ? activeColor : inactiveColor);
            mBtnIndStandard.setSelected(mode == ImageFusion.INDUSTRY_STANDARD);
        }
        if (mBtnIndElectrical != null) {
            mBtnIndElectrical.setTextColor(mode == ImageFusion.INDUSTRY_ELECTRICAL ? Color.parseColor("#FF6B4A") : inactiveColor);
            mBtnIndElectrical.setSelected(mode == ImageFusion.INDUSTRY_ELECTRICAL);
        }
        if (mBtnIndBuilding != null) {
            mBtnIndBuilding.setTextColor(mode == ImageFusion.INDUSTRY_BUILDING ? activeColor : inactiveColor);
            mBtnIndBuilding.setSelected(mode == ImageFusion.INDUSTRY_BUILDING);
        }
        if (mBtnIndPcb != null) {
            mBtnIndPcb.setTextColor(mode == ImageFusion.INDUSTRY_PCB ? Color.parseColor("#38BDF8") : inactiveColor);
            mBtnIndPcb.setSelected(mode == ImageFusion.INDUSTRY_PCB);
        }
        if (mBtnIndMedical != null) {
            mBtnIndMedical.setTextColor(mode == ImageFusion.INDUSTRY_MEDICAL ? Color.parseColor("#FF6B4A") : inactiveColor);
            mBtnIndMedical.setSelected(mode == ImageFusion.INDUSTRY_MEDICAL);
        }

        boolean isPcb = (mode == ImageFusion.INDUSTRY_PCB);
        if (mPcbActionStrip != null) {
            mPcbActionStrip.setVisibility(isPcb ? View.VISIBLE : View.GONE);
        }
        if (isPcb) {
            updatePcbStatus(mImageFusion != null && mImageFusion.hasBaseline());
        }
    }

    private void switchIndustryMode(int mode) {
        mCurrentIndustryMode = mode;
        updateIndustryButtons(mode);
        if (mImageFusion != null) {
            if (mode == ImageFusion.INDUSTRY_ELECTRICAL) {
                mImageFusion.setIndustryMode(mode, mIsothermAlarmTemp, 0);
                Toast.makeText(this, "⚡ 已开启【电力巡检模式】：超温等温线报警 (≥50℃)", Toast.LENGTH_SHORT).show();
            } else if (mode == ImageFusion.INDUSTRY_BUILDING) {
                mImageFusion.setIndustryMode(mode, 0, 3.5f);
                mCurrentColorTab = 1; // JET
                mImageFusion.setColorTab(1);
                updatePaletteDisplay();
                Toast.makeText(this, "🏢 已开启【建筑暖通模式】：CLAHE微温差高敏增强已就绪", Toast.LENGTH_SHORT).show();
            } else if (mode == ImageFusion.INDUSTRY_PCB) {
                mImageFusion.setIndustryMode(mode, 0, 2.5f);
                Toast.makeText(this, "💻 已开启【电子排障模式】：请点击「采样冷态基准」", Toast.LENGTH_SHORT).show();
            } else if (mode == ImageFusion.INDUSTRY_MEDICAL) {
                mImageFusion.setIndustryMode(mode, 37.3f, 0);
                Toast.makeText(this, "🩺 已开启【医疗体温模式】：35~42℃生理温区高敏拉伸", Toast.LENGTH_SHORT).show();
            } else {
                mImageFusion.setIndustryMode(mode, 0, 0);
                Toast.makeText(this, "⚙️ 已恢复【通用工业模式】", Toast.LENGTH_SHORT).show();
            }
        }
        CalibrationManager.CalibrationData data = CalibrationManager.load(this);
        data.industryMode = mode;
        CalibrationManager.save(this, data);
    }

    private void updateModeButtons(int mode) {
        int activeColor = Color.parseColor("#00D2A0");
        int inactiveColor = Color.parseColor("#8B949E");
        if (mBtnModeFusion != null) {
            mBtnModeFusion.setTextColor(mode == 0 ? activeColor : inactiveColor);
            mBtnModeFusion.setSelected(mode == 0);
        }
        if (mBtnModeThermal != null) {
            mBtnModeThermal.setTextColor(mode == 1 ? activeColor : inactiveColor);
            mBtnModeThermal.setSelected(mode == 1);
        }
        if (mBtnModeVisible != null) {
            mBtnModeVisible.setTextColor(mode == 2 ? activeColor : inactiveColor);
            mBtnModeVisible.setSelected(mode == 2);
        }
    }

    private void updatePaletteDisplay() {
        if (mBtnQuickPalette == null) return;
        boolean isPlasma = (mCurrentColorTab == 0 || mCurrentColorTab == ImageFusion.PSEUDO_COLOR_TAB_PLASMA);
        if (isPlasma) {
            mBtnQuickPalette.setText("色谱: 等离子");
            mBtnQuickPalette.setTextColor(Color.parseColor("#38BDF8"));
        } else {
            mBtnQuickPalette.setText("色谱: 彩虹色");
            mBtnQuickPalette.setTextColor(Color.parseColor("#FF6B4A"));
        }
    }

    private void updateMirrorButtonState(boolean isMirror) {
        if (mBtnQuickMirror == null) return;
        if (isMirror) {
            mBtnQuickMirror.setText("⇄ 镜像: 开");
            mBtnQuickMirror.setTextColor(Color.parseColor("#00D2A0"));
            mBtnQuickMirror.setBackgroundResource(R.drawable.bg_quick_pill_active);
        } else {
            mBtnQuickMirror.setText("⇄ 镜像: 关");
            mBtnQuickMirror.setTextColor(Color.parseColor("#8E8EA0"));
            mBtnQuickMirror.setBackgroundResource(R.drawable.bg_quick_pill);
        }
    }

    private void takeSnapshot() {
        if (mLatestFusedBitmap == null) {
            Toast.makeText(this, "暂无图像可供抓拍", Toast.LENGTH_SHORT).show();
            return;
        }

        try {
            Vibrator v = (Vibrator) getSystemService(VIBRATOR_SERVICE);
            if (v != null) {
                if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.O) {
                    v.vibrate(VibrationEffect.createOneShot(50, VibrationEffect.DEFAULT_AMPLITUDE));
                } else {
                    v.vibrate(50);
                }
            }
        } catch (Exception ignored) {}

        Bitmap toSave = mLatestFusedBitmap.copy(Bitmap.Config.ARGB_8888, false);
        new Thread(() -> {
            boolean success = saveBitmapToGallery(toSave);
            runOnUiThread(() -> {
                if (success) {
                    Toast.makeText(MainActivity.this, R.string.toast_photo_saved, Toast.LENGTH_SHORT).show();
                } else {
                    Toast.makeText(MainActivity.this, R.string.toast_photo_failed, Toast.LENGTH_SHORT).show();
                }
            });
        }).start();
    }

    private boolean saveBitmapToGallery(Bitmap bitmap) {
        String filename = "THERMAL_" + System.currentTimeMillis() + ".png";
        ContentValues values = new ContentValues();
        values.put(MediaStore.Images.Media.DISPLAY_NAME, filename);
        values.put(MediaStore.Images.Media.MIME_TYPE, "image/png");
        values.put(MediaStore.Images.Media.RELATIVE_PATH, Environment.DIRECTORY_PICTURES + "/ThermalEyes");

        Uri uri = getContentResolver().insert(MediaStore.Images.Media.EXTERNAL_CONTENT_URI, values);
        if (uri == null) return false;

        try (OutputStream os = getContentResolver().openOutputStream(uri)) {
            if (os == null) return false;
            bitmap.compress(Bitmap.CompressFormat.PNG, 100, os);
            return true;
        } catch (Exception e) {
            Log.e(TAG, "saveBitmapToGallery error: " + e.getMessage());
            return false;
        }
    }

    @RequiresApi(api = Build.VERSION_CODES.O)
    @Override
    protected void onStart() {
        super.onStart();
        initCameraHelper();
        initThermalDevice();
    }

    @Override
    protected void onResume() {
        super.onResume();
        checkAndConnectDevice();
    }

    @Override
    protected void onStop() {
        super.onStop();
        clearCameraHelper();
        if (mThermalDevice != null) {
            mThermalDevice.disconnect();
        }
        if (mImageFusion != null) {
            mImageFusion.exit();
            mImageFusion = null;
        }
    }

    @Override
    protected void onDestroy() {
        super.onDestroy();
        clearCameraHelper();
        if (mThermalDevice != null) {
            mThermalDevice.disconnect();
        }
        if (mImageFusion != null) {
            mImageFusion.exit();
            mImageFusion = null;
        }
    }

    @Override
    protected void onNewIntent(Intent intent) {
        super.onNewIntent(intent);
        setIntent(intent);
        Log.i(TAG, "onNewIntent: USB device re-attached, check and connect");
        checkAndConnectDevice();
    }

    private void initCameraHelper() {
        if (CAM_DISPLAY) Log.d(TAG, "initCameraHelper:");
        if (mCameraHelper == null) {
            mCameraHelper = new CameraHelper();
            mCameraHelper.setStateCallback(mStateListener);
        }
    }

    private boolean isCameraDevice(UsbDevice device) {
        if (device == null) return false;
        if (device.getVendorId() == 2316) return true;
        if (device.getDeviceClass() == 239 && device.getDeviceSubclass() == 2) return true;
        if (device.getDeviceClass() == 14) return true;
        for (int i = 0; i < device.getInterfaceCount(); i++) {
            if (device.getInterface(i).getInterfaceClass() == 14) {
                return true;
            }
        }
        return false;
    }

    private void checkAndConnectDevice() {
        if (mCameraHelper == null) {
            initCameraHelper();
        }
        UsbManager usbManager = (UsbManager) getSystemService(USB_SERVICE);
        if (usbManager == null) return;

        HashMap<String, UsbDevice> deviceList = usbManager.getDeviceList();
        Log.i(TAG, "checkAndConnectDevice: found " + deviceList.size() + " devices");
        for (UsbDevice device : deviceList.values()) {
            Log.i(TAG, "USB Device: name=" + device.getDeviceName() + ", VID=" + device.getVendorId() + ", PID=" + device.getProductId());
            if (isCameraDevice(device)) {
                Log.i(TAG, "Found target camera device, triggering selectDevice");
                mStateListener.onAttach(device);
                break;
            }
        }
    }

    private void initThermalDevice() {
        if (mImageFusion != null) {
            return;
        }

        mImageFusion = new ImageFusion(DEFAULT_WIDTH, DEFAULT_HEIGHT,
                ThermalDevice.IMAGE_WIDTH, ThermalDevice.IMAGE_HEIGHT) {

            @Override
            public void onFrame(FrameInfo frame) {
                if (frame == null || frame.data == null) return;
                try {
                    Bitmap srcBm = mNv21ToBitmap.nv21ToBitmap(frame.data, DEFAULT_WIDTH, DEFAULT_HEIGHT);
                    if (srcBm == null) return;

                    drawTemptationTrack(srcBm, frame);
                    mLatestFusedBitmap = srcBm;
                    mIsCameraConnected = true;

                    runOnUiThread(() -> {
                        if (mFusionImagePreview != null) {
                            mFusionImagePreview.setImageBitmap(srcBm);
                        }
                    });
                } catch (Throwable t) {
                    Log.e(TAG, "Fusion onFrame error: " + t.getMessage());
                }
            }
        };

        // Load saved visual calibration parameters
        CalibrationManager.CalibrationData savedCalib = CalibrationManager.load(this);
        mImageFusion.setMirror(savedCalib.mirrorX, savedCalib.mirrorY);
        mImageFusion.setCalibration(savedCalib.offsetX, savedCalib.offsetY, savedCalib.scale, savedCalib.rotation);
        mImageFusion.setHighFreqRatio(savedCalib.highFreq);
        mImageFusion.setColorTab(savedCalib.colorTab);
        mImageFusion.setMode(savedCalib.fusionMode);
        mImageFusion.setIndustryMode(savedCalib.industryMode, savedCalib.isothermTemp, 0);
        mCurrentIndustryMode = savedCalib.industryMode;
        mIsothermAlarmTemp = savedCalib.isothermTemp;
        runOnUiThread(() -> {
            updateIndustryButtons(mCurrentIndustryMode);
            updateMirrorButtonState(savedCalib.mirrorX);
        });

        mImageFusion.start();
    }

    private void clearCameraHelper() {
        if (CAM_DISPLAY) Log.d(TAG, "clearCameraHelper:");
        mIsCameraOpened = false;
        if (mCameraHelper != null) {
            try {
                mCameraHelper.setFrameCallback(null, 0);
            } catch (Exception ignored) { }
            try {
                mCameraHelper.stopPreview();
                mCameraHelper.closeCamera();
            } catch (Exception ignored) { }
            try {
                mCameraHelper.release();
            } catch (Exception ignored) { }
            mCameraHelper = null;
        }
    }

    private void selectDevice(final UsbDevice device) {
        if (CAM_DISPLAY) Log.v(TAG, "selectDevice:device=" + device.getDeviceName());
        if (mCameraHelper != null) {
            mCameraHelper.selectDevice(device);
        }
    }

    private final ICameraHelper.StateCallback mStateListener = new ICameraHelper.StateCallback() {
        static final int CAMERA_VIP = 2316;

        @Override
        public void onAttach(UsbDevice device) {
            if (!isCameraDevice(device)) {
                Log.i(TAG, "onAttach: Not support device: " + device.getVendorId());
                return;
            }
            if (mCameraHelper == null) return;
            if (mIsCameraOpened) {
                Log.i(TAG, "onAttach: camera already opened, skip selectDevice");
                return;
            }

            Log.i(TAG, "onAttach: supported camera attached: " + device.getDeviceName() + " (VID=" + device.getVendorId() + ")");
            selectDevice(device);
            runOnUiThread(() -> {
                if (mFusionImagePreview != null) mFusionImagePreview.setVisibility(View.VISIBLE);
                if (mDeviceOfflineLayout != null) mDeviceOfflineLayout.setVisibility(View.GONE);
            });

            try {
                mThermalDevice.connect();
                CalibrationManager.CalibrationData savedCalib = CalibrationManager.load(MainActivity.this);
                mThermalDevice.setFPS(savedCalib.fps);
                Log.i(TAG, "mThermalDevice connected successfully with FPS: " + savedCalib.fps);
            } catch (IOException e) {
                Log.e(TAG, "ThermalDevice connect failed: " + e.getMessage());
            }
        }

        @Override
        public void onDeviceOpen(UsbDevice device, boolean isFirstOpen) {
            if (!isCameraDevice(device)) return;
            if (mCameraHelper == null) return;
            Log.i(TAG, "onDeviceOpen: opening camera");
            try {
                mCameraHelper.openCamera();
            } catch (Exception e) {
                Log.e(TAG, "openCamera failed: " + e.getMessage());
            }
        }

        @Override
        public void onCameraOpen(UsbDevice device) {
            if (!isCameraDevice(device)) return;
            if (mCameraHelper == null) return;
            mIsCameraOpened = true;
            Log.i(TAG, "onCameraOpen: configuring preview & callback");
            try {
                Size size = mCameraHelper.getPreviewSize();
                int previewWidth = size != null ? size.width : DEFAULT_WIDTH;
                int previewHeight = size != null ? size.height : DEFAULT_HEIGHT;
                int expectedBytes = previewWidth * previewHeight * 2;
                byte[] nv21Buffer = new byte[previewWidth * previewHeight * 3 / 2];

                mCameraHelper.setFrameCallback(frame -> {
                    if (frame == null) return;
                    try {
                        int remaining = frame.remaining();
                        if (remaining < expectedBytes) {
                            // Drop incomplete/dropped USB packet frame to prevent any crash
                            return;
                        }
                        ImageFusion.yuyvToNv21(frame, nv21Buffer, previewWidth, previewHeight);

                        FrameInfo frameInfo = new FrameInfo();
                        frameInfo.data = new byte[nv21Buffer.length];
                        System.arraycopy(nv21Buffer, 0, frameInfo.data, 0, nv21Buffer.length);
                        frameInfo.width = previewWidth;
                        frameInfo.height = previewHeight;

                        ImageFusion fusion = mImageFusion;
                        if (fusion != null) {
                            fusion.putCameraImage(frameInfo);
                        }
                    } catch (Throwable e) {
                        Log.e(TAG, "frameCallback error: " + e.getMessage());
                    }
                }, UVCCamera.PIXEL_FORMAT_RAW);

                mCameraHelper.startPreview();
            } catch (Exception e) {
                Log.e(TAG, "startPreview failed: " + e.getMessage());
            }
        }

        @Override
        public void onCameraClose(UsbDevice device) {
            mIsCameraOpened = false;
            if (CAM_DISPLAY) Log.v(TAG, "onCameraClose:");
        }

        @Override
        public void onDeviceClose(UsbDevice device) {
            mIsCameraOpened = false;
            if (CAM_DISPLAY) Log.v(TAG, "onDeviceClose:");
        }

        @Override
        public void onDetach(UsbDevice device) {
            mIsCameraOpened = false;
            if (!isCameraDevice(device)) return;
            if (CAM_DISPLAY) Log.v(TAG, "onDetach:");
            clearCameraHelper();
            runOnUiThread(() -> {
                if (mFusionImagePreview != null) mFusionImagePreview.setVisibility(View.GONE);
                if (mDeviceOfflineLayout != null) mDeviceOfflineLayout.setVisibility(View.VISIBLE);
            });
            if (mThermalDevice != null) {
                mThermalDevice.disconnect();
            }
        }

        @Override
        public void onCancel(UsbDevice device) {
            if (!isCameraDevice(device)) return;
            if (CAM_DISPLAY) Log.v(TAG, "onCancel: camera permission canceled");
        }

        @Override
        public void onError(UsbDevice device, com.herohan.uvcapp.CameraException e) {
            Log.e(TAG, "CameraHelper onError: code=" + e.getCode() + ", msg=" + e.getMessage(), e);
            runOnUiThread(() -> Toast.makeText(MainActivity.this, "相机异常: " + e.getMessage(), Toast.LENGTH_SHORT).show());
        }
    };

    @Override
    public void onClick(View v) { }

    @Override
    public boolean onCreateOptionsMenu(Menu menu) {
        // Inflate the menu; this adds items to the action bar if it is present.
        getMenuInflater().inflate(R.menu.menu_main, menu);
        return true;
    }

    @Override
    public boolean onOptionsItemSelected(MenuItem item) {
        // Handle action bar item clicks here. The action bar will
        // automatically handle clicks on the Home/Up button, so long
        // as you specify a parent activity in AndroidManifest.xml.
        int id = item.getItemId();

        //noinspection SimplifiableIfStatement
        if (id == R.id.action_control) {
            showCameraControlsDialog();
        }

        return true;
    }

    @Override
    public boolean onPrepareOptionsMenu(Menu menu) {
        if (mIsCameraConnected) {
            menu.findItem(R.id.action_control).setVisible(true);
        } else {
            menu.findItem(R.id.action_control).setVisible(false);
        }
        return super.onPrepareOptionsMenu(menu);
    }

    private void showCameraControlsDialog() {
        if (mControlsDialog == null) {
            mControlsDialog = new ParameterDialogFragment(mImageFusion, mThermalDevice);
        }
        // When DialogFragment is not showing
        if (!mControlsDialog.isAdded()) {
            mControlsDialog.show(getSupportFragmentManager(), "camera_controls");
        }
    }
}