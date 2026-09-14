"""Frozen experiment contract for the empirical viability sprint.

This module owns the 64-cell primary matrix and the exact method overlays.
It deliberately contains no result interpretation and never mutates player
code or runtime defaults.  The fixture generator and capture runner import
these values so the written manifests and executed cells cannot drift apart.
"""

from __future__ import annotations

import argparse
import hashlib
import json
import subprocess
from pathlib import Path
from typing import Any

REPO_ROOT = Path(__file__).resolve().parents[2]

SCHEMA = "temporal_forge.empirical_viability.contract.v1"
CAMPAIGN_ID = "evs-20260914"
HR_WIDTH = 1920
HR_HEIGHT = 1080
LR_WIDTH = 640
LR_HEIGHT = 360
FRAME_RATE = 8
FRAME_COUNT = 8
SEED = 20260914

SEQUENCES: dict[str, dict[str, Any]] = {
    "SEQ-A": {
        "velocity_hr_pixels_per_frame": [0.37, 0.23],
        "phase_origin_hr_pixels": [0.11, 0.07],
    },
    "SEQ-B": {
        "velocity_hr_pixels_per_frame": [0.19, 0.41],
        "phase_origin_hr_pixels": [0.43, 0.29],
    },
}

REGIMES: dict[str, dict[str, Any]] = {
    "R1": {
        "name": "phase-rich-known-fractional-motion",
        "prefilter": {"kind": "gaussian", "sigma": 0.70},
        "purpose": "retain complementary recoverable high frequency",
    },
    "R2": {
        "name": "strong-prefilter-suppression-control",
        "prefilter": {"kind": "gaussian", "sigma": 2.20},
        "purpose": "suppress complementary recoverable high frequency before decimation",
    },
}

STRUCTURAL_CLASSES: dict[str, dict[str, Any]] = {
    "S1": {
        "name": "thin-geometry",
        "generator": "thin_geometry_v1",
        "description": "subpixel diagonals, crossings, and narrow rings",
    },
    "S2": {
        "name": "fine-text-glyphs",
        "generator": "fine_text_glyphs_v1",
        "description": "small glyphs, strokes, and high-contrast counters",
    },
    "S3": {
        "name": "repeating-texture",
        "generator": "repeating_texture_v1",
        "description": "phase-sensitive periodic bands and checker texture",
    },
    "S4": {
        "name": "natural-detail",
        "generator": "natural_detail_v1",
        "description": "multi-scale stochastic detail with soft natural forms",
    },
}

COMMON_PLAYER_ENV: dict[str, str] = {
    "TFORGE_BENCHMARK_PRESET": "Quality",
    "TFORGE_FSR4_FORCE_VIEWPORT": "1920x1080",
    "TFORGE_FSR4_FORCE_SCALE": "1.50",
    "TFORGE_FSR4_DISABLE_NATIVE_INT8": "1",
    "TFORGE_DISABLE_HW_DECODE": "1",
    "TFORGE_FSR4_DISABLE_CAS": "1",
    "TFORGE_FSR4_RE_ROOT": "<RE_ROOT>",
}

METHODS: dict[str, dict[str, Any]] = {
    "M1": {
        "name": "lanczos3-external-spatial",
        "kind": "external_ffmpeg_spatial",
        "description": "Lanczos3 resize of the decoded low-resolution input",
        "config_path": None,
        "environment": {},
        "filter": "lanczos",
    },
    "M2": {
        "name": "base-only-bilinear",
        "kind": "player_quality_lab_spatial",
        "description": "strongest existing deterministic spatial baseline",
        "config_path": "benchmarks/quality_sweeps/stage_a/base_only_bilinear.json",
        "environment": {
            **COMMON_PLAYER_ENV,
            "TFORGE_FSR4_JITTER_MODE": "off",
            "TFORGE_FSR4_DISABLE_COLOR_HISTORY": "1",
            "TFORGE_FSR4_DISABLE_RECURRENT": "1",
        },
        "evidence": "docs/reports/M6_RECAPTURE_REPORT_20260901.md",
    },
    "M3": {
        "name": "pre-sprint-forge-default",
        "kind": "player_forge_fsr4",
        "description": "frozen pre-sprint Forge path with Quality Lab disabled",
        "config_path": "benchmarks/quality_sweeps/stage_h_temporal_tiny/current_control.json",
        "environment": {
            **COMMON_PLAYER_ENV,
            "TFORGE_FSR4_JITTER_MODE": "synthetic",
            "TFORGE_FSR4_JITTER_SEQUENCE": "halton23",
            "TFORGE_FSR4_ENABLE_COLOR_HISTORY": "1",
            "TFORGE_FSR4_ENABLE_RECURRENT": "1",
        },
        "evidence": "benchmarks/quality_sweeps/swarm/agent_clean_native_default/measured_results.json",
    },
    "M4": {
        "name": "existing-current-composition",
        "kind": "player_forge_fsr4",
        "description": "existing justified explicit current-composition control",
        "config_path": "benchmarks/quality_sweeps/swarm/agent_composition_audit/current_control.json",
        "environment": {
            **COMMON_PLAYER_ENV,
            "TFORGE_FSR4_JITTER_MODE": "synthetic",
            "TFORGE_FSR4_JITTER_SEQUENCE": "halton23",
            "TFORGE_FSR4_ENABLE_COLOR_HISTORY": "1",
            "TFORGE_FSR4_ENABLE_RECURRENT": "1",
        },
        "evidence": "benchmarks/quality_sweeps/swarm/agent_composition_audit/README.md",
    },
}


def sha256_file(path: Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as handle:
        for chunk in iter(lambda: handle.read(1024 * 1024), b""):
            digest.update(chunk)
    return digest.hexdigest()


def git(*args: str) -> str:
    completed = subprocess.run(
        ["git", "-C", str(REPO_ROOT), *args],
        text=True,
        stdout=subprocess.PIPE,
        stderr=subprocess.PIPE,
        check=False,
    )
    if completed.returncode != 0:
        raise RuntimeError(completed.stderr.strip() or f"git {' '.join(args)} failed")
    return completed.stdout.strip()


def matrix() -> list[dict[str, str]]:
    cells: list[dict[str, str]] = []
    for structural in STRUCTURAL_CLASSES:
        for regime in REGIMES:
            for method in METHODS:
                for sequence in SEQUENCES:
                    cells.append(
                        {
                            "cell_id": f"{structural}_{regime}_{method}_{sequence.lower()}",
                            "structural_class": structural,
                            "regime": regime,
                            "method": method,
                            "sequence": sequence,
                        }
                    )
    return cells


def _config_identity(method: dict[str, Any]) -> dict[str, Any]:
    relative = method.get("config_path")
    if not relative:
        return {"path": None, "sha256": None}
    path = REPO_ROOT / relative
    if not path.is_file():
        raise RuntimeError(f"method config missing: {path}")
    return {"path": relative, "sha256": sha256_file(path)}


def _fixture_identity(fixture_manifest: Path | None) -> dict[str, Any]:
    if fixture_manifest is None:
        return {
            "manifest_path": None,
            "manifest_sha256": None,
            "media": {},
        }
    if not fixture_manifest.is_file():
        raise RuntimeError(f"fixture manifest missing: {fixture_manifest}")
    document = json.loads(fixture_manifest.read_text(encoding="utf-8"))
    media = document.get("media", {})
    return {
        "manifest_path": "EVS_FIXTURE_ROOT/fixture_manifest.json",
        "manifest_sha256": sha256_file(fixture_manifest),
        "media": media,
    }


def build_manifest(fixture_manifest: Path | None = None) -> dict[str, Any]:
    head = git("rev-parse", "HEAD")
    status = git("status", "--porcelain")
    return {
        "schema": SCHEMA,
        "campaign_id": CAMPAIGN_ID,
        "status": "frozen-before-capture",
        "baseline_parent_commit": head,
        "branch": git("branch", "--show-current"),
        "dirty_at_freeze": bool(status),
        "dirty_paths_at_freeze": [line[3:] for line in status.splitlines() if line.strip()],
        "fixture": {
            "seed": SEED,
            "hr_dimensions": [HR_WIDTH, HR_HEIGHT],
            "lr_dimensions": [LR_WIDTH, LR_HEIGHT],
            "output_dimensions": [HR_WIDTH, HR_HEIGHT],
            "frame_rate": FRAME_RATE,
            "frame_count": FRAME_COUNT,
            "decimation": [3, 3],
            "color_format": "8-bit RGB PNG source; FFV1 yuv444p input media",
            "classes": STRUCTURAL_CLASSES,
            "regimes": REGIMES,
            "sequences": SEQUENCES,
        },
        "methods": {
            method_id: {
                **method,
                "config": _config_identity(method),
            }
            for method_id, method in METHODS.items()
        },
        "limits": {"primary_cells": 64, "confirmation_cells_max": 16, "total_cells_max": 80},
        "matrix": matrix(),
        "fixture_identity": _fixture_identity(fixture_manifest),
        "excluded": [
            "AMD parity/oracle investigation",
            "DLL or FidelityFX SDK archaeology",
            "historical Final Word campaign authority and ledgers",
            "new reconstruction tuning, weights, topology, or filters",
            "same-session independent-review artifacts as active authority",
        ],
    }


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--output", type=Path, required=True)
    parser.add_argument("--fixture-manifest", type=Path)
    args = parser.parse_args()
    document = build_manifest(args.fixture_manifest)
    args.output.parent.mkdir(parents=True, exist_ok=True)
    args.output.write_text(json.dumps(document, indent=2, sort_keys=True) + "\n", encoding="utf-8")
    print(f"wrote {len(document['matrix'])} cells to {args.output}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
