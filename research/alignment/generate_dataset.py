"""
Dataset generator for Thermal Contour Chamfer Alignment (TCCA) offline verification.
Generates physically consistent multi-distance test pairs of:
  - rgb.png (640x480, visible light image with natural edges)
  - thermal.npy (32x24, float32 temperature matrix in Celsius, sensor-mirrored)
  - ground_truth.json (ground truth dx, dy, scale, distance)
"""

import os
import json
import numpy as np
import cv2


def create_hand_mask(size, center, scale=1.0):
    """Draws a stylized hand silhouette (palm + 5 fingers) on a binary mask."""
    w, h = size
    mask = np.zeros((h, w), dtype=np.uint8)
    cx, cy = int(center[0]), int(center[1])
    s = scale

    # Palm (ellipse)
    cv2.ellipse(mask, (cx, cy + int(15 * s)), (int(35 * s), int(42 * s)), 0, 0, 360, 255, -1)

    # 5 Fingers
    fingers = [
        # (offset_x, offset_y, angle_deg, length, width)
        (-32 * s, -10 * s, -45, 38 * s, 11 * s),  # Thumb
        (-18 * s, -42 * s, -12, 52 * s, 10 * s),  # Index
        (0 * s, -52 * s, 0, 60 * s, 10 * s),     # Middle
        (18 * s, -45 * s, 10, 54 * s, 10 * s),    # Ring
        (32 * s, -28 * s, 25, 40 * s, 9 * s),     # Little
    ]

    for ox, oy, ang, length, width in fingers:
        fx = int(cx + ox)
        fy = int(cy + oy)
        cv2.ellipse(mask, (fx, fy), (int(width), int(length)), ang, 0, 360, 255, -1)

    # Smooth contour
    mask = cv2.GaussianBlur(mask, (9, 9), 2.5)
    _, mask = cv2.threshold(mask, 127, 255, cv2.THRESH_BINARY)
    return mask


def create_device_mask(size, center, scale=1.0):
    """Draws a stylized electronic device (chassis with heatsink fins/ports)."""
    w, h = size
    mask = np.zeros((h, w), dtype=np.uint8)
    cx, cy = int(center[0]), int(center[1])
    s = scale

    # Main chassis
    bw, bh = int(100 * s), int(65 * s)
    cv2.rectangle(mask, (cx - bw, cy - bh), (cx + bw, cy + bh), 255, -1)

    # Top cooling fins / antenna
    cv2.rectangle(mask, (cx - int(70 * s), cy - bh - int(18 * s)), (cx - int(40 * s), cy - bh), 255, -1)
    cv2.rectangle(mask, (cx + int(40 * s), cy - bh - int(18 * s)), (cx + int(70 * s), cy - bh), 255, -1)
    cv2.rectangle(mask, (cx - int(15 * s), cy - bh - int(25 * s)), (cx + int(15 * s), cy - bh), 255, -1)

    # Side bracket
    cv2.rectangle(mask, (cx + bw, cy - int(20 * s)), (cx + bw + int(15 * s), cy + int(20 * s)), 255, -1)

    mask = cv2.GaussianBlur(mask, (5, 5), 1.0)
    _, mask = cv2.threshold(mask, 127, 255, cv2.THRESH_BINARY)
    return mask


def create_body_mask(size, center, scale=1.0):
    """Draws a human head, neck and upper torso silhouette."""
    w, h = size
    mask = np.zeros((h, w), dtype=np.uint8)
    cx, cy = int(center[0]), int(center[1])
    s = scale

    # Head
    cv2.ellipse(mask, (cx, cy - int(90 * s)), (int(32 * s), int(42 * s)), 0, 0, 360, 255, -1)
    # Neck
    cv2.rectangle(mask, (cx - int(14 * s), cy - int(55 * s)), (cx + int(14 * s), cy - int(35 * s)), 255, -1)
    # Torso / Shoulders
    cv2.ellipse(mask, (cx, cy + int(45 * s)), (int(95 * s), int(85 * s)), 0, 180, 360, 255, -1)
    cv2.rectangle(mask, (cx - int(95 * s), cy + int(40 * s)), (cx + int(95 * s), cy + int(140 * s)), 255, -1)

    mask = cv2.GaussianBlur(mask, (7, 7), 1.5)
    _, mask = cv2.threshold(mask, 127, 255, cv2.THRESH_BINARY)
    return mask


def generate_sample(sample_dir, sample_type, distance_m, gt_dx, gt_dy, gt_scale=1.0, add_clutter=False):
    """
    Generates a synchronized pair:
      - 640x480 RGB image with realistic visible edges
      - 32x24 thermal float array (with hardware sensor horizontal mirroring)
    """
    os.makedirs(sample_dir, exist_ok=True)

    rgb_w, rgb_h = 640, 480
    therm_w, therm_h = 32, 24
    scale_factor = rgb_w / therm_w  # 20.0

    # Base target position in RGB image
    cam_cx = 320.0
    cam_cy = 240.0

    # In RGB image:
    rgb_img = np.full((rgb_h, rgb_w, 3), 195, dtype=np.uint8)

    # Background wallpaper/table gradient & lines
    for y in range(rgb_h):
        rgb_img[y, :, :] = np.clip(180 + (y // 15) * 2, 0, 255)
    for x in range(0, rgb_w, 60):
        cv2.line(rgb_img, (x, 0), (x, rgb_h), (160, 160, 160), 1)

    if add_clutter:
        # Add background cluttered objects: monitor frame, bookshelf, desk items
        cv2.rectangle(rgb_img, (30, 40), (220, 260), (90, 90, 90), 2)
        cv2.rectangle(rgb_img, (450, 70), (610, 420), (120, 100, 80), 2)
        for ly in range(110, 410, 40):
            cv2.line(rgb_img, (450, ly), (610, ly), (100, 80, 70), 1)
        # Random keyboard / notebook stripes
        cv2.rectangle(rgb_img, (80, 320), (290, 450), (70, 70, 70), 2)
        for kx in range(95, 280, 25):
            cv2.line(rgb_img, (kx, 335), (kx, 435), (140, 140, 140), 1)

    # Render target in RGB
    if sample_type == "hand":
        rgb_mask = create_hand_mask((rgb_w, rgb_h), (cam_cx, cam_cy), scale=gt_scale * 1.3)
        target_color = (130, 160, 220)  # Skin tone in BGR
    elif sample_type == "device":
        rgb_mask = create_device_mask((rgb_w, rgb_h), (cam_cx, cam_cy), scale=gt_scale * 1.2)
        target_color = (60, 60, 65)     # Dark metallic chassis
    elif sample_type == "body":
        rgb_mask = create_body_mask((rgb_w, rgb_h), (cam_cx, cam_cy), scale=gt_scale * 1.1)
        target_color = (80, 110, 160)   # Clothing/silhouette
    else:
        rgb_mask = create_hand_mask((rgb_w, rgb_h), (cam_cx, cam_cy), scale=gt_scale * 1.2)
        target_color = (120, 150, 210)

    # Draw target on RGB
    for c in range(3):
        rgb_img[:, :, c] = np.where(rgb_mask > 0, target_color[c], rgb_img[:, :, c])

    # Add distinct internal edges and border contours to RGB image
    contours, _ = cv2.findContours(rgb_mask, cv2.RETR_EXTERNAL, cv2.CHAIN_APPROX_NONE)
    cv2.drawContours(rgb_img, contours, -1, (30, 30, 30), 2)

    # Add Gaussian camera noise to RGB
    noise = np.random.normal(0, 3.0, (rgb_h, rgb_w, 3)).astype(np.float32)
    rgb_img = np.clip(rgb_img.astype(np.float32) + noise, 0, 255).astype(np.uint8)

    # Save RGB
    rgb_path = os.path.join(sample_dir, "rgb.png")
    cv2.imwrite(rgb_path, rgb_img)

    # Now construct Thermal field (32x24 native sensor grid)
    # The thermal sensor target center in RGB coordinate space is:
    # cam_center = therm_center_in_rgb + [gt_dx, gt_dy]
    # => therm_center_in_rgb = cam_center - [gt_dx, gt_dy]
    therm_cx_in_rgb = cam_cx - gt_dx
    therm_cy_in_rgb = cam_cy - gt_dy

    # Convert to 32x24 grid coordinates
    th_cx = therm_cx_in_rgb / scale_factor
    th_cy = therm_cy_in_rgb / scale_factor

    # Render target at high resolution then downsample to 32x24 to mimic optical PSF
    high_tw, high_th = 320, 240
    high_scale = high_tw / therm_w  # 10x
    high_cx = th_cx * high_scale
    high_cy = th_cy * high_scale

    if sample_type == "hand":
        high_mask = create_hand_mask((high_tw, high_th), (high_cx, high_cy), scale=(gt_scale * 1.3) / 2.0)
        t_ambient = 21.5
        t_target = 34.0
    elif sample_type == "device":
        high_mask = create_device_mask((high_tw, high_th), (high_cx, high_cy), scale=(gt_scale * 1.2) / 2.0)
        t_ambient = 22.0
        t_target = 52.0
    elif sample_type == "body":
        high_mask = create_body_mask((high_tw, high_th), (high_cx, high_cy), scale=(gt_scale * 1.1) / 2.0)
        t_ambient = 20.0
        t_target = 35.5
    else:
        high_mask = create_hand_mask((high_tw, high_th), (high_cx, high_cy), scale=(gt_scale * 1.2) / 2.0)
        t_ambient = 21.0
        t_target = 33.5

    # Thermal field with natural radial thermal diffusion gradient
    dist_inside = cv2.distanceTransform(high_mask, cv2.DIST_L2, 5)
    max_d = np.max(dist_inside) if np.max(dist_inside) > 0 else 1.0
    high_temp = t_ambient + (t_target - t_ambient) * (high_mask / 255.0) * (0.8 + 0.2 * (dist_inside / max_d))

    # Downsample with area interpolation (equivalent to sensor pixel integration)
    therm_grid = cv2.resize(high_temp, (therm_w, therm_h), interpolation=cv2.INTER_AREA)

    # Add realistic MLX90640 NETD detector noise (~0.15°C standard deviation)
    thermal_noise = np.random.normal(0, 0.18, (therm_h, therm_w)).astype(np.float32)
    therm_grid = (therm_grid + thermal_noise).astype(np.float32)

    # HARDWARE PROPERTY: MLX90640 sensor raw array is horizontally mirrored!
    # When hardware driver reads it, it is flipped horizontally.
    therm_raw_sensor = cv2.flip(therm_grid, 1)

    # Save thermal.npy
    thermal_path = os.path.join(sample_dir, "thermal.npy")
    np.save(thermal_path, therm_raw_sensor)

    meta = {
        "sample_id": os.path.basename(sample_dir),
        "sample_type": sample_type,
        "distance_m": distance_m,
        "gt_dx": float(gt_dx),
        "gt_dy": float(gt_dy),
        "gt_scale": float(gt_scale),
        "ambient_c": float(t_ambient),
        "target_c": float(t_target),
        "add_clutter": bool(add_clutter)
    }
    with open(os.path.join(sample_dir, "meta.json"), "w", encoding="utf-8") as f:
        json.dump(meta, f, indent=2)

    return meta


def generate_benchmark_suite(base_dir):
    """
    Generates the standard 5-case multi-distance benchmark suite:
      1. 0.2m Hand (Large disparity dx=112, dy=-5)
      2. 0.3m Hand (Standard disparity dx=75, dy=-5)
      3. 0.5m Device (Medium disparity dx=46, dy=-5)
      4. 1.0m Body (Small disparity dx=24, dy=-5)
      5. 0.4m Cluttered (dx=57, dy=-5 in cluttered visible background)
    """
    os.makedirs(base_dir, exist_ok=True)
    benchmark_cases = [
        ("sample_01_0.2m_hand", "hand", 0.20, 112.0, -5.0, 1.15, False),
        ("sample_02_0.3m_hand", "hand", 0.30, 75.0, -5.0, 1.05, False),
        ("sample_03_0.5m_device", "device", 0.50, 46.0, -5.0, 1.00, False),
        ("sample_04_1.0m_body", "body", 1.00, 24.0, -5.0, 0.90, False),
        ("sample_05_cluttered_bg", "hand", 0.40, 57.0, -5.0, 1.00, True),
    ]

    all_meta = {}
    for folder_name, stype, dist, dx, dy, scale, clutter in benchmark_cases:
        sdir = os.path.join(base_dir, folder_name)
        m = generate_sample(sdir, stype, dist, dx, dy, scale, clutter)
        all_meta[folder_name] = m
        print(f"Generated benchmark case: {folder_name} (dist={dist}m, gt_dx={dx}, gt_dy={dy}, scale={scale})")

    gt_file = os.path.join(base_dir, "ground_truth.json")
    with open(gt_file, "w", encoding="utf-8") as f:
        json.dump(all_meta, f, indent=2)
    print(f"Benchmark suite generated successfully at: {base_dir}")
    print(f"Ground truth saved to: {gt_file}")


if __name__ == "__main__":
    import argparse
    parser = argparse.ArgumentParser(description="Generate TCCA multi-distance benchmark dataset")
    parser.add_argument("--output_dir", default=os.path.join(os.path.dirname(__file__), "data"),
                        help="Path to output data directory")
    args = parser.parse_args()
    generate_benchmark_suite(args.output_dir)
