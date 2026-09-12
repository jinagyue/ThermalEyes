package com.example.thermaleyes;

import android.content.Context;
import android.graphics.Canvas;
import android.graphics.Color;
import android.graphics.Paint;
import android.graphics.Path;
import android.graphics.RectF;
import android.graphics.Typeface;
import android.util.AttributeSet;
import android.view.View;

import androidx.annotation.Nullable;

import java.util.Locale;

public class ThermalTrendView extends View {
    private static final int MAX_POINTS = 50;
    private final float[] mMaxPoints = new float[MAX_POINTS];
    private final float[] mCenterPoints = new float[MAX_POINTS];
    private final float[] mMinPoints = new float[MAX_POINTS];
    private int mPointCount = 0;
    private int mHead = 0;

    private final Paint mBgPaint = new Paint(Paint.ANTI_ALIAS_FLAG);
    private final Paint mBorderPaint = new Paint(Paint.ANTI_ALIAS_FLAG);
    private final Paint mGridPaint = new Paint(Paint.ANTI_ALIAS_FLAG);
    private final Paint mMaxLinePaint = new Paint(Paint.ANTI_ALIAS_FLAG);
    private final Paint mCenterLinePaint = new Paint(Paint.ANTI_ALIAS_FLAG);
    private final Paint mMinLinePaint = new Paint(Paint.ANTI_ALIAS_FLAG);
    private final Paint mTextPaint = new Paint(Paint.ANTI_ALIAS_FLAG);
    private final RectF mBgRect = new RectF();
    private final Path mMaxPath = new Path();
    private final Path mCenterPath = new Path();
    private final Path mMinPath = new Path();

    private float mLatestMax = 0f;
    private float mLatestCenter = 0f;
    private float mLatestMin = 0f;

    public ThermalTrendView(Context context) {
        super(context);
        init();
    }

    public ThermalTrendView(Context context, @Nullable AttributeSet attrs) {
        super(context, attrs);
        init();
    }

    public ThermalTrendView(Context context, @Nullable AttributeSet attrs, int defStyleAttr) {
        super(context, attrs, defStyleAttr);
        init();
    }

    private void init() {
        mBgPaint.setStyle(Paint.Style.FILL);
        mBgPaint.setColor(Color.parseColor("#161B22"));

        mBorderPaint.setStyle(Paint.Style.STROKE);
        mBorderPaint.setStrokeWidth(1.2f);
        mBorderPaint.setColor(Color.parseColor("#21262D"));

        mGridPaint.setStyle(Paint.Style.STROKE);
        mGridPaint.setStrokeWidth(1f);
        mGridPaint.setColor(Color.parseColor("#1F242C"));

        mMaxLinePaint.setStyle(Paint.Style.STROKE);
        mMaxLinePaint.setStrokeWidth(2.8f);
        mMaxLinePaint.setColor(Color.parseColor("#FF6B4A"));
        mMaxLinePaint.setStrokeCap(Paint.Cap.ROUND);
        mMaxLinePaint.setStrokeJoin(Paint.Join.ROUND);

        mCenterLinePaint.setStyle(Paint.Style.STROKE);
        mCenterLinePaint.setStrokeWidth(2.2f);
        mCenterLinePaint.setColor(Color.parseColor("#00D2A0"));
        mCenterLinePaint.setStrokeCap(Paint.Cap.ROUND);
        mCenterLinePaint.setStrokeJoin(Paint.Join.ROUND);

        mMinLinePaint.setStyle(Paint.Style.STROKE);
        mMinLinePaint.setStrokeWidth(2.2f);
        mMinLinePaint.setColor(Color.parseColor("#38BDF8"));
        mMinLinePaint.setStrokeCap(Paint.Cap.ROUND);
        mMinLinePaint.setStrokeJoin(Paint.Join.ROUND);

        mTextPaint.setTypeface(Typeface.create("sans-serif-medium", Typeface.BOLD));
        mTextPaint.setTextSize(22f);
    }

    public synchronized void addTemperaturePoint(float maxVal, float centerVal) {
        addTemperaturePoint(maxVal, centerVal, centerVal);
    }

    public synchronized void addTemperaturePoint(float maxVal, float centerVal, float minVal) {
        if (maxVal < -40f || maxVal > 150f) return;
        mMaxPoints[mHead] = maxVal;
        mCenterPoints[mHead] = centerVal;
        mMinPoints[mHead] = minVal;
        mLatestMax = maxVal;
        mLatestCenter = centerVal;
        mLatestMin = minVal;

        mHead = (mHead + 1) % MAX_POINTS;
        if (mPointCount < MAX_POINTS) {
            mPointCount++;
        }
        postInvalidate();
    }

    @Override
    protected void onDraw(Canvas canvas) {
        super.onDraw(canvas);
        int w = getWidth();
        int h = getHeight();
        if (w <= 0 || h <= 0) return;

        mBgRect.set(1f, 1f, w - 1f, h - 1f);
        canvas.drawRoundRect(mBgRect, 10f, 10f, mBgPaint);
        canvas.drawRoundRect(mBgRect, 10f, 10f, mBorderPaint);

        // Header Title
        mTextPaint.setColor(Color.parseColor("#8B949E"));
        mTextPaint.setTextSize(18f);
        canvas.drawText("30S 测温趋势", 16f, 25f, mTextPaint);

        // Legends: MAX (#FF6B4A), CTR (#00D2A0), MIN (#38BDF8)
        mTextPaint.setTextSize(17f);

        // 1. MAX Legend
        mTextPaint.setColor(Color.parseColor("#FF6B4A"));
        String maxLegend = String.format(Locale.getDefault(), "● MAX:%.1f°", mLatestMax);
        float maxW = mTextPaint.measureText(maxLegend);
        float maxX = w - maxW - 14f;
        canvas.drawText(maxLegend, maxX, 25f, mTextPaint);

        // 2. CTR Legend
        mTextPaint.setColor(Color.parseColor("#00D2A0"));
        String ctrLegend = String.format(Locale.getDefault(), "● CTR:%.1f°", mLatestCenter);
        float ctrW = mTextPaint.measureText(ctrLegend);
        float ctrX = maxX - ctrW - 14f;
        canvas.drawText(ctrLegend, ctrX, 25f, mTextPaint);

        // 3. MIN Legend
        mTextPaint.setColor(Color.parseColor("#38BDF8"));
        String minLegend = String.format(Locale.getDefault(), "● MIN:%.1f°", mLatestMin);
        float minW = mTextPaint.measureText(minLegend);
        float minX = ctrX - minW - 14f;
        canvas.drawText(minLegend, minX, 25f, mTextPaint);

        if (mPointCount < 2) return;

        // Determine range for plotting
        float minP = Float.MAX_VALUE;
        float maxP = -Float.MAX_VALUE;
        for (int i = 0; i < mPointCount; i++) {
            float valM = mMaxPoints[i];
            float valC = mCenterPoints[i];
            float valL = mMinPoints[i];
            if (valM < minP) minP = valM;
            if (valM > maxP) maxP = valM;
            if (valC < minP) minP = valC;
            if (valC > maxP) maxP = valC;
            if (valL < minP) minP = valL;
            if (valL > maxP) maxP = valL;
        }

        float span = maxP - minP;
        if (span < 4.0f) {
            float mid = (maxP + minP) / 2.0f;
            minP = mid - 2.0f;
            maxP = mid + 2.0f;
            span = 4.0f;
        } else {
            minP -= span * 0.1f;
            maxP += span * 0.1f;
            span = maxP - minP;
        }

        // Plot area
        float plotLeft = 45f;
        float plotRight = w - 16f;
        float plotTop = 38f;
        float plotBottom = h - 14f;
        float plotHeight = plotBottom - plotTop;
        float plotWidth = plotRight - plotLeft;

        // Draw 3 subtle horizontal grid lines
        for (int g = 0; g < 3; g++) {
            float gy = plotTop + plotHeight * g / 2.0f;
            canvas.drawLine(plotLeft, gy, plotRight, gy, mGridPaint);

            float gTemp = maxP - span * g / 2.0f;
            mTextPaint.setColor(Color.parseColor("#484F58"));
            mTextPaint.setTextSize(16f);
            String yLabel = String.format(Locale.getDefault(), "%.0f°", gTemp);
            canvas.drawText(yLabel, 8f, gy + 5f, mTextPaint);
        }

        // Build Paths
        mMaxPath.reset();
        mCenterPath.reset();
        mMinPath.reset();

        int startIdx = (mPointCount < MAX_POINTS) ? 0 : mHead;
        for (int i = 0; i < mPointCount; i++) {
            int idx = (startIdx + i) % MAX_POINTS;
            float x = plotLeft + plotWidth * i / (mPointCount - 1);

            float yMax = plotBottom - ((mMaxPoints[idx] - minP) / span) * plotHeight;
            float yCtr = plotBottom - ((mCenterPoints[idx] - minP) / span) * plotHeight;
            float yMin = plotBottom - ((mMinPoints[idx] - minP) / span) * plotHeight;

            if (i == 0) {
                mMaxPath.moveTo(x, yMax);
                mCenterPath.moveTo(x, yCtr);
                mMinPath.moveTo(x, yMin);
            } else {
                mMaxPath.lineTo(x, yMax);
                mCenterPath.lineTo(x, yCtr);
                mMinPath.lineTo(x, yMin);
            }
        }

        // Draw 3 curves
        canvas.drawPath(mMinPath, mMinLinePaint);
        canvas.drawPath(mCenterPath, mCenterLinePaint);
        canvas.drawPath(mMaxPath, mMaxLinePaint);
    }
}
