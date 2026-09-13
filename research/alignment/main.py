"""
Main CLI Entrypoint for Thermal Contour Chamfer Alignment (TCCA) Verification Platform.
Supports single sample diagnostic mode, full benchmark evaluation, and physics prior comparison.
"""

import os
import sys
import argparse
import json

from thermal_contour import ThermalContourExtractor
from rgb_edge import RGBEdgeProcessor
from chamfer_alignment import ChamferAligner
from visualize import TCCAVisualizer
from evaluate import run_evaluation
from generate_dataset import generate_benchmark_suite


def run_single_sample(sample_path, out_dir=None, lambda_phys=0.0):
    """Executes TCCA on a single sample folder containing rgb.png and thermal.npy."""
    rgb_file = os.path.join(sample_path, "rgb.png")
    therm_file = os.path.join(sample_path, "thermal.npy")
    meta_file = os.path.join(sample_path, "meta.json")

    if not os.path.exists(rgb_file) or not os.path.exists(therm_file):
        print(f"Error: Missing rgb.png or thermal.npy in {sample_path}")
        sys.exit(1)

    gt_meta = None
    if os.path.exists(meta_file):
        with open(meta_file, "r", encoding="utf-8") as f:
            gt_meta = json.load(f)

    print(f"\nProcessing sample: {sample_path}")
    if gt_meta:
        print(f"  Ground Truth: dx={gt_meta.get('gt_dx')}, dy={gt_meta.get('gt_dy')}, "
              f"scale={gt_meta.get('gt_scale')}, dist={gt_meta.get('distance_m')}m")

    # Step 1: Thermal Contour
    ext = ThermalContourExtractor()
    tc = ext.extract(therm_file)
    print(f"  [1] Thermal extraction: status={tc['status']}, valid={tc['valid']}, "
          f"contrast={tc['contrast']:.2f}C, pts={len(tc['contour_pts_rgb'])}")

    if not tc["valid"]:
        print("  Target extraction failed. Aborting.")
        return

    # Step 2: RGB Edges & Distance
    proc = RGBEdgeProcessor()
    rp = proc.process(rgb_file)
    print(f"  [2] RGB edge processing: non-zero edges={np.count_nonzero(rp['edges'])}")

    # Step 3: Chamfer Alignment
    aligner = ChamferAligner()
    physics_prior = None
    if gt_meta and lambda_phys > 0.0:
        physics_prior = {"expected_dx": gt_meta.get("gt_dx", 0.0)}

    align_res = aligner.align(
        tc["contour_pts_rgb"],
        tc["centroid_rgb"],
        rp["dist_map"],
        physics_prior=physics_prior,
        lambda_phys=lambda_phys
    )

    print(f"  [3] TCCA Chamfer Matching:")
    print(f"      - Best dx:        {align_res['best_dx']:+.2f} px")
    print(f"      - Best dy:        {align_res['best_dy']:+.2f} px")
    print(f"      - Best scale:     {align_res['best_scale']:.3f}")
    print(f"      - Chamfer score:  {align_res['best_score']:.2f} px")
    print(f"      - Confidence:     {align_res['confidence']:.3f}")
    print(f"      - Inlier ratio:   {align_res['inlier_ratio']*100:.1f}%")
    print(f"      - Elapsed time:   {align_res['elapsed_ms']:.1f} ms")

    if gt_meta:
        err = np.hypot(align_res['best_dx'] - gt_meta['gt_dx'], align_res['best_dy'] - gt_meta['gt_dy'])
        print(f"      - Alignment Error vs GT: {err:.2f} px")

    # Step 4: Visuals
    sample_name = os.path.basename(os.path.normpath(sample_path))
    target_out = out_dir or os.path.join(os.path.dirname(__file__), "output_visuals", sample_name)
    vis = TCCAVisualizer(target_out)
    paths = vis.generate_all(sample_name, rp, tc, align_res, target_out)
    print(f"  [4] Diagnostic visualizations exported to: {target_out}")
    print(f"      Dashboard: {paths['dashboard_path']}\n")


def main():
    parser = argparse.ArgumentParser(
        description="Thermal Contour Chamfer Alignment (TCCA) Verification Platform"
    )
    parser.add_argument("--eval-all", action="store_true",
                        help="Run batch evaluation on all benchmark samples in research/alignment/data")
    parser.add_argument("--sample", type=str, default=None,
                        help="Path or name of sample directory to evaluate")
    parser.add_argument("--gen-data", action="store_true",
                        help="Regenerate benchmark dataset suite")
    parser.add_argument("--data-dir", type=str,
                        default=os.path.join(os.path.dirname(__file__), "data"),
                        help="Base directory for benchmark data")
    parser.add_argument("--out-csv", type=str,
                        default=os.path.join(os.path.dirname(__file__), "evaluation.csv"),
                        help="Path to save evaluation.csv")
    parser.add_argument("--out-vis", type=str,
                        default=os.path.join(os.path.dirname(__file__), "output_visuals"),
                        help="Directory to save visual diagnostics")
    parser.add_argument("--lambda-phys", type=float, default=0.0,
                        help="Weight for physical disparity prior constraint (default: 0.0)")

    args = parser.parse_args()
    import numpy as np

    if args.gen_data:
        generate_benchmark_suite(args.data_dir)
        if not args.eval_all and not args.sample:
            return

    if args.sample:
        sample_path = args.sample
        if not os.path.exists(sample_path):
            # Try looking in data_dir
            cand = os.path.join(args.data_dir, sample_path)
            if os.path.exists(cand):
                sample_path = cand
            else:
                print(f"Error: Sample path not found: {sample_path}")
                sys.exit(1)
        run_single_sample(sample_path, out_dir=args.out_vis, lambda_phys=args.lambda_phys)
    else:
        # Default action: run full benchmark evaluation
        run_evaluation(args.data_dir, args.out_csv, args.out_vis, save_visuals=True)


if __name__ == "__main__":
    import numpy as np
    main()
