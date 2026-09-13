"""
Thermal Contour Extractor for MLX90640 (32x24) -> Visible (640x480).
Handles sensor hardware normalization, temperature normalization, adaptive Otsu thresholding,
morphology cleanup, and uniform perimeter contour point sampling.
"""

import numpy as np
import cv2


class ThermalContourExtractor:
    def __init__(self, rgb_w=640, rgb_h=480, therm_w=32, therm_h=24, num_sample_pts=45):
        self.rgb_w = rgb_w
        self.rgb_h = rgb_h
        self.therm_w = therm_w
        self.therm_h = therm_h
        self.scale_x = rgb_w / therm_w  # 20.0
        self.scale_y = rgb_h / therm_h  # 20.0
        self.num_sample_pts = num_sample_pts

    def extract(self, thermal_source, flip_horizontal=True):
        """
        Extracts target contour from 32x24 thermal data.
        
        Args:
            thermal_source: np.ndarray (32x24) float or path to .npy file.
            flip_horizontal: bool, whether to horizontally flip raw MLX90640 frame.
        
        Returns:
            dict containing:
              - valid: bool
              - status: str
              - contour_pts_rgb: np.ndarray of shape (N, 2), points in 640x480 space
              - centroid_rgb: (cx, cy) in 640x480 space
              - bbox_rgb: (x, y, w, h) in 640x480 space
              - binary_mask_native: np.ndarray (24, 32) uint8
              - binary_mask_rgb: np.ndarray (480, 640) uint8
              - thermal_norm_visual: np.ndarray (480, 640, 3) BGR pseudocolor
              - contrast: float (°C)
        """
        if isinstance(thermal_source, str):
            thermal_raw = np.load(thermal_source)
        else:
            thermal_raw = np.array(thermal_source, dtype=np.float32)

        # 1. Hardware sensor normalization (MLX90640 horizontal mirror flip)
        if flip_horizontal:
            therm = cv2.flip(thermal_raw, 1)
        else:
            therm = thermal_raw.copy()

        # 2. Dynamic range and contrast check
        min_temp = float(np.min(therm))
        max_temp = float(np.max(therm))
        contrast = max_temp - min_temp

        if contrast < 2.0:
            return {
                "valid": False,
                "status": "LOW_CONTRAST",
                "contrast": contrast,
                "contour_pts_rgb": np.zeros((0, 2), dtype=np.float32),
                "centroid_rgb": (self.rgb_w / 2, self.rgb_h / 2),
                "bbox_rgb": (0, 0, 0, 0),
                "binary_mask_native": np.zeros((self.therm_h, self.therm_w), dtype=np.uint8),
                "binary_mask_rgb": np.zeros((self.rgb_h, self.rgb_w), dtype=np.uint8),
                "thermal_norm_visual": np.zeros((self.rgb_h, self.rgb_w, 3), dtype=np.uint8)
            }

        # Temperature normalization to 0-255 uint8
        therm_norm = np.clip((therm - min_temp) / (contrast + 1e-6) * 255.0, 0, 255).astype(np.uint8)

        # 3. Smoothing on native 32x24 grid (median filter to suppress dead/noisy pixels)
        smooth = cv2.medianBlur(therm_norm, 3)

        # 4. Adaptive segmentation with Otsu + lower extremity floor
        otsu_val, _ = cv2.threshold(smooth, 0, 255, cv2.THRESH_BINARY + cv2.THRESH_OTSU)
        # Extremity preservation floor: prevent finger/limb truncation
        floor_thresh = int(max(15, otsu_val * 0.70))
        _, binary_mask = cv2.threshold(smooth, floor_thresh, 255, cv2.THRESH_BINARY)

        # 5. Morphological cleanup
        kernel = cv2.getStructuringElement(cv2.MORPH_ELLIPSE, (3, 3))
        binary_mask = cv2.morphologyEx(binary_mask, cv2.MORPH_CLOSE, kernel)
        binary_mask = cv2.morphologyEx(binary_mask, cv2.MORPH_OPEN, kernel)

        # 6. Target FOV overflow check
        total_pixels = self.therm_w * self.therm_h
        fg_pixels = int(np.count_nonzero(binary_mask))
        fill_ratio = fg_pixels / total_pixels

        if fill_ratio > 0.85:
            return {
                "valid": False,
                "status": "TARGET_CLIPPED",
                "contrast": contrast,
                "contour_pts_rgb": np.zeros((0, 2), dtype=np.float32),
                "centroid_rgb": (self.rgb_w / 2, self.rgb_h / 2),
                "bbox_rgb": (0, 0, 0, 0),
                "binary_mask_native": binary_mask,
                "binary_mask_rgb": cv2.resize(binary_mask, (self.rgb_w, self.rgb_h), interpolation=cv2.INTER_NEAREST),
                "thermal_norm_visual": cv2.applyColorMap(
                    cv2.resize(therm_norm, (self.rgb_w, self.rgb_h), interpolation=cv2.INTER_CUBIC),
                    cv2.COLORMAP_JET
                )
            }

        # 7. Contour extraction on native grid
        contours, _ = cv2.findContours(binary_mask, cv2.RETR_EXTERNAL, cv2.CHAIN_APPROX_NONE)
        if not contours:
            return {
                "valid": False,
                "status": "NO_TARGET",
                "contrast": contrast,
                "contour_pts_rgb": np.zeros((0, 2), dtype=np.float32),
                "centroid_rgb": (self.rgb_w / 2, self.rgb_h / 2),
                "bbox_rgb": (0, 0, 0, 0),
                "binary_mask_native": binary_mask,
                "binary_mask_rgb": cv2.resize(binary_mask, (self.rgb_w, self.rgb_h), interpolation=cv2.INTER_NEAREST),
                "thermal_norm_visual": cv2.applyColorMap(
                    cv2.resize(therm_norm, (self.rgb_w, self.rgb_h), interpolation=cv2.INTER_CUBIC),
                    cv2.COLORMAP_JET
                )
            }

        # Largest contour by area
        best_contour = max(contours, key=cv2.contourArea)
        area = cv2.contourArea(best_contour)
        if area < 10.0 or len(best_contour) < 6:
            return {
                "valid": False,
                "status": "TARGET_TOO_SMALL",
                "contrast": contrast,
                "contour_pts_rgb": np.zeros((0, 2), dtype=np.float32),
                "centroid_rgb": (self.rgb_w / 2, self.rgb_h / 2),
                "bbox_rgb": (0, 0, 0, 0),
                "binary_mask_native": binary_mask,
                "binary_mask_rgb": cv2.resize(binary_mask, (self.rgb_w, self.rgb_h), interpolation=cv2.INTER_NEAREST),
                "thermal_norm_visual": cv2.applyColorMap(
                    cv2.resize(therm_norm, (self.rgb_w, self.rgb_h), interpolation=cv2.INTER_CUBIC),
                    cv2.COLORMAP_JET
                )
            }

        # 8. Transform contour points to 640x480 RGB coordinate system
        pts = best_contour.reshape(-1, 2).astype(np.float32)
        # Continuous perimeter interpolation for smooth sub-pixel contour in 640x480 space
        pts_rgb = np.zeros_like(pts)
        pts_rgb[:, 0] = pts[:, 0] * self.scale_x + (self.scale_x / 2.0)
        pts_rgb[:, 1] = pts[:, 1] * self.scale_y + (self.scale_y / 2.0)

        # Uniform perimeter resampling
        pts_sampled = self._resample_contour(pts_rgb, self.num_sample_pts)

        # Centroid
        M = cv2.moments(best_contour)
        if M["m00"] > 1e-4:
            cx_therm = M["m10"] / M["m00"]
            cy_therm = M["m01"] / M["m00"]
            cx_rgb = cx_therm * self.scale_x + (self.scale_x / 2.0)
            cy_rgb = cy_therm * self.scale_y + (self.scale_y / 2.0)
        else:
            cx_rgb = float(np.mean(pts_sampled[:, 0]))
            cy_rgb = float(np.mean(pts_sampled[:, 1]))

        # Bounding box in RGB space
        bx, by, bw, bh = cv2.boundingRect(best_contour)
        bbox_rgb = (
            int(bx * self.scale_x),
            int(by * self.scale_y),
            int(bw * self.scale_x),
            int(bh * self.scale_y)
        )

        # Visuals
        binary_mask_rgb = cv2.resize(binary_mask, (self.rgb_w, self.rgb_h), interpolation=cv2.INTER_NEAREST)
        thermal_visual = cv2.applyColorMap(
            cv2.resize(therm_norm, (self.rgb_w, self.rgb_h), interpolation=cv2.INTER_CUBIC),
            cv2.COLORMAP_JET
        )

        return {
            "valid": True,
            "status": "OK",
            "contrast": contrast,
            "min_temp": min_temp,
            "max_temp": max_temp,
            "area": area,
            "contour_pts_rgb": pts_sampled,
            "centroid_rgb": (float(cx_rgb), float(cy_rgb)),
            "bbox_rgb": bbox_rgb,
            "binary_mask_native": binary_mask,
            "binary_mask_rgb": binary_mask_rgb,
            "thermal_norm_visual": thermal_visual
        }

    def _resample_contour(self, pts, target_count):
        """Uniformly resamples a closed polygon to target_count points along perimeter."""
        if len(pts) < 3:
            return pts

        # Close loop
        pts_closed = np.vstack([pts, pts[0:1]])
        diffs = np.diff(pts_closed, axis=0)
        seg_lens = np.hypot(diffs[:, 0], diffs[:, 1])
        cum_lens = np.insert(np.cumsum(seg_lens), 0, 0.0)
        total_len = cum_lens[-1]

        if total_len < 1e-3:
            return pts

        target_distances = np.linspace(0.0, total_len, target_count, endpoint=False)
        resampled_x = np.interp(target_distances, cum_lens, pts_closed[:, 0])
        resampled_y = np.interp(target_distances, cum_lens, pts_closed[:, 1])

        return np.column_stack([resampled_x, resampled_y]).astype(np.float32)


if __name__ == "__main__":
    import os
    sample_npy = os.path.join(os.path.dirname(__file__), "data", "sample_01_0.2m_hand", "thermal.npy")
    if os.path.exists(sample_npy):
        extractor = ThermalContourExtractor()
        res = extractor.extract(sample_npy)
        print("Extractor self-test:")
        print("  Status:", res["status"])
        print("  Valid:", res["valid"])
        print("  Centroid:", res["centroid_rgb"])
        print("  Points count:", len(res["contour_pts_rgb"]))
        print("  Contrast:", f"{res['contrast']:.2f}°C")
    else:
        print(f"Sample file not found: {sample_npy}")
