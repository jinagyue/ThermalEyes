"""
Thermal Contour Chamfer Alignment (TCCA) Matching Engine.
Performs 3D parameter search (scale s, dx, dy) over Euclidean Distance Transform D(x, y)
to minimize the Chamfer distance error E = mean(D(p'_i)).
"""

import time
import numpy as np
import cv2


class ChamferAligner:
    def __init__(self,
                 scale_range=(0.70, 1.40),
                 scale_step_coarse=0.05,
                 scale_step_fine=0.02,
                 dx_range=(-150, 150),
                 dx_step_coarse=4,
                 dx_step_fine=1,
                 dy_range=(-80, 80),
                 dy_step_coarse=4,
                 dy_step_fine=1,
                 out_of_bounds_penalty=60.0):
        self.scale_min, self.scale_max = scale_range
        self.scale_step_coarse = scale_step_coarse
        self.scale_step_fine = scale_step_fine
        self.dx_min, self.dx_max = dx_range
        self.dx_step_coarse = dx_step_coarse
        self.dx_step_fine = dx_step_fine
        self.dy_min, self.dy_max = dy_range
        self.dy_step_coarse = dy_step_coarse
        self.dy_step_fine = dy_step_fine
        self.out_of_bounds_penalty = out_of_bounds_penalty

    def align(self, contour_pts, centroid, dist_map, physics_prior=None, lambda_phys=0.0):
        """
        Executes Chamfer matching between thermal contour points and RGB distance map.
        
        Args:
            contour_pts: np.ndarray (N, 2), coordinates in 640x480 space
            centroid: (cx, cy) tuple in 640x480 space
            dist_map: np.ndarray (H, W) float32 Euclidean distance transform
            physics_prior: dict with 'expected_dx' (optional physical constraint)
            lambda_phys: weight of physical constraint (0.0 for pure TCCA)
            
        Returns:
            dict containing:
              - best_scale: float
              - best_dx: float
              - best_dy: float
              - best_score: float (mean Chamfer distance in px)
              - confidence: float in [0, 1]
              - inlier_ratio: float
              - transformed_pts: np.ndarray (N, 2)
              - elapsed_ms: float
              - search_history: list of top candidates
        """
        start_time = time.perf_counter()
        h, w = dist_map.shape
        num_pts = len(contour_pts)

        if num_pts < 4:
            return {
                "success": False,
                "status": "TOO_FEW_CONTOUR_POINTS",
                "best_scale": 1.0,
                "best_dx": 0.0,
                "best_dy": 0.0,
                "best_score": 999.0,
                "confidence": 0.0,
                "inlier_ratio": 0.0,
                "transformed_pts": contour_pts,
                "elapsed_ms": 0.0
            }

        cx, cy = centroid
        # Center points at origin for scale invariance around centroid
        rel_pts = contour_pts - np.array([cx, cy], dtype=np.float32)

        # -------------------------------------------------------------
        # 1. Coarse Grid Search
        # -------------------------------------------------------------
        scales_coarse = np.arange(self.scale_min, self.scale_max + 1e-4, self.scale_step_coarse)
        dx_coarse = np.arange(self.dx_min, self.dx_max + 1, self.dx_step_coarse)
        dy_coarse = np.arange(self.dy_min, self.dy_max + 1, self.dy_step_coarse)

        best_score = float("inf")
        best_scale = 1.0
        best_dx = 0.0
        best_dy = 0.0
        top_candidates = []

        for s in scales_coarse:
            scaled_rel = rel_pts * s
            pts_base = scaled_rel + np.array([cx, cy], dtype=np.float32)

            for dy in dy_coarse:
                for dx in dx_coarse:
                    px = pts_base[:, 0] + dx
                    py = pts_base[:, 1] + dy

                    ix = np.round(px).astype(np.int32)
                    iy = np.round(py).astype(np.int32)

                    valid_mask = (ix >= 0) & (ix < w) & (iy >= 0) & (iy < h)
                    valid_count = np.count_nonzero(valid_mask)

                    if valid_count < (num_pts * 0.4):
                        continue

                    valid_dist = dist_map[iy[valid_mask], ix[valid_mask]]
                    # Penalize points falling outside the image frame
                    out_count = num_pts - valid_count
                    score = (np.sum(valid_dist) + out_count * self.out_of_bounds_penalty) / num_pts

                    # Physics prior regularizer: lambda * ((dx - expected_dx)^2 + (dy - expected_dy)^2)
                    if physics_prior is not None and lambda_phys > 0.0:
                        exp_dx = physics_prior.get("expected_dx", dx)
                        exp_dy = physics_prior.get("expected_dy", -5.0)
                        score += lambda_phys * (((dx - exp_dx) / 15.0) ** 2 + 2.0 * ((dy - exp_dy) / 6.0) ** 2)

                    if score < best_score:
                        best_score = score
                        best_scale = float(s)
                        best_dx = float(dx)
                        best_dy = float(dy)
                        top_candidates.append((score, float(s), float(dx), float(dy)))

        # -------------------------------------------------------------
        # 2. Fine Grid Search around coarse optimum
        # -------------------------------------------------------------
        fine_s_min = max(self.scale_min, best_scale - self.scale_step_coarse)
        fine_s_max = min(self.scale_max, best_scale + self.scale_step_coarse)
        scales_fine = np.arange(fine_s_min, fine_s_max + 1e-4, self.scale_step_fine)

        fine_dx_min = max(self.dx_min, int(best_dx - self.dx_step_coarse))
        fine_dx_max = min(self.dx_max, int(best_dx + self.dx_step_coarse))
        dx_fine = np.arange(fine_dx_min, fine_dx_max + 1, self.dx_step_fine)

        fine_dy_min = max(self.dy_min, int(best_dy - self.dy_step_coarse))
        fine_dy_max = min(self.dy_max, int(best_dy + self.dy_step_coarse))
        dy_fine = np.arange(fine_dy_min, fine_dy_max + 1, self.dy_step_fine)

        for s in scales_fine:
            scaled_rel = rel_pts * s
            pts_base = scaled_rel + np.array([cx, cy], dtype=np.float32)

            for dy in dy_fine:
                for dx in dx_fine:
                    px = pts_base[:, 0] + dx
                    py = pts_base[:, 1] + dy

                    ix = np.round(px).astype(np.int32)
                    iy = np.round(py).astype(np.int32)

                    valid_mask = (ix >= 0) & (ix < w) & (iy >= 0) & (iy < h)
                    valid_count = np.count_nonzero(valid_mask)

                    if valid_count < (num_pts * 0.4):
                        continue

                    valid_dist = dist_map[iy[valid_mask], ix[valid_mask]]
                    out_count = num_pts - valid_count
                    score = (np.sum(valid_dist) + out_count * self.out_of_bounds_penalty) / num_pts

                    if physics_prior is not None and lambda_phys > 0.0:
                        exp_dx = physics_prior.get("expected_dx", dx)
                        exp_dy = physics_prior.get("expected_dy", -5.0)
                        score += lambda_phys * (((dx - exp_dx) / 15.0) ** 2 + 2.0 * ((dy - exp_dy) / 6.0) ** 2)

                    if score < best_score:
                        best_score = score
                        best_scale = float(s)
                        best_dx = float(dx)
                        best_dy = float(dy)

        # -------------------------------------------------------------
        # 3. Compute Final Transformed Points and Metrics
        # -------------------------------------------------------------
        final_pts = rel_pts * best_scale + np.array([cx, cy], dtype=np.float32)
        final_pts[:, 0] += best_dx
        final_pts[:, 1] += best_dy

        # Inlier ratio: points within 4px of an edge
        ix = np.clip(np.round(final_pts[:, 0]).astype(np.int32), 0, w - 1)
        iy = np.clip(np.round(final_pts[:, 1]).astype(np.int32), 0, h - 1)
        final_dists = dist_map[iy, ix]
        inliers = np.count_nonzero(final_dists <= 4.0)
        inlier_ratio = float(inliers / num_pts)

        # Confidence: sigmoid / softmin score
        confidence = float(1.0 / (1.0 + (best_score / 4.0)))

        elapsed_ms = (time.perf_counter() - start_time) * 1000.0

        return {
            "success": True,
            "status": "OK",
            "best_scale": best_scale,
            "best_dx": best_dx,
            "best_dy": best_dy,
            "best_score": float(best_score),
            "confidence": confidence,
            "inlier_ratio": inlier_ratio,
            "transformed_pts": final_pts,
            "elapsed_ms": elapsed_ms,
            "top_candidates": top_candidates[-5:]
        }


if __name__ == "__main__":
    import os
    from thermal_contour import ThermalContourExtractor
    from rgb_edge import RGBEdgeProcessor

    data_dir = os.path.join(os.path.dirname(__file__), "data", "sample_01_0.2m_hand")
    if os.path.exists(data_dir):
        ext = ThermalContourExtractor()
        tc = ext.extract(os.path.join(data_dir, "thermal.npy"))
        
        proc = RGBEdgeProcessor()
        rp = proc.process(os.path.join(data_dir, "rgb.png"))

        aligner = ChamferAligner()
        res = aligner.align(tc["contour_pts_rgb"], tc["centroid_rgb"], rp["dist_map"])

        print("Chamfer Aligner self-test:")
        print("  Status:", res["status"])
        print("  Best scale:", res["best_scale"])
        print("  Best dx:", res["best_dx"], "(GT: 112.0)")
        print("  Best dy:", res["best_dy"], "(GT: -5.0)")
        print("  Best score (px):", f"{res['best_score']:.2f}")
        print("  Confidence:", f"{res['confidence']:.3f}")
        print("  Inlier ratio:", f"{res['inlier_ratio'] * 100:.1f}%")
        print("  Elapsed ms:", f"{res['elapsed_ms']:.1f}ms")
    else:
        print(f"Data dir not found: {data_dir}")
