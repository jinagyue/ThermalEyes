"""
RGB Edge and Distance Transform Field Generator.
Computes Canny edges and Euclidean Distance Transform map D(x, y) on 640x480 RGB frames.
"""

import numpy as np
import cv2


class RGBEdgeProcessor:
    def __init__(self, blur_ksize=(5, 5), blur_sigma=1.2, canny_low=40, canny_high=120):
        self.blur_ksize = blur_ksize
        self.blur_sigma = blur_sigma
        self.canny_low = canny_low
        self.canny_high = canny_high

    def process(self, rgb_source):
        """
        Processes RGB image to extract edges and Euclidean distance transform.
        
        Args:
            rgb_source: np.ndarray (480, 640, 3) or path to image file.
            
        Returns:
            dict containing:
              - rgb_raw: (480, 640, 3) BGR image
              - gray: (480, 640) uint8
              - edges: (480, 640) uint8 binary edge map
              - dist_map: (480, 640) float32 Euclidean distance map (0 at edges)
              - dist_visual: (480, 640, 3) colormapped visualization of distance field
              - edge_visual: (480, 640, 3) edge overlay visualization
        """
        if isinstance(rgb_source, str):
            rgb_raw = cv2.imread(rgb_source)
            if rgb_raw is None:
                raise ValueError(f"Failed to read image from {rgb_source}")
        else:
            rgb_raw = rgb_source.copy()

        # 1. Grayscale
        if len(rgb_raw.shape) == 3:
            gray = cv2.cvtColor(rgb_raw, cv2.COLOR_BGR2GRAY)
        else:
            gray = rgb_raw.copy()
            rgb_raw = cv2.cvtColor(gray, cv2.COLOR_GRAY2BGR)

        # 2. Gaussian Blur to suppress sensor noise
        blurred = cv2.GaussianBlur(gray, self.blur_ksize, self.blur_sigma)

        # 3. Canny Edge Detection
        edges = cv2.Canny(blurred, self.canny_low, self.canny_high)

        # 4. Euclidean Distance Transform (L2 norm)
        # Inverted edges: 0 at edges, 255 in background
        dist_map = cv2.distanceTransform(~edges, cv2.DIST_L2, 3)

        # 5. Distance visualization: clamp to 0-50 px range for high contrast rendering
        dist_clamped = np.clip(dist_map / 50.0 * 255.0, 0, 255).astype(np.uint8)
        dist_visual = cv2.applyColorMap(dist_clamped, cv2.COLORMAP_TURBO)

        # Edge visualization (green edges on dim grayscale)
        edge_visual = cv2.cvtColor(gray // 2, cv2.COLOR_GRAY2BGR)
        edge_visual[edges > 0] = [0, 255, 0]

        return {
            "rgb_raw": rgb_raw,
            "gray": gray,
            "edges": edges,
            "dist_map": dist_map,
            "dist_visual": dist_visual,
            "edge_visual": edge_visual
        }


if __name__ == "__main__":
    import os
    sample_png = os.path.join(os.path.dirname(__file__), "data", "sample_01_0.2m_hand", "rgb.png")
    if os.path.exists(sample_png):
        proc = RGBEdgeProcessor()
        res = proc.process(sample_png)
        print("RGB Processor self-test:")
        print("  Raw shape:", res["rgb_raw"].shape)
        print("  Edges non-zero:", np.count_nonzero(res["edges"]))
        print("  Dist map range: [", np.min(res["dist_map"]), ",", np.max(res["dist_map"]), "]")
    else:
        print(f"Sample file not found: {sample_png}")
