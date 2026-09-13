"""Gate-0 determinism probe.

Schema ``temporal_forge.gate0.determinism.v1``. Repeatedly executes the same
controlled input under an identical configuration and measures how repeatable
the captured output payloads are:

- ``byte_identical``   — every compared dump has the same SHA-256 in every run;
- ``metric_stable``    — payloads differ but the measured mean-absolute-error
                         envelope stays within the recorded threshold;
- ``nondeterministic`` — differences exceed the envelope (Gate-0 defect).

The report records the comparison rule later A/B testing must use.
"""

from __future__ import annotations

import json
import sys
from datetime import datetime, timezone
from pathlib import Path
from typing import Any

from benchmarks.gate0.player_run import run_player

SCHEMA = "temporal_forge.gate0.determinism.v1"

# MAE on 8-bit RGB samples (0-255). 0 means byte-equal content; the threshold
# exists so a repeat with isolated single-sample GPU noise is measurable
# rather than instantly fatal. Any A/B must pair arms within this envelope.
MAE_THRESHOLD = 0.05


def read_ppm(path: Path) -> tuple[int, int, bytes]:
    """Parse a binary P6 PPM; returns (width, height, rgb_bytes)."""
    with open(path, "rb") as handle:
        magic = handle.readline().strip()
        if magic != b"P6":
            raise ValueError(f"not a binary PPM: {path}")
        line = handle.readline()
        while line.startswith(b"#"):
            line = handle.readline()
        width, height = (int(v) for v in line.split())
        maxval = int(handle.readline().strip())
        if maxval != 255:
            raise ValueError(f"unsupported PPM maxval {maxval}: {path}")
        pixel_bytes = handle.read(width * height * 3)
    if len(pixel_bytes) != width * height * 3:
        raise ValueError(f"short PPM payload: {path}")
    return width, height, pixel_bytes


def ppm_mae(path_a: Path, path_b: Path) -> float:
    """Mean absolute error between two same-size PPMs, 0-255 scale."""
    width_a, height_a, data_a = read_ppm(path_a)
    width_b, height_b, data_b = read_ppm(path_b)
    if (width_a, height_a) != (width_b, height_b):
        raise ValueError(
            f"dimension mismatch: {width_a}x{height_a} vs {width_b}x{height_b}"
        )
    total = 0
    for byte_a, byte_b in zip(data_a, data_b):
        total += abs(byte_a - byte_b)
    return total / len(data_a)


def _compare_run_pair(
    run_a: dict[str, Any], run_b: dict[str, Any], base_dir: Path
) -> dict[str, Any]:
    common = sorted(set(run_a["dumps"]) & set(run_b["dumps"]))
    if not common:
        return {"compared": 0, "byte_identical_frames": 0, "max_mae": None}
    byte_identical = sum(
        1 for name in common if run_a["dumps"][name] == run_b["dumps"][name]
    )
    max_mae: float | None = 0.0
    for name in common:
        if run_a["dumps"][name] == run_b["dumps"][name]:
            continue
        payload_a = base_dir / run_a["dump_dir"] / name
        payload_b = base_dir / run_b["dump_dir"] / name
        if not (payload_a.is_file() and payload_b.is_file()):
            # Retained payloads are missing; the mismatch cannot be measured.
            # The verdict logic treats this as nondeterministic (fail closed).
            max_mae = None
            break
        max_mae = max(max_mae or 0.0, ppm_mae(payload_a, payload_b))
    return {
        "compared": len(common),
        "byte_identical_frames": byte_identical,
        "max_mae": max_mae,
    }


def verify_determinism(runs: list[dict[str, Any]], base_dir: Path,
                       mae_threshold: float = MAE_THRESHOLD) -> dict[str, Any]:
    """Pure verifier over completed run records; contract-tested."""
    completed = [r for r in runs if r.get("ok")]
    if len(completed) < 2:
        return {
            "verdict": "inconclusive",
            "reason": f"only {len(completed)} completed runs",
            "pairwise": [],
        }
    pairwise = []
    for i in range(len(completed) - 1):
        pairwise.append(
            {"runs": [completed[i]["run_id"], completed[i + 1]["run_id"]],
             **_compare_run_pair(completed[i], completed[i + 1], base_dir)}
        )
    if all(p["byte_identical_frames"] == p["compared"] for p in pairwise):
        verdict = "byte_identical"
    elif all(
        p["max_mae"] is not None and p["max_mae"] <= mae_threshold
        for p in pairwise
    ):
        verdict = "metric_stable"
    else:
        verdict = "nondeterministic"
    return {"verdict": verdict, "mae_threshold": mae_threshold,
            "pairwise": pairwise}


def run_determinism(
    player_path: Path,
    input_media: Path,
    output_dir: Path,
    runs: int = 3,
    frames: int = 6,
    warmup: int = 2,
    env_overrides: dict[str, str] | None = None,
    timeout_s: int = 120,
) -> dict[str, Any]:
    """Execute the controlled input ``runs`` times and classify repeatability."""
    run_records = []
    for index in range(runs):
        record = run_player(
            player_path, input_media, output_dir / f"run_{index:02d}",
            run_id=f"FFW-T0-2-det-{index:02d}", frames=frames, warmup=warmup,
            env_overrides=env_overrides, timeout_s=timeout_s,
        )
        record["dump_dir"] = f"run_{index:02d}/dumps"
        run_records.append(record)
    verification = verify_determinism(run_records, output_dir)
    return {
        "schema": SCHEMA,
        "generated_utc": datetime.now(timezone.utc).strftime("%Y-%m-%dT%H:%M:%SZ"),
        "player": str(player_path),
        "input_media": str(input_media),
        "runs_requested": runs,
        "frames": frames,
        "warmup": warmup,
        "runs": [
            {k: r[k] for k in ("run_id", "ok", "dumps", "dump_dir",
                               "timed_out", "terminated_after_capture")}
            for r in run_records
        ],
        "verification": verification,
        "comparison_rule": (
            "Later A/B testing must pair arms captured under this envelope: "
            "byte-identical repeats make frame hashes valid provenance keys; "
            "otherwise spatial deltas within the recorded MAE threshold are "
            "the maximum expected run-to-run noise."
        ),
    }


def main(argv: list[str] | None = None) -> int:
    import argparse

    parser = argparse.ArgumentParser(
        description="Run the Gate-0 determinism probe."
    )
    parser.add_argument("--player", required=True)
    parser.add_argument("--input", required=True)
    parser.add_argument("--output-dir", required=True)
    parser.add_argument("--report", required=True)
    parser.add_argument("--runs", type=int, default=3)
    parser.add_argument("--frames", type=int, default=6)
    parser.add_argument("--warmup", type=int, default=2)
    parser.add_argument("--timeout", type=int, default=120)
    args = parser.parse_args(argv)

    report = run_determinism(
        player_path=Path(args.player),
        input_media=Path(args.input),
        output_dir=Path(args.output_dir),
        runs=args.runs,
        frames=args.frames,
        warmup=args.warmup,
        timeout_s=args.timeout,
    )
    report_path = Path(args.report)
    report_path.parent.mkdir(parents=True, exist_ok=True)
    report_path.write_text(
        json.dumps(report, indent=2, sort_keys=True), encoding="utf-8"
    )
    print(report["verification"]["verdict"])
    return 0 if report["verification"]["verdict"] in ("byte_identical",
                                                      "metric_stable") else 2


if __name__ == "__main__":
    sys.exit(main())
