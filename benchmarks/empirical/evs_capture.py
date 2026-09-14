"""Capture the frozen empirical viability primary matrix.

The runner is intentionally serial.  It never changes source or player
defaults, never overwrites an existing cell directory, and preserves failed
launches beside their logs.  Run it only after CP-A has been pushed.
"""

from __future__ import annotations

import argparse
import hashlib
import json
import os
import subprocess
import sys
from pathlib import Path
from typing import Any

ROOT = Path(__file__).resolve().parents[2]
if str(ROOT) not in sys.path:
    sys.path.insert(0, str(ROOT))

from benchmarks.empirical import experiment_identity
from benchmarks.empirical.evs_manifest import (
    CAMPAIGN_ID,
    FRAME_COUNT,
    HR_HEIGHT,
    HR_WIDTH,
    METHODS,
    matrix,
)
from benchmarks.empirical.player_run import run_player


def sha256_file(path: Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as handle:
        for chunk in iter(lambda: handle.read(1024 * 1024), b""):
            digest.update(chunk)
    return digest.hexdigest()


def read_json(path: Path) -> dict[str, Any]:
    return json.loads(path.read_text(encoding="utf-8"))


def experiment_id(cell_id: str) -> str:
    return hashlib.sha256(f"{CAMPAIGN_ID}:{cell_id}".encode("utf-8")).hexdigest()[:32]


def run_external_lanczos(input_media: Path, output_dir: Path) -> dict[str, Any]:
    output_dir.mkdir(parents=True, exist_ok=False)
    pattern = output_dir / "frame_%04d.ppm"
    command = [
        "ffmpeg", "-hide_banner", "-loglevel", "error", "-y",
        "-i", str(input_media),
        "-vf", f"scale={HR_WIDTH}:{HR_HEIGHT}:flags=lanczos",
        "-frames:v", str(FRAME_COUNT), "-pix_fmt", "rgb24", str(pattern),
    ]
    completed = subprocess.run(command, text=True, stdout=subprocess.PIPE, stderr=subprocess.PIPE, check=False)
    (output_dir / "capture_command.json").write_text(
        json.dumps({"command": command, "returncode": completed.returncode, "stderr": completed.stderr}, indent=2) + "\n",
        encoding="utf-8",
    )
    if completed.returncode != 0:
        raise RuntimeError(f"Lanczos capture failed: {completed.stderr.strip()}")
    frames = sorted(output_dir.glob("frame_*.ppm"))
    if len(frames) != FRAME_COUNT:
        raise RuntimeError(f"Lanczos capture produced {len(frames)} frames, expected {FRAME_COUNT}")
    return {
        "kind": "external_ffmpeg_spatial",
        "command": command,
        "frames": [str(path) for path in frames],
        "frame_sha256": {path.name: sha256_file(path) for path in frames},
        "output_dimensions": [HR_WIDTH, HR_HEIGHT],
    }


def _actual_environment(method_id: str, re_root: Path) -> dict[str, str]:
    method = METHODS[method_id]
    environment = dict(method.get("environment", {}))
    if "TFORGE_FSR4_RE_ROOT" in environment:
        environment["TFORGE_FSR4_RE_ROOT"] = str(re_root.resolve())
    return environment


def _write(path: Path, document: dict[str, Any]) -> None:
    path.write_text(json.dumps(document, indent=2, sort_keys=True) + "\n", encoding="utf-8")


def _shared_identity(player: Path, re_root: Path) -> dict[str, Any]:
    previous = os.environ.get("TFORGE_FSR4_RE_ROOT")
    os.environ["TFORGE_FSR4_RE_ROOT"] = str(re_root.resolve())
    try:
        return {
            "schema": "temporal_forge.empirical_viability.shared_identity.v1",
            "git": experiment_identity.git_identity(ROOT),
            "binary": experiment_identity.binary_identity(player),
            "shaders": experiment_identity.shader_identity(ROOT),
            "model_assets": experiment_identity.model_asset_identity(ROOT),
            "host": experiment_identity.host_identity(),
        }
    finally:
        if previous is None:
            os.environ.pop("TFORGE_FSR4_RE_ROOT", None)
        else:
            os.environ["TFORGE_FSR4_RE_ROOT"] = previous


def capture_cell(
    cell: dict[str, str],
    fixture_root: Path,
    output_root: Path,
    player: Path,
    re_root: Path,
    fixture_manifest: dict[str, Any],
    spatial_cache: dict[str, dict[str, Any]],
    shared_identity: dict[str, Any],
    timeout_s: int,
) -> dict[str, Any]:
    cell_id = cell["cell_id"]
    cell_dir = output_root / "cells" / cell_id
    if cell_dir.exists():
        raise RuntimeError(f"refusing to overwrite existing cell directory: {cell_dir}")
    cell_dir.mkdir(parents=True, exist_ok=False)

    media_key = f"{cell['structural_class']}_{cell['regime']}_{cell['sequence'].lower()}"
    media_record = fixture_manifest["media"][media_key]
    input_media = fixture_root / media_record["input_media_relative"]
    gt_key = media_record["gt_sequence_key"]
    gt_record = fixture_manifest["media"][gt_key]
    gt_paths = [str(fixture_root / relative) for relative in gt_record["frame_paths"]]
    method_id = cell["method"]
    method = METHODS[method_id]
    run_id = experiment_id(cell_id)
    config_path = ROOT / method["config_path"] if method.get("config_path") else None

    result: dict[str, Any] = {
        "schema": "temporal_forge.empirical_viability.cell.v1",
        "campaign_id": CAMPAIGN_ID,
        "cell": cell,
        "run_id": run_id,
        "fixture": {
            "input_media": str(input_media),
            "input_media_sha256": sha256_file(input_media),
            "ground_truth_frames": gt_paths,
            "ground_truth_sequence_sha256": gt_record["combined_sha256"],
            "input_dimensions": media_record["input_media_dimensions"],
            "output_dimensions": [HR_WIDTH, HR_HEIGHT],
            "frame_count": FRAME_COUNT,
        },
        "method": {
            "id": method_id,
            "name": method["name"],
            "kind": method["kind"],
            "config_path": str(config_path) if config_path else None,
            "config_sha256": sha256_file(config_path) if config_path else None,
            "environment": _actual_environment(method_id, re_root),
        },
        "shared_identity": {
            "git_head": shared_identity["git"]["head"],
            "binary_sha256": shared_identity["binary"]["sha256"],
            "host": shared_identity["host"],
        },
    }

    if method_id == "M1":
        cache_key = media_key
        if cache_key not in spatial_cache:
            spatial_cache[cache_key] = run_external_lanczos(input_media, output_root / "spatial_cache" / cache_key)
        capture = spatial_cache[cache_key]
        result["capture"] = {
            **capture,
            "frames": [str(Path(path).resolve()) for path in capture["frames"]],
            "cache_key": cache_key,
        }
        result["identity"] = {
            "schema": "temporal_forge.empirical_viability.external_identity.v1",
            "git": shared_identity["git"],
            "binary": None,
            "input_media": experiment_identity.input_media_identity(input_media),
            "method_config_sha256": None,
            "runtime_trace_crosscheck": None,
            "verified": all(Path(path).is_file() for path in capture["frames"]),
        }
    else:
        player_dir = cell_dir / "player"
        env = _actual_environment(method_id, re_root)
        if config_path:
            env["TFORGE_QUALITY_LAB_CONFIG"] = str(config_path.resolve())
            env["TFORGE_CONFIG_SHA256"] = sha256_file(config_path)
        env["TFORGE_EXPERIMENT_ID"] = run_id
        capture = run_player(
            player,
            input_media,
            player_dir,
            run_id,
            frames=FRAME_COUNT,
            warmup=0,
            env_overrides=env,
            timeout_s=timeout_s,
        )
        result["capture"] = capture
        runtime_trace = player_dir / "runtime_trace.json"
        identity_document = {
            "schema": "temporal_forge.empirical_viability.identity.v1",
            "run_id": run_id,
            "git": experiment_identity.git_identity(ROOT),
            "binary": experiment_identity.binary_identity(player),
            "input_media": experiment_identity.input_media_identity(input_media),
            "configuration": experiment_identity.configuration_identity(
                environment={**os.environ, **env}, quality_lab_config=config_path
            ),
            "runtime_trace_crosscheck": None,
        }
        if runtime_trace.is_file():
            identity_document["runtime_trace_crosscheck"] = experiment_identity.runtime_trace_crosscheck(
                runtime_trace, identity_document
            )
        identity_document["verified"] = bool(
            capture.get("ok")
            and identity_document["runtime_trace_crosscheck"]
            and identity_document["runtime_trace_crosscheck"].get("passed")
        )
        result["identity"] = identity_document

    result["status"] = "complete" if result["capture"].get("ok", result["identity"]["verified"]) else "failed"
    _write(cell_dir / "cell.json", result)
    return result


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--manifest", type=Path, required=True)
    parser.add_argument("--fixture-root", type=Path, required=True)
    parser.add_argument("--output-root", type=Path, required=True)
    parser.add_argument("--player", type=Path, required=True)
    parser.add_argument("--re-root", type=Path, required=True)
    parser.add_argument("--timeout", type=int, default=900)
    parser.add_argument("--resume", action="store_true")
    args = parser.parse_args()

    manifest = read_json(args.manifest)
    if manifest.get("status") != "frozen-before-capture":
        raise SystemExit("manifest is not the frozen CP-A contract")
    if len(manifest.get("matrix", [])) != 64:
        raise SystemExit("primary matrix is not exactly 64 cells")
    if not args.player.is_file() or not os.access(args.player, os.X_OK):
        raise SystemExit(f"player is not executable: {args.player}")
    fixture_manifest_path = args.fixture_root / "fixture_manifest.json"
    fixture_manifest = read_json(fixture_manifest_path)
    if not args.re_root.is_dir():
        raise SystemExit(f"FSR4 RE root is not a directory: {args.re_root}")
    if args.output_root.exists() and any(args.output_root.iterdir()):
        if not args.resume:
            raise SystemExit(f"output root is not empty: {args.output_root}")
    args.output_root.mkdir(parents=True, exist_ok=True)
    shared_path = args.output_root / "shared_identity.json"
    if shared_path.is_file():
        shared = read_json(shared_path)
    else:
        shared = _shared_identity(args.player.resolve(), args.re_root.resolve())
        _write(shared_path, shared)

    cells = manifest["matrix"]
    spatial_cache: dict[str, dict[str, Any]] = {}
    completed = 0
    failed = 0
    for index, cell in enumerate(cells, start=1):
        cell_dir = args.output_root / "cells" / cell["cell_id"]
        cell_json = cell_dir / "cell.json"
        if args.resume and cell_json.is_file():
            existing = read_json(cell_json)
            if existing.get("status") == "complete":
                completed += 1
                print(f"[{index:02d}/64] resume {cell['cell_id']}")
                continue
        print(f"[{index:02d}/64] capture {cell['cell_id']}", flush=True)
        try:
            result = capture_cell(
                cell,
                args.fixture_root,
                args.output_root,
                args.player.resolve(),
                args.re_root.resolve(),
                fixture_manifest,
                spatial_cache,
                shared,
                args.timeout,
            )
            if result["status"] == "complete":
                completed += 1
            else:
                failed += 1
                print(f"  failed: {cell['cell_id']}", file=sys.stderr)
        except Exception as error:  # preserve the cell directory and continue
            failed += 1
            _write(args.output_root / "cells" / cell["cell_id"] / "failure.json", {"cell": cell, "error": repr(error)})
            print(f"  exception: {error}", file=sys.stderr)
    summary = {
        "schema": "temporal_forge.empirical_viability.capture_summary.v1",
        "campaign_id": CAMPAIGN_ID,
        "requested_cells": len(cells),
        "complete_cells": completed,
        "failed_cells": failed,
        "total_cells_attempted": completed + failed,
        "hard_maximum": 80,
    }
    _write(args.output_root / "capture_summary.json", summary)
    print(json.dumps(summary, sort_keys=True))
    return 1 if failed else 0


if __name__ == "__main__":
    raise SystemExit(main())
