"""Evaluate the empirical viability primary matrix.

The evaluator is deliberately metric-first. It selects the strongest frozen
deterministic spatial comparator from the measured matrix, then records the
causal residual comparison against that comparator and temporal diagnostics.
It does not choose the campaign verdict automatically; interpretation is
written into the active authority after the complete evidence has been
reviewed.
"""

from __future__ import annotations

import argparse
import csv
import json
import math
import sys
from pathlib import Path
from typing import Any

import cv2
import numpy as np

ROOT = Path(__file__).resolve().parents[2]
if str(ROOT) not in sys.path:
    sys.path.insert(0, str(ROOT))

from benchmarks.empirical.evs_manifest import FRAME_COUNT, HR_HEIGHT, HR_WIDTH, METHODS, SEQUENCES


DETERMINISTIC_SPATIAL_KINDS = frozenset({"external_ffmpeg_spatial", "player_quality_lab_spatial"})
BASELINE_METRIC_DIRECTIONS = {
    "mean_psnr_db": "higher",
    "mean_ssim": "higher",
    "mean_hf_correlation": "higher",
    "mean_registered_temporal_error": "lower",
}
BASELINE_EPSILON = 1.0e-12


def read_json(path: Path) -> dict[str, Any]:
    return json.loads(path.read_text(encoding="utf-8"))


def load_rgb(path: Path) -> np.ndarray:
    image = cv2.imread(str(path), cv2.IMREAD_COLOR)
    if image is None:
        raise RuntimeError(f"cannot decode image: {path}")
    if image.shape[1] != HR_WIDTH or image.shape[0] != HR_HEIGHT:
        raise RuntimeError(f"unexpected image dimensions for {path}: {image.shape[1]}x{image.shape[0]}")
    return cv2.cvtColor(image, cv2.COLOR_BGR2RGB).astype(np.float32) / 255.0


def mean_corr(left: np.ndarray, right: np.ndarray) -> float:
    a = left.reshape(-1).astype(np.float64)
    b = right.reshape(-1).astype(np.float64)
    a -= a.mean()
    b -= b.mean()
    denominator = math.sqrt(float(np.dot(a, a) * np.dot(b, b)))
    return float(np.dot(a, b) / denominator) if denominator > 1.0e-12 else 0.0


def gray(image: np.ndarray) -> np.ndarray:
    return (0.2126 * image[..., 0] + 0.7152 * image[..., 1] + 0.0722 * image[..., 2]).astype(np.float32)


def highpass(image: np.ndarray) -> np.ndarray:
    return image - cv2.GaussianBlur(image, (0, 0), sigmaX=1.25, sigmaY=1.25, borderType=cv2.BORDER_REFLECT_101)


def ssim(left: np.ndarray, right: np.ndarray) -> float:
    a = gray(left)
    b = gray(right)
    mu_a = cv2.GaussianBlur(a, (11, 11), 1.5)
    mu_b = cv2.GaussianBlur(b, (11, 11), 1.5)
    sigma_a = cv2.GaussianBlur(a * a, (11, 11), 1.5) - mu_a * mu_a
    sigma_b = cv2.GaussianBlur(b * b, (11, 11), 1.5) - mu_b * mu_b
    sigma_ab = cv2.GaussianBlur(a * b, (11, 11), 1.5) - mu_a * mu_b
    c1 = 0.01 ** 2
    c2 = 0.03 ** 2
    numerator = (2.0 * mu_a * mu_b + c1) * (2.0 * sigma_ab + c2)
    denominator = (mu_a * mu_a + mu_b * mu_b + c1) * (sigma_a + sigma_b + c2)
    return float(np.mean(np.divide(numerator, denominator, out=np.ones_like(numerator), where=denominator != 0)))


def psnr(left: np.ndarray, right: np.ndarray) -> float:
    mse = float(np.mean((left.astype(np.float64) - right.astype(np.float64)) ** 2))
    return 99.0 if mse <= 1.0e-14 else float(10.0 * np.log10(1.0 / mse))


def edge_mask(image: np.ndarray) -> np.ndarray:
    values = np.rint(np.clip(gray(image) * 255.0, 0.0, 255.0)).astype(np.uint8)
    return cv2.Canny(values, 50, 150) > 0


def edge_position_error(candidate: np.ndarray, reference: np.ndarray) -> float:
    candidate_edges = edge_mask(candidate)
    reference_edges = edge_mask(reference)
    if not candidate_edges.any() and not reference_edges.any():
        return 0.0
    if not candidate_edges.any() or not reference_edges.any():
        return 1.0
    reference_distance = cv2.distanceTransform((~reference_edges).astype(np.uint8), cv2.DIST_L2, 3)
    candidate_distance = cv2.distanceTransform((~candidate_edges).astype(np.uint8), cv2.DIST_L2, 3)
    forward = float(np.mean(reference_distance[candidate_edges]))
    backward = float(np.mean(candidate_distance[reference_edges]))
    return (forward + backward) / 2.0


def thin_overlap(candidate: np.ndarray, reference: np.ndarray) -> dict[str, float]:
    candidate_edges = edge_mask(candidate)
    reference_edges = edge_mask(reference)
    intersection = float(np.count_nonzero(candidate_edges & reference_edges))
    candidate_count = float(np.count_nonzero(candidate_edges))
    reference_count = float(np.count_nonzero(reference_edges))
    precision = intersection / candidate_count if candidate_count else 0.0
    recall = intersection / reference_count if reference_count else 0.0
    f1 = 2.0 * precision * recall / (precision + recall) if precision + recall else 0.0
    return {"precision": precision, "recall": recall, "f1": f1}


def fourier_band_error(candidate: np.ndarray, reference: np.ndarray) -> float:
    a = np.fft.fftshift(np.fft.fft2(gray(candidate)))
    b = np.fft.fftshift(np.fft.fft2(gray(reference)))
    yy, xx = np.indices(a.shape, dtype=np.float32)
    radius = np.sqrt((xx - a.shape[1] / 2.0) ** 2 + (yy - a.shape[0] / 2.0) ** 2)
    band = radius > 0.35 * min(a.shape)
    magnitude_a = np.abs(a)
    magnitude_b = np.abs(b)
    denominator = float(np.mean(magnitude_b[band])) + 1.0e-8
    return float(np.mean(np.abs(magnitude_a[band] - magnitude_b[band])) / denominator)


def unsupported_detail_energy(candidate: np.ndarray, reference: np.ndarray) -> float:
    candidate_hp = highpass(candidate)
    reference_hp = highpass(reference)
    support = np.mean(np.abs(reference_hp), axis=2)
    threshold = float(np.quantile(support, 0.45))
    unsupported = support <= threshold
    energy = np.mean(np.square(candidate_hp), axis=2)
    return float(np.mean(energy[unsupported])) if unsupported.any() else 0.0


def residual_metrics(candidate: np.ndarray, baseline: np.ndarray, reference: np.ndarray) -> dict[str, float]:
    gt_residual = highpass(reference - baseline)
    candidate_residual = highpass(candidate - baseline)
    gt_magnitude = np.abs(gt_residual)
    candidate_magnitude = np.abs(candidate_residual)
    denominator = float(np.mean(gt_magnitude)) + 1.0e-8
    weighted = np.abs(gt_residual).reshape(-1)
    gt_flat = gt_residual.reshape(-1)
    candidate_flat = candidate_residual.reshape(-1)
    sign_match = np.sign(gt_flat) == np.sign(candidate_flat)
    phase_agreement = float(np.sum(weighted * sign_match) / (np.sum(weighted) + 1.0e-8))
    projection = float(np.mean(candidate_residual * gt_residual) / (np.mean(gt_residual * gt_residual) + 1.0e-8))
    return {
        "gt_residual_mean_abs": float(np.mean(gt_magnitude)),
        "candidate_residual_mean_abs": float(np.mean(candidate_magnitude)),
        "candidate_residual_magnitude_ratio": float(np.mean(candidate_magnitude) / denominator),
        "candidate_residual_correlation": mean_corr(candidate_residual, gt_residual),
        "candidate_residual_phase_agreement": phase_agreement,
        "candidate_residual_signed_projection": projection,
    }


def select_strongest_spatial_baseline(
    metric_rows: list[dict[str, Any]],
    method_specs: dict[str, dict[str, Any]] | None = None,
) -> tuple[str, dict[str, Any]]:
    """Select the unique Pareto-dominant frozen deterministic spatial method.

    The baseline is selected only after every method has been evaluated against
    ground truth. A method must be no worse on all frozen aggregate fidelity
    criteria and strictly better on at least one. This makes the choice
    reproducible without assigning arbitrary weights to unlike metrics.
    """

    specs = method_specs or METHODS
    candidates = sorted(
        method_id
        for method_id, method in specs.items()
        if method.get("kind") in DETERMINISTIC_SPATIAL_KINDS
    )
    if len(candidates) < 2:
        raise RuntimeError(
            "baseline selection requires at least two frozen deterministic spatial methods; "
            f"found {candidates}"
        )

    aggregates: dict[str, dict[str, float]] = {}
    for method_id in candidates:
        rows = [row for row in metric_rows if row["method"] == method_id]
        if not rows:
            raise RuntimeError(f"baseline candidate {method_id} has no evaluated rows")
        aggregates[method_id] = {
            metric: float(np.mean([float(row[metric]) for row in rows]))
            for metric in BASELINE_METRIC_DIRECTIONS
        }

    def at_least(left: float, right: float, direction: str) -> bool:
        if direction == "higher":
            return left >= right - BASELINE_EPSILON
        return left <= right + BASELINE_EPSILON

    def strictly_better(left: float, right: float, direction: str) -> bool:
        if direction == "higher":
            return left > right + BASELINE_EPSILON
        return left < right - BASELINE_EPSILON

    def dominates(left: str, right: str) -> bool:
        left_values = aggregates[left]
        right_values = aggregates[right]
        return all(
            at_least(left_values[metric], right_values[metric], direction)
            for metric, direction in BASELINE_METRIC_DIRECTIONS.items()
        ) and any(
            strictly_better(left_values[metric], right_values[metric], direction)
            for metric, direction in BASELINE_METRIC_DIRECTIONS.items()
        )

    dominators = {
        method_id: [other for other in candidates if other != method_id and dominates(other, method_id)]
        for method_id in candidates
    }
    winners = [method_id for method_id in candidates if not dominators[method_id]]
    if len(winners) != 1:
        raise RuntimeError(
            "frozen deterministic spatial baseline is not uniquely selected: "
            f"candidates={candidates}, dominators={dominators}, aggregates={aggregates}"
        )

    selected = winners[0]
    return selected, {
        "rule": "unique_pareto_dominant_deterministic_spatial_method",
        "metric_directions": BASELINE_METRIC_DIRECTIONS,
        "candidate_methods": candidates,
        "aggregates": aggregates,
        "dominators": dominators,
        "selected_method": selected,
    }


def warp_forward(previous: np.ndarray, dx: float, dy: float) -> np.ndarray:
    matrix = np.array([[1.0, 0.0, dx], [0.0, 1.0, dy]], dtype=np.float32)
    return cv2.warpAffine(
        previous,
        matrix,
        (HR_WIDTH, HR_HEIGHT),
        flags=cv2.INTER_LINEAR,
        borderMode=cv2.BORDER_REFLECT_101,
    )


def periodicity(image: np.ndarray) -> float:
    hp = gray(highpass(image)).astype(np.float64)
    hp -= hp.mean()
    denominator = float(np.dot(hp.reshape(-1), hp.reshape(-1))) + 1.0e-12
    scores = []
    for dy, dx in ((0, 2), (2, 0), (2, 2), (0, 3), (3, 0)):
        shifted = np.roll(hp, (dy, dx), axis=(0, 1))
        scores.append(float(np.dot(hp.reshape(-1), shifted.reshape(-1)) / denominator))
    return max(scores)


def temporal_metrics(outputs: list[np.ndarray], references: list[np.ndarray], sequence: str) -> dict[str, float]:
    vx, vy = SEQUENCES[sequence]["velocity_hr_pixels_per_frame"]
    registered_errors: list[float] = []
    flicker: list[float] = []
    for previous_output, output, previous_reference, reference in zip(
        outputs[:-1], outputs[1:], references[:-1], references[1:]
    ):
        predicted_output = warp_forward(previous_output, vx, vy)
        predicted_reference = warp_forward(previous_reference, vx, vy)
        output_delta = output - predicted_output
        reference_delta = reference - predicted_reference
        registered_errors.append(float(np.mean(np.abs(output_delta - reference_delta))))
        flicker.append(float(np.mean(np.abs((output - predicted_output) - (reference - predicted_reference)))))
    if not registered_errors:
        registered_errors = [0.0]
        flicker = [0.0]
    edge = np.stack([cv2.Sobel(gray(frame), cv2.CV_32F, 1, 0, ksize=3) for frame in outputs])
    return {
        "registered_temporal_error": float(np.mean(registered_errors)),
        "static_flicker": float(np.mean(flicker)),
        "edge_variance": float(np.var(edge)),
        "periodicity": float(np.mean([periodicity(frame) for frame in outputs])),
    }


def output_paths(cell_record: dict[str, Any], cell_dir: Path) -> list[Path]:
    capture = cell_record["capture"]
    if cell_record["cell"]["method"] == "M1":
        paths = [Path(path) for path in capture["frames"]]
    else:
        paths = sorted((cell_dir / "player" / "dumps").glob("temporal_forge_fsr4_*.ppm"))
    if len(paths) != FRAME_COUNT:
        raise RuntimeError(f"{cell_record['cell']['cell_id']} has {len(paths)} output frames, expected {FRAME_COUNT}")
    return paths


def evaluate_cell(cell_record: dict[str, Any], cell_dir: Path, baseline_outputs: list[np.ndarray] | None) -> tuple[dict[str, Any], list[np.ndarray]]:
    references = [load_rgb(Path(path)) for path in cell_record["fixture"]["ground_truth_frames"]]
    outputs = [load_rgb(path) for path in output_paths(cell_record, cell_dir)]
    frame_rows: list[dict[str, float]] = []
    for output, reference in zip(outputs, references):
        candidate_hp = highpass(output)
        reference_hp = highpass(reference)
        frame_rows.append(
            {
                "psnr_db": psnr(output, reference),
                "ssim": ssim(output, reference),
                "high_frequency_residual_correlation": mean_corr(candidate_hp, reference_hp),
                "signed_phase_agreement": float(np.mean(np.sign(candidate_hp) == np.sign(reference_hp))),
                "edge_position_error_px": edge_position_error(output, reference),
                "thin_precision": thin_overlap(output, reference)["precision"],
                "thin_overlap_f1": thin_overlap(output, reference)["f1"],
                "fourier_band_error": fourier_band_error(output, reference),
                "unsupported_detail_energy": unsupported_detail_energy(output, reference),
            }
        )
    aggregate = {key: float(np.mean([row[key] for row in frame_rows])) for key in frame_rows[0]}
    aggregate.update(temporal_metrics(outputs, references, cell_record["cell"]["sequence"]))
    if baseline_outputs is not None:
        residual_rows = [
            residual_metrics(output, baseline, reference)
            for output, baseline, reference in zip(outputs, baseline_outputs, references)
        ]
        for key in residual_rows[0]:
            aggregate[key] = float(np.mean([row[key] for row in residual_rows]))
    result = {
        "schema": "temporal_forge.empirical_viability.metrics.v1",
        "cell": cell_record["cell"],
        "run_id": cell_record["run_id"],
        "status": cell_record["status"],
        "identity_verified": bool(cell_record["identity"].get("verified")),
        "frame_metrics": frame_rows,
        "aggregate": aggregate,
        "output_frame_sha256": {
            path.name: __import__("hashlib").sha256(path.read_bytes()).hexdigest()
            for path in output_paths(cell_record, cell_dir)
        },
    }
    return result, outputs


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--manifest", type=Path, required=True)
    parser.add_argument("--capture-root", type=Path, required=True)
    parser.add_argument("--output-root", type=Path, required=True)
    args = parser.parse_args()
    manifest = read_json(args.manifest)
    cells = manifest.get("matrix", [])
    if len(cells) != 64:
        raise SystemExit("evaluation requires the complete 64-cell primary matrix")
    args.output_root.mkdir(parents=True, exist_ok=True)
    evaluated_cells: list[tuple[dict[str, Any], dict[str, Any], Path, dict[str, Any], list[np.ndarray]]] = []
    outputs_by_key: dict[tuple[str, str, str, str], list[np.ndarray]] = {}
    for index, cell in enumerate(cells, start=1):
        cell_dir = args.capture_root / "cells" / cell["cell_id"]
        cell_path = cell_dir / "cell.json"
        if not cell_path.is_file():
            raise SystemExit(f"missing cell provenance: {cell_path}")
        record = read_json(cell_path)
        if record.get("status") != "complete" or not record.get("identity", {}).get("verified"):
            raise SystemExit(f"cell is incomplete or identity is not verified: {cell['cell_id']}")
        metrics, outputs = evaluate_cell(record, cell_dir, None)
        key = (cell["structural_class"], cell["regime"], cell["sequence"], cell["method"])
        outputs_by_key[key] = outputs
        evaluated_cells.append((cell, record, cell_dir, metrics, outputs))
        print(f"[{index:02d}/64] evaluated {cell['cell_id']}", flush=True)

    preliminary_rows = [
        {
            "cell_id": cell["cell_id"],
            **cell,
            **metrics["aggregate"],
            "mean_psnr_db": metrics["aggregate"]["psnr_db"],
            "mean_ssim": metrics["aggregate"]["ssim"],
            "mean_hf_correlation": metrics["aggregate"]["high_frequency_residual_correlation"],
            "mean_registered_temporal_error": metrics["aggregate"]["registered_temporal_error"],
        }
        for cell, _record, _cell_dir, metrics, _outputs in evaluated_cells
    ]
    baseline_method, baseline_selection = select_strongest_spatial_baseline(
        preliminary_rows, method_specs=manifest.get("methods", METHODS)
    )

    metric_rows: list[dict[str, Any]] = []
    for cell, record, cell_dir, metrics, _outputs in evaluated_cells:
        if cell["method"] != baseline_method:
            baseline_key = (cell["structural_class"], cell["regime"], cell["sequence"], baseline_method)
            baseline = outputs_by_key.get(baseline_key)
            if baseline is None:
                raise RuntimeError(f"missing selected baseline output for {cell['cell_id']}: {baseline_key}")
            references = [load_rgb(Path(path)) for path in record["fixture"]["ground_truth_frames"]]
            outputs = [load_rgb(path) for path in output_paths(record, cell_dir)]
            residual_rows = [
                residual_metrics(output, base, reference)
                for output, base, reference in zip(outputs, baseline, references)
            ]
            for key in residual_rows[0]:
                metrics["aggregate"][key] = float(np.mean([row[key] for row in residual_rows]))
        metric_rows.append(
            {
                "cell_id": cell["cell_id"],
                **cell,
                **metrics["aggregate"],
            }
        )
        metrics["baseline_method"] = baseline_method
        (args.output_root / f"{cell['cell_id']}.json").write_text(
            json.dumps(metrics, indent=2, sort_keys=True) + "\n", encoding="utf-8"
        )

    csv_path = args.output_root / "primary_metrics.csv"
    identity_fields = ["cell_id", "method", "regime", "sequence", "structural_class"]
    fieldnames = identity_fields + sorted(
        {key for row in metric_rows for key in row if key not in identity_fields}
    )
    with csv_path.open("w", newline="", encoding="utf-8") as handle:
        writer = csv.DictWriter(handle, fieldnames=fieldnames)
        writer.writeheader()
        writer.writerows(metric_rows)

    summary: dict[str, Any] = {
        "schema": "temporal_forge.empirical_viability.primary_evaluation.v2",
        "campaign_id": manifest["campaign_id"],
        "cells_evaluated": len(metric_rows),
        "baseline_method": baseline_method,
        "baseline_selection": baseline_selection,
        "methods": {},
        "r1_vs_r2": {},
        "r1_vs_baseline": {},
        "r1_residual_by_class": {},
        "residual_comparison": {},
        "confirmation_recommendation": "undecided",
        "interpretation_status": "corrected-metrics-recorded-no-verdict",
    }
    method_ids = sorted(manifest.get("methods", METHODS))
    for method in method_ids:
        method_rows = [row for row in metric_rows if row["method"] == method]
        summary["methods"][method] = {
            "cells": len(method_rows),
            "mean_ssim": float(np.mean([row["ssim"] for row in method_rows])),
            "mean_psnr_db": float(np.mean([row["psnr_db"] for row in method_rows])),
            "mean_hf_correlation": float(np.mean([row["high_frequency_residual_correlation"] for row in method_rows])),
            "mean_registered_temporal_error": float(np.mean([row["registered_temporal_error"] for row in method_rows])),
        }
    for regime in ("R1", "R2"):
        for method in method_ids:
            rows = [row for row in metric_rows if row["regime"] == regime and row["method"] == method]
            entry: dict[str, Any] = {
                "cells": len(rows),
                "mean_hf_correlation": float(np.mean([row["high_frequency_residual_correlation"] for row in rows])),
            }
            residual_fields = {
                "mean_residual_correlation": "candidate_residual_correlation",
                "mean_residual_phase_agreement": "candidate_residual_phase_agreement",
                "mean_residual_projection": "candidate_residual_signed_projection",
                "mean_residual_magnitude_ratio": "candidate_residual_magnitude_ratio",
            }
            if rows and all(field in rows[0] for field in residual_fields.values()):
                entry.update(
                    {
                        output_key: float(np.mean([row[input_key] for row in rows]))
                        for output_key, input_key in residual_fields.items()
                    }
                )
            summary["r1_vs_r2"][f"{regime}_{method}"] = entry

            if regime == "R1" and method != baseline_method:
                summary["residual_comparison"][method] = entry

    for structural_class in sorted({row["structural_class"] for row in metric_rows}):
        baseline_rows = [
            row
            for row in metric_rows
            if row["structural_class"] == structural_class
            and row["regime"] == "R1"
            and row["method"] == baseline_method
        ]
        for method in method_ids:
            if method == baseline_method:
                continue
            candidate_rows = [
                row
                for row in metric_rows
                if row["structural_class"] == structural_class
                and row["regime"] == "R1"
                and row["method"] == method
            ]
            baseline_hf = float(np.mean([row["high_frequency_residual_correlation"] for row in baseline_rows]))
            candidate_hf = float(np.mean([row["high_frequency_residual_correlation"] for row in candidate_rows]))
            summary["r1_vs_baseline"][f"{structural_class}_{method}"] = {
                "baseline_method": baseline_method,
                "baseline_hf_correlation": baseline_hf,
                "candidate_hf_correlation": candidate_hf,
                "absolute_delta": candidate_hf - baseline_hf,
                "relative_delta_percent": (candidate_hf / baseline_hf - 1.0) * 100.0,
                "meets_plus_10_percent_target": candidate_hf >= baseline_hf * 1.10,
                "baseline_psnr_db": float(np.mean([row["psnr_db"] for row in baseline_rows])),
                "candidate_psnr_db": float(np.mean([row["psnr_db"] for row in candidate_rows])),
                "baseline_ssim": float(np.mean([row["ssim"] for row in baseline_rows])),
                "candidate_ssim": float(np.mean([row["ssim"] for row in candidate_rows])),
                "baseline_registered_temporal_error": float(
                    np.mean([row["registered_temporal_error"] for row in baseline_rows])
                ),
                "candidate_registered_temporal_error": float(
                    np.mean([row["registered_temporal_error"] for row in candidate_rows])
                ),
            }
            residual_fields = {
                "mean_gt_residual_mean_abs": "gt_residual_mean_abs",
                "mean_candidate_residual_mean_abs": "candidate_residual_mean_abs",
                "mean_residual_magnitude_ratio": "candidate_residual_magnitude_ratio",
                "mean_residual_correlation": "candidate_residual_correlation",
                "mean_residual_phase_agreement": "candidate_residual_phase_agreement",
                "mean_residual_projection": "candidate_residual_signed_projection",
            }
            summary["r1_residual_by_class"][f"{structural_class}_{method}"] = {
                "baseline_method": baseline_method,
                "cells": len(candidate_rows),
                **{
                    output_key: float(np.mean([row[input_key] for row in candidate_rows]))
                    for output_key, input_key in residual_fields.items()
                },
            }
    (args.output_root / "primary_evaluation.json").write_text(
        json.dumps(summary, indent=2, sort_keys=True) + "\n", encoding="utf-8"
    )
    print(json.dumps(summary, indent=2, sort_keys=True))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
