"""
Evaluation Engine for TCCA Offline Verification Platform.
Executes batch evaluation on benchmark datasets, computes Pixel RMSE against ground truth,
and writes the standardized evaluation.csv summary.
"""

import os
import csv
import json
import numpy as np

from thermal_contour import ThermalContourExtractor
from rgb_edge import RGBEdgeProcessor
from chamfer_alignment import ChamferAligner
from visualize import TCCAVisualizer


def run_evaluation(data_dir, output_csv=None, output_vis_dir=None, save_visuals=True, lambda_phys=0.0):
    """
    Runs TCCA pipeline across all benchmark samples in data_dir.
    
    Args:
        data_dir: Path to directory containing sample folders
        output_csv: Path to output CSV file (default: evaluation.csv in research/alignment/)
        output_vis_dir: Path to save visualization artifacts
        save_visuals: bool, whether to generate visual charts
        lambda_phys: float, weight for physical disparity prior (0.0 for pure TCCA)
        
    Returns:
        tuple (records, summary_stats)
    """
    if output_csv is None:
        output_csv = os.path.join(os.path.dirname(__file__), "evaluation.csv")
    if output_vis_dir is None:
        output_vis_dir = os.path.join(os.path.dirname(__file__), "output_visuals")

    extractor = ThermalContourExtractor()
    rgb_proc = RGBEdgeProcessor()
    aligner = ChamferAligner()
    visualizer = TCCAVisualizer(output_vis_dir)

    # Find sample folders
    subdirs = sorted([
        d for d in os.listdir(data_dir)
        if os.path.isdir(os.path.join(data_dir, d)) and not d.startswith(".")
    ])

    records = []
    errors = []

    mode_label = "TCCA-Phys (Physics-Constrained)" if lambda_phys > 0 else "Pure TCCA (Baseline)"
    print(f"\n{'='*75}")
    print(f"Starting TCCA Batch Evaluation on: {data_dir}")
    print(f"Mode: {mode_label} (lambda_phys={lambda_phys})")
    print(f"Samples found: {len(subdirs)}")
    print(f"{'='*75}")

    for sname in subdirs:
        sdir = os.path.join(data_dir, sname)
        rgb_path = os.path.join(sdir, "rgb.png")
        therm_path = os.path.join(sdir, "thermal.npy")
        meta_path = os.path.join(sdir, "meta.json")

        if not (os.path.exists(rgb_path) and os.path.exists(therm_path)):
            continue

        gt_dx, gt_dy, gt_scale, dist_m = 0.0, 0.0, 1.0, 0.5
        if os.path.exists(meta_path):
            with open(meta_path, "r", encoding="utf-8") as f:
                meta = json.load(f)
                gt_dx = float(meta.get("gt_dx", 0.0))
                gt_dy = float(meta.get("gt_dy", 0.0))
                gt_scale = float(meta.get("gt_scale", 1.0))
                dist_m = float(meta.get("distance_m", 0.5))

        # 1. Thermal Contour Extraction
        tc_res = extractor.extract(therm_path)
        if not tc_res["valid"]:
            rec = {
                "image_id": sname,
                "distance": dist_m,
                "gt_dx": gt_dx,
                "gt_dy": gt_dy,
                "gt_scale": gt_scale,
                "pred_dx": 0.0,
                "pred_dy": 0.0,
                "pred_scale": 1.0,
                "error": 999.0,
                "chamfer_score": 999.0,
                "confidence": 0.0,
                "inlier_ratio": 0.0,
                "elapsed_ms": 0.0,
                "status": tc_res["status"]
            }
            records.append(rec)
            continue

        # 2. RGB Edge & Distance Transform
        rp_res = rgb_proc.process(rgb_path)

        # 3. TCCA Chamfer Matching
        physics_prior = None
        if lambda_phys > 0.0:
            # Physical prior: expected dx(Z) = 22.1 / Z + 1.5, expected dy = -5.0
            pred_dx_prior = (22.1 / max(dist_m, 0.18)) + 1.5
            physics_prior = {"expected_dx": pred_dx_prior, "expected_dy": -5.0}

        align_res = aligner.align(
            tc_res["contour_pts_rgb"],
            tc_res["centroid_rgb"],
            rp_res["dist_map"],
            physics_prior=physics_prior,
            lambda_phys=lambda_phys
        )

        pred_dx = align_res["best_dx"]
        pred_dy = align_res["best_dy"]
        pred_scale = align_res["best_scale"]

        # Pixel Euclidean Error
        err = float(np.hypot(pred_dx - gt_dx, pred_dy - gt_dy))
        errors.append(err)

        rec = {
            "image_id": sname,
            "distance": dist_m,
            "gt_dx": gt_dx,
            "gt_dy": gt_dy,
            "gt_scale": gt_scale,
            "pred_dx": pred_dx,
            "pred_dy": pred_dy,
            "pred_scale": pred_scale,
            "error": err,
            "chamfer_score": align_res["best_score"],
            "confidence": align_res["confidence"],
            "inlier_ratio": align_res["inlier_ratio"],
            "elapsed_ms": align_res["elapsed_ms"],
            "status": align_res["status"]
        }
        records.append(rec)

        # 4. Save diagnostic visual assets
        if save_visuals:
            s_vis_dir = os.path.join(output_vis_dir, sname)
            visualizer.generate_all(sname, rp_res, tc_res, align_res, s_vis_dir)

        print(f"[{sname:25s}] Dist: {dist_m:4.2f}m | GT: ({gt_dx:+6.1f}, {gt_dy:+5.1f}) | "
              f"Pred: ({pred_dx:+6.1f}, {pred_dy:+5.1f}) | Scale: {pred_scale:4.2f} | "
              f"Err: {err:5.2f}px | E_chamfer: {align_res['best_score']:5.2f}px | {align_res['elapsed_ms']:5.1f}ms")

    # Write evaluation.csv
    fieldnames = [
        "image_id", "distance", "gt_dx", "gt_dy", "gt_scale",
        "pred_dx", "pred_dy", "pred_scale", "error",
        "chamfer_score", "confidence", "inlier_ratio", "elapsed_ms", "status"
    ]
    with open(output_csv, "w", newline="", encoding="utf-8") as f:
        writer = csv.DictWriter(f, fieldnames=fieldnames)
        writer.writeheader()
        for r in records:
            writer.writerow(r)

    # Compute Summary Statistics
    valid_errors = [r["error"] for r in records if r["status"] == "OK"]
    if valid_errors:
        rmse = float(np.sqrt(np.mean(np.square(valid_errors))))
        mean_err = float(np.mean(valid_errors))
        max_err = float(np.max(valid_errors))
        # 1 thermal native pixel = 20 RGB pixels. Accuracy < 6px is < 0.3 native thermal pixel!
        sub_thermal_pixel_acc = float(np.mean([e <= 6.0 for e in valid_errors]) * 100.0)
    else:
        rmse = mean_err = max_err = sub_thermal_pixel_acc = 0.0

    avg_time = float(np.mean([r["elapsed_ms"] for r in records if r["status"] == "OK"])) if records else 0.0

    summary = {
        "total_samples": len(records),
        "successful_samples": len(valid_errors),
        "pixel_rmse": rmse,
        "mean_error_px": mean_err,
        "max_error_px": max_err,
        "sub_thermal_pixel_acc_percent": sub_thermal_pixel_acc,
        "avg_time_ms": avg_time
    }

    print(f"\n{'='*75}")
    print("EVALUATION SUMMARY REPORT:")
    print(f"  Total test cases:             {summary['total_samples']}")
    print(f"  Pixel RMSE:                   {summary['pixel_rmse']:.2f} px (at 640x480)")
    print(f"  Mean Error:                   {summary['mean_error_px']:.2f} px")
    print(f"  Max Error:                    {summary['max_error_px']:.2f} px")
    print(f"  Accuracy (< 0.3 thermal px):  {summary['sub_thermal_pixel_acc_percent']:.1f}%")
    print(f"  Average Execution Time:       {summary['avg_time_ms']:.1f} ms")
    print(f"  Summary saved to:             {output_csv}")
    print(f"  Visualizations saved to:      {output_vis_dir}")
    print(f"{'='*75}\n")

    return records, summary


if __name__ == "__main__":
    import argparse
    parser = argparse.ArgumentParser(description="Run TCCA Evaluation Suite")
    parser.add_argument("--data_dir", default=os.path.join(os.path.dirname(__file__), "data"))
    parser.add_argument("--output_csv", default=os.path.join(os.path.dirname(__file__), "evaluation.csv"))
    parser.add_argument("--output_vis_dir", default=os.path.join(os.path.dirname(__file__), "output_visuals"))
    parser.add_argument("--lambda_phys", type=float, default=0.0, help="Physics constraint weight (default: 0.0)")
    args = parser.parse_args()

    run_evaluation(args.data_dir, args.output_csv, args.output_vis_dir, lambda_phys=args.lambda_phys)

