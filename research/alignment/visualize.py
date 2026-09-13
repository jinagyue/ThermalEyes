"""
Visualization Engine for Thermal Contour Chamfer Alignment (TCCA).
Generates diagnostic figures:
  1. rgb_raw.png (Original RGB)
  2. thermal_mask.png (Segmented thermal mask & native contour)
  3. rgb_edge.png (Canny edge map)
  4. rgb_distance.png (Euclidean distance transform field)
  5. contour_overlay.png (Matched thermal contour overlaid on RGB)
  6. fusion_comparison.png (Before vs. After fusion side-by-side)
  7. tcca_dashboard.png (Consolidated 6-panel diagnostic sheet)
"""

import os
import cv2
import numpy as np


class TCCAVisualizer:
    def __init__(self, output_dir=None):
        self.output_dir = output_dir

    def create_fusion_image(self, rgb_bgr, thermal_visual, dx, dy, scale, alpha=0.55):
        """
        Warps the thermal visual by (scale, dx, dy) and blends with RGB.
        """
        h, w = rgb_bgr.shape[:2]
        cx, cy = w / 2.0, h / 2.0

        # Construct 2x3 affine matrix for scale around center + translation
        # p' = s * (p - c) + c + t = s*p + (c - s*c + t)
        M = np.array([
            [scale, 0.0, (1.0 - scale) * cx + dx],
            [0.0, scale, (1.0 - scale) * cy + dy]
        ], dtype=np.float32)

        warped_therm = cv2.warpAffine(
            thermal_visual, M, (w, h),
            flags=cv2.INTER_LINEAR,
            borderMode=cv2.BORDER_CONSTANT,
            borderValue=(0, 0, 0)
        )

        # High-frequency detail injection (Laplacian of RGB)
        rgb_gray = cv2.cvtColor(rgb_bgr, cv2.COLOR_BGR2GRAY)
        lap = cv2.Laplacian(rgb_gray, cv2.CV_32F, ksize=3)
        lap_norm = np.clip(np.abs(lap) * 1.5, 0, 255).astype(np.uint8)
        lap_bgr = cv2.cvtColor(lap_norm, cv2.COLOR_GRAY2BGR)

        # Alpha blend warped thermal with RGB
        blended = cv2.addWeighted(rgb_bgr, 1.0 - alpha, warped_therm, alpha, 0)
        # Inject visible high frequency outlines for MSX look
        fusion = cv2.addWeighted(blended, 0.85, lap_bgr, 0.35, 0)
        return fusion

    def generate_all(self, sample_id, rgb_data, thermal_data, align_result, out_dir=None):
        """
        Renders and saves the full diagnostic suite for a sample.
        """
        save_dir = out_dir or self.output_dir or "."
        os.makedirs(save_dir, exist_ok=True)

        rgb_raw = rgb_data["rgb_raw"]
        dist_visual = rgb_data["dist_visual"]
        edges = rgb_data["edges"]

        therm_mask_rgb = thermal_data["binary_mask_rgb"]
        therm_visual = thermal_data["thermal_norm_visual"]
        initial_pts = thermal_data["contour_pts_rgb"]
        matched_pts = align_result["transformed_pts"]

        h, w = rgb_raw.shape[:2]

        # 1. Save rgb_raw.png
        cv2.imwrite(os.path.join(save_dir, "rgb_raw.png"), rgb_raw)

        # 2. Save thermal_mask.png (mask overlaid on jet thermal visual)
        mask_overlay = therm_visual.copy()
        mask_contours, _ = cv2.findContours(therm_mask_rgb, cv2.RETR_EXTERNAL, cv2.CHAIN_APPROX_SIMPLE)
        cv2.drawContours(mask_overlay, mask_contours, -1, (255, 255, 255), 2)
        cv2.imwrite(os.path.join(save_dir, "thermal_mask.png"), mask_overlay)

        # 3. Save rgb_edge.png
        edge_bgr = cv2.cvtColor(edges, cv2.COLOR_GRAY2BGR)
        cv2.imwrite(os.path.join(save_dir, "rgb_edge.png"), edge_bgr)

        # 4. Save rgb_distance.png
        cv2.imwrite(os.path.join(save_dir, "rgb_distance.png"), dist_visual)

        # 5. Save contour_overlay.png
        overlay_img = rgb_raw.copy()
        # Draw initial (unaligned) contour in RED
        if len(initial_pts) > 2:
            pts_init_i = np.round(initial_pts).astype(np.int32).reshape((-1, 1, 2))
            cv2.polylines(overlay_img, [pts_init_i], isClosed=True, color=(0, 0, 255), thickness=2)
            for p in initial_pts[::3]:
                cv2.circle(overlay_img, (int(p[0]), int(p[1])), 3, (0, 0, 255), -1)

        # Draw matched (aligned) contour in BRIGHT GREEN
        if len(matched_pts) > 2:
            pts_match_i = np.round(matched_pts).astype(np.int32).reshape((-1, 1, 2))
            cv2.polylines(overlay_img, [pts_match_i], isClosed=True, color=(0, 255, 0), thickness=2)
            for p in matched_pts[::3]:
                cv2.circle(overlay_img, (int(p[0]), int(p[1])), 3, (0, 255, 255), -1)

        # Legend
        cv2.putText(overlay_img, "RED: Initial Thermal Contour", (15, 30),
                    cv2.FONT_HERSHEY_SIMPLEX, 0.65, (0, 0, 255), 2)
        cv2.putText(overlay_img, "GREEN: TCCA Matched Contour", (15, 60),
                    cv2.FONT_HERSHEY_SIMPLEX, 0.65, (0, 255, 0), 2)
        cv2.imwrite(os.path.join(save_dir, "matched_contour_overlay.png"), overlay_img)

        # 6. Save fusion_comparison.png (Before vs After)
        fusion_before = self.create_fusion_image(rgb_raw, therm_visual, dx=0.0, dy=0.0, scale=1.0)
        fusion_after = self.create_fusion_image(
            rgb_raw, therm_visual,
            dx=align_result["best_dx"],
            dy=align_result["best_dy"],
            scale=align_result["best_scale"]
        )

        # Annotate before & after
        cv2.putText(fusion_before, "BEFORE (Parallax Misaligned: dx=0, dy=0)", (20, 40),
                    cv2.FONT_HERSHEY_SIMPLEX, 0.7, (0, 0, 255), 2)
        cv2.putText(fusion_after, f"AFTER (TCCA: dx={align_result['best_dx']:.1f}, dy={align_result['best_dy']:.1f}, s={align_result['best_scale']:.2f})",
                    (20, 40), cv2.FONT_HERSHEY_SIMPLEX, 0.7, (0, 255, 0), 2)

        comparison_img = np.hstack([fusion_before, fusion_after])
        cv2.imwrite(os.path.join(save_dir, "fusion_comparison.png"), comparison_img)

        # 7. Create consolidated 6-panel diagnostic dashboard
        dashboard = self._build_dashboard(
            rgb_raw, mask_overlay, edge_bgr, dist_visual, overlay_img, fusion_after,
            sample_id, align_result
        )
        dashboard_path = os.path.join(save_dir, "tcca_dashboard.png")
        cv2.imwrite(dashboard_path, dashboard)

        return {
            "rgb_raw_path": os.path.join(save_dir, "rgb_raw.png"),
            "thermal_mask_path": os.path.join(save_dir, "thermal_mask.png"),
            "rgb_edge_path": os.path.join(save_dir, "rgb_edge.png"),
            "rgb_distance_path": os.path.join(save_dir, "rgb_distance.png"),
            "contour_overlay_path": os.path.join(save_dir, "matched_contour_overlay.png"),
            "fusion_comparison_path": os.path.join(save_dir, "fusion_comparison.png"),
            "dashboard_path": dashboard_path
        }

    def _build_dashboard(self, p1, p2, p3, p4, p5, p6, sample_id, align_res):
        """Builds a 2x3 grid dashboard with informative banner."""
        thumb_w, thumb_h = 480, 360

        def prep(img, title):
            res = cv2.resize(img, (thumb_w, thumb_h))
            cv2.rectangle(res, (0, 0), (thumb_w, 32), (30, 30, 30), -1)
            cv2.putText(res, title, (10, 22), cv2.FONT_HERSHEY_SIMPLEX, 0.6, (255, 255, 255), 1)
            return res

        r1 = np.hstack([
            prep(p1, "1. Visible RGB (640x480)"),
            prep(p2, "2. Thermal Mask (MLX90640 32x24)"),
            prep(p3, "3. Canny Visible Edges")
        ])
        r2 = np.hstack([
            prep(p4, "4. Distance Transform Map"),
            prep(p5, "5. TCCA Contour Alignment Overlay"),
            prep(p6, "6. Dual-Spectrum Fused Result")
        ])
        grid = np.vstack([r1, r2])

        # Header banner
        banner_h = 75
        total_w = grid.shape[1]
        banner = np.full((banner_h, total_w, 3), 40, dtype=np.uint8)

        title_txt = f"TCCA Verification Platform: {sample_id}"
        metrics_txt = (
            f"Est: dx={align_res['best_dx']:.1f}px, dy={align_res['best_dy']:.1f}px, scale={align_res['best_scale']:.2f} | "
            f"Chamfer E={align_res['best_score']:.2f}px | Inliers={align_res['inlier_ratio']*100:.1f}% | Time={align_res['elapsed_ms']:.1f}ms"
        )

        cv2.putText(banner, title_txt, (20, 30), cv2.FONT_HERSHEY_SIMPLEX, 0.8, (0, 255, 255), 2)
        cv2.putText(banner, metrics_txt, (20, 60), cv2.FONT_HERSHEY_SIMPLEX, 0.6, (220, 220, 220), 1)

        dashboard = np.vstack([banner, grid])
        return dashboard


if __name__ == "__main__":
    print("TCCAVisualizer module ready.")
