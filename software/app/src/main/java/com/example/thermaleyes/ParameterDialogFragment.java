package com.example.thermaleyes;

import android.app.Dialog;
import android.os.Bundle;
import android.view.LayoutInflater;
import android.view.View;
import android.view.ViewGroup;
import android.view.Window;
import android.view.WindowManager;
import android.widget.RadioButton;
import android.widget.RadioGroup;
import android.widget.Toast;

import androidx.annotation.NonNull;
import androidx.annotation.Nullable;
import androidx.fragment.app.DialogFragment;

import com.example.thermaleyes.databinding.FragmentParamterBinding;
import com.warkiz.widget.IndicatorSeekBar;
import com.warkiz.widget.OnSeekChangeListener;

public class ParameterDialogFragment extends DialogFragment {

    private final ImageFusion mImageFusion;
    private final ThermalDevice mThermalDevice;

    private FragmentParamterBinding mBinding;
    private static final String TAG = ParameterDialogFragment.class.getSimpleName();

    public ParameterDialogFragment() {
        mImageFusion = null;
        mThermalDevice = null;
    }

    public ParameterDialogFragment(ImageFusion imageFusion, ThermalDevice thermalDevice) {
        mImageFusion = imageFusion;
        mThermalDevice = thermalDevice;
    }

    @Override
    public void onCreate(@Nullable Bundle savedInstanceState) {
        super.onCreate(savedInstanceState);
        setStyle(STYLE_NO_TITLE, R.style.TransparentDialogFragment);
    }

    @NonNull
    @Override
    public Dialog onCreateDialog(@Nullable Bundle savedInstanceState) {
        Dialog dialog = super.onCreateDialog(savedInstanceState);
        dialog.setCanceledOnTouchOutside(false);
        return dialog;
    }

    @Override
    public View onCreateView(@NonNull LayoutInflater inflater, @Nullable ViewGroup container, @Nullable Bundle savedInstanceState) {
        mBinding = FragmentParamterBinding.inflate(getLayoutInflater(), container, false);
        setButtonListeners();
        return mBinding.getRoot();
    }

    @Override
    public void onStart() {
        super.onStart();
        disableDimBehind();
        showCameraControls();
    }

    private void setButtonListeners() {
        mBinding.btnCameraControlsCancel.setOnClickListener(v -> {
            dismiss();
        });

        mBinding.btnCameraControlsReset.setOnClickListener(v -> {
            CalibrationManager.CalibrationData defaultData = new CalibrationManager.CalibrationData();
            applyParamToDevice(defaultData);
            setAllControlParams(defaultData);
            CalibrationManager.reset(requireContext());
            Toast.makeText(requireContext(), R.string.calib_reset_tip, Toast.LENGTH_SHORT).show();
        });

        mBinding.btnCameraControlsSave.setOnClickListener(v -> {
            CalibrationManager.CalibrationData currentData = getCurrentParam();
            CalibrationManager.save(requireContext(), currentData);
            if (getActivity() instanceof MainActivity) {
                ((MainActivity) getActivity()).updateAlignButtonDisplay();
            }
            Toast.makeText(requireContext(), R.string.calib_saved_tip, Toast.LENGTH_SHORT).show();
        });
    }

    private void disableDimBehind() {
        if (getDialog() == null) return;
        Window window = getDialog().getWindow();
        if (window != null) {
            WindowManager.LayoutParams params = window.getAttributes();
            params.dimAmount = 0.0f;
            params.flags |= WindowManager.LayoutParams.FLAG_DIM_BEHIND;
            window.setAttributes(params);
        }
    }

    private CalibrationManager.CalibrationData getCurrentParam() {
        CalibrationManager.CalibrationData param = (getContext() != null)
                ? CalibrationManager.load(requireContext())
                : new CalibrationManager.CalibrationData();
        if (mImageFusion != null) {
            param.offsetX = mImageFusion.getOffsetX();
            param.offsetY = mImageFusion.getOffsetY();
            param.scale = mImageFusion.getScale();
            param.rotation = mImageFusion.getRotation();
            param.highFreq = mImageFusion.getHighFreqRatio();
            param.colorTab = mImageFusion.getColorTab();
            param.fusionMode = mImageFusion.getMode();
            param.mirrorX = mImageFusion.isMirrorX();
            param.alignMode = mImageFusion.getAlignMode();
        }
        if (mThermalDevice != null) {
            param.fps = mThermalDevice.getFPS();
        }
        return param;
    }

    private void applyParamToDevice(CalibrationManager.CalibrationData data) {
        if (mImageFusion != null) {
            mImageFusion.setMirror(data.mirrorX, false);
            mImageFusion.setCalibration(data.offsetX, data.offsetY, data.scale, data.rotation);
            mImageFusion.setAlignMode(data.alignMode);
        }
        if (mThermalDevice != null) {
            mThermalDevice.setFPS(data.fps);
        }
    }

    private void showCameraControls() {
        CalibrationManager.CalibrationData currentParam = getCurrentParam();
        setAllControlParams(currentParam);
        setAllControlChangeListener();
    }

    private void setAllControlParams(CalibrationManager.CalibrationData param) {
        mBinding.switchMirrorX.setChecked(param.mirrorX);
        setSeekBarParams(
                mBinding.isbOffsetX,
                true,
                new int[]{-150, 150},
                param.offsetX);

        setSeekBarParams(
                mBinding.isbOffsetY,
                true,
                new int[]{-150, 150},
                param.offsetY);

        setSeekBarParams(
                mBinding.isbScale,
                true,
                new int[]{50, 150},
                Math.round(param.scale * 100.0f));

        setSeekBarParams(
                mBinding.isbRotation,
                true,
                new int[]{-15, 15},
                Math.round(param.rotation));

        setRadioGroup(
                mBinding.rgThermalFPS,
                true,
                new int[]{ ThermalDevice.FPS_4, ThermalDevice.FPS_8},
                param.fps);

        if (param.alignMode == ImageFusion.ALIGN_MODE_PCTVA) {
            mBinding.rbAlignPctva.setChecked(true);
        } else {
            mBinding.rbAlignTga.setChecked(true);
        }
    }

    private void setAllControlChangeListener() {
        mBinding.isbOffsetX.setOnSeekChangeListener(
                (MyOnSeekChangeListener) seekParams -> mImageFusion.setCalibration(
                        seekParams.progress,
                        mImageFusion.getOffsetY(),
                        mImageFusion.getScale(),
                        mImageFusion.getRotation()
                ));

        mBinding.isbOffsetY.setOnSeekChangeListener(
                (MyOnSeekChangeListener) seekParams -> mImageFusion.setCalibration(
                        mImageFusion.getOffsetX(),
                        seekParams.progress,
                        mImageFusion.getScale(),
                        mImageFusion.getRotation()
                ));

        mBinding.isbScale.setOnSeekChangeListener(
                (MyOnSeekChangeListener) seekParams -> mImageFusion.setCalibration(
                        mImageFusion.getOffsetX(),
                        mImageFusion.getOffsetY(),
                        seekParams.progress / 100.0f,
                        mImageFusion.getRotation()
                ));

        mBinding.isbRotation.setOnSeekChangeListener(
                (MyOnSeekChangeListener) seekParams -> mImageFusion.setCalibration(
                        mImageFusion.getOffsetX(),
                        mImageFusion.getOffsetY(),
                        mImageFusion.getScale(),
                        (float) seekParams.progress
                ));

        mBinding.rgThermalFPS.setOnCheckedChangeListener((group, checkedId) -> {
            int fps;
            if (checkedId == R.id.rb8Hz) {
                fps = ThermalDevice.FPS_4;
            } else {
                fps = ThermalDevice.FPS_8;
            }
            mThermalDevice.setFPS(fps);
        });

        mBinding.rgAlignMode.setOnCheckedChangeListener((group, checkedId) -> {
            int mode = (checkedId == R.id.rbAlignPctva) ? ImageFusion.ALIGN_MODE_PCTVA : ImageFusion.ALIGN_MODE_TGA;
            if (mImageFusion != null) {
                mImageFusion.setAlignMode(mode);
            }
        });
    }

    private void setSeekBarParams(IndicatorSeekBar seekBar, boolean isEnable, int[] limit, int value) {
        seekBar.setEnabled(isEnable);
        if (isEnable) {
            if (limit[0] < seekBar.getMin()) {
                seekBar.setMin(limit[0]);
                seekBar.setMax(limit[1]);
            } else {
                seekBar.setMax(limit[1]);
                seekBar.setMin(limit[0]);
            }
            seekBar.setProgress(value);
        }
    }

    private void setRadioGroup(RadioGroup radioGroup, boolean isEnable, int[] limit, int value) {
        radioGroup.setEnabled(isEnable);
        if (isEnable) {
            for (int i = 0; i < limit.length; i++) {
                if (limit[i] == value) {
                    RadioButton rb = (RadioButton) radioGroup.getChildAt(i);
                    if (rb != null) {
                        rb.setChecked(true);
                    }
                }
            }
        }
    }

    interface MyOnSeekChangeListener extends OnSeekChangeListener {
        @Override
        default void onStartTrackingTouch(IndicatorSeekBar seekBar) {}

        @Override
        default void onStopTrackingTouch(IndicatorSeekBar seekBar) {}
    }
}

