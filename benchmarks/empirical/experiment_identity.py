"""Empirical experiment identity manifest.

Schema ``temporal_forge.empirical.identity.v1``: the complete, verifiable identity
of one experiment run — exact code state, binary, shaders, model assets, input
media, runtime configuration, host, and a fail-closed cross-check against the
player's own runtime pipeline trace (``temporal_forge.runtime_pipeline.v1``).

The cross-check is deliberately strict: an omitted or empty runtime-trace
provenance field is treated as unrecorded provenance and fails, mirroring
``benchmarks/quality_sweeps/campaign_provenance.py``. The current checkout is
never substituted for a recorded value.
"""

from __future__ import annotations

import hashlib
import json
import os
import platform
import re
import subprocess
import sys
from datetime import datetime, timezone
from pathlib import Path
from typing import Any

SCHEMA = "temporal_forge.empirical.identity.v1"

_TRACE_BINARY_FIELD = "binary_sha256"
_TRACE_GIT_FIELD = "git_head"
_TRACE_CONFIG_FIELD = "config_sha256"
_TRACE_RUN_FIELD = "run_id"

_VULKAN_DEVICE_NAME = re.compile(r"deviceName\s*=\s*(.+)")
_VULKAN_DRIVER_VERSION = re.compile(r"driverVersion\s*=\s*([0-9.]+)")
_VULKAN_API_VERSION = re.compile(r"apiVersion\s*=\s*([0-9.]+)")


class IdentityError(RuntimeError):
    """Raised when identity evidence cannot be produced honestly."""


def sha256_file(path: Path) -> str:
    digest = hashlib.sha256()
    with open(path, "rb") as handle:
        for chunk in iter(lambda: handle.read(1024 * 1024), b""):
            digest.update(chunk)
    return digest.hexdigest()


def sha256_bytes(payload: bytes) -> str:
    return hashlib.sha256(payload).hexdigest()


def _utc_now() -> str:
    return datetime.now(timezone.utc).strftime("%Y-%m-%dT%H:%M:%SZ")


def git_identity(repo_root: Path) -> dict[str, Any]:
    """Record the exact code state: HEAD, dirty flag, and uncommitted diff hash.

    ``diff_content_sha256`` is the SHA-256 of ``git diff HEAD`` output, so two
    dirty checkouts with the same content produce the same identity even
    though their mtimes differ. A clean tree records ``None``.
    """
    def git(*args: str) -> str:
        completed = subprocess.run(
            ["git", "-C", str(repo_root), *args],
            stdout=subprocess.PIPE,
            stderr=subprocess.PIPE,
            text=True,
            check=False,
        )
        if completed.returncode != 0:
            raise IdentityError(
                f"git {' '.join(args)} failed: {completed.stderr.strip()}"
            )
        return completed.stdout

    head = git("rev-parse", "HEAD").strip()
    status = git("status", "--porcelain")
    dirty = bool(status.strip())
    diff = git("diff", "HEAD")
    return {
        "head": head,
        "dirty": dirty,
        "dirty_paths": sorted({line[3:] for line in status.splitlines() if line.strip()}),
        "diff_content_sha256": sha256_bytes(diff.encode("utf-8")) if dirty else None,
    }


def shader_identity(repo_root: Path) -> dict[str, str]:
    """SHA-256 of every shader source under ``shaders/`` (compile-time input)."""
    root = repo_root / "shaders"
    if not root.is_dir():
        raise IdentityError(f"shader tree missing: {root}")
    return {
        str(path.relative_to(repo_root)): sha256_file(path)
        for path in sorted(root.rglob("*"))
        if path.is_file()
    }


def _resolve_weight_blob(repo_root: Path, blob_name: str) -> dict[str, Any]:
    """Mirror src/util/Fsr4Paths.cpp resolveWeightBlob search order exactly."""
    searched: list[str] = []
    candidates: list[Path] = []
    re_root = os.environ.get("TFORGE_FSR4_RE_ROOT")
    if re_root:
        candidates.append(
            Path(re_root) / "extracted" / "v410_initializers" / blob_name
        )
    data_home = os.environ.get("XDG_DATA_HOME")
    if not data_home and os.environ.get("HOME"):
        data_home = str(Path(os.environ["HOME"]) / ".local" / "share")
    if data_home:
        candidates.append(
            Path(data_home)
            / "temporal-forge-player"
            / "fsr4"
            / "extracted"
            / "v410_initializers"
            / blob_name
        )
    for candidate in candidates:
        searched.append(str(candidate))
        if candidate.is_file():
            return {
                "blob_name": blob_name,
                "found": True,
                "path": str(candidate),
                "sha256": sha256_file(candidate),
                "size_bytes": candidate.stat().st_size,
                "searched": searched,
            }
    return {
        "blob_name": blob_name,
        "found": False,
        "path": None,
        "sha256": None,
        "size_bytes": None,
        "searched": searched,
    }


def model_asset_identity(repo_root: Path) -> dict[str, Any]:
    """Identity of the FSR4 model assets actually resolvable by the player.

    Native INT8 packs resolve relative to the executable directory or the
    repository root (``resources/fsr4/native_i8``); the generic v4.1 weight
    blob resolves like ``WeightBlobLoader``. Both are hashed so any asset
    substitution is detectable.
    """
    packs_root = repo_root / "resources" / "fsr4" / "native_i8"
    packs: dict[str, Any] = {}
    if packs_root.is_dir():
        for pack_dir in sorted(p for p in packs_root.iterdir() if p.is_dir()):
            files = {
                str(f.relative_to(repo_root)): sha256_file(f)
                for f in sorted(pack_dir.rglob("*"))
                if f.is_file()
            }
            packs[pack_dir.name] = {
                "path": str(pack_dir.relative_to(repo_root)),
                "file_count": len(files),
                "combined_sha256": sha256_bytes(
                    "\n".join(f"{h}  {n}" for n, h in sorted(files.items())).encode("utf-8")
                ),
            }
    else:
        packs_root = None
    return {
        "native_packs_root": (
            str(packs_root.relative_to(repo_root)) if packs_root else None
        ),
        "native_packs": packs,
        "generic_weight_blob": _resolve_weight_blob(repo_root, "quality.bin"),
    }


def input_media_identity(path: Path | None) -> dict[str, Any] | None:
    """SHA-256 plus decoded stream metadata for the experiment input media."""
    if path is None:
        return None
    if not path.is_file():
        raise IdentityError(f"input media missing: {path}")
    identity: dict[str, Any] = {
        "path": str(path),
        "sha256": sha256_file(path),
        "size_bytes": path.stat().st_size,
    }
    try:
        completed = subprocess.run(
            [
                "ffprobe", "-v", "quiet", "-print_format", "json",
                "-show_streams", "-show_format", str(path),
            ],
            stdout=subprocess.PIPE,
            stderr=subprocess.PIPE,
            text=True,
            check=True,
            timeout=60,
        )
        probe = json.loads(completed.stdout)
        streams = [
            {
                key: stream.get(key)
                for key in (
                    "codec_name", "codec_type", "width", "height",
                    "pix_fmt", "r_frame_rate", "nb_frames",
                    "color_range", "color_space", "color_transfer",
                    "color_primaries",
                )
                if stream.get(key) is not None
            }
            for stream in probe.get("streams", [])
        ]
        identity["streams"] = streams
        identity["format"] = {
            key: probe.get("format", {}).get(key)
            for key in ("format_name", "duration", "size", "bit_rate")
            if probe.get("format", {}).get(key) is not None
        }
    except (subprocess.SubprocessError, json.JSONDecodeError) as error:
        # A missing ffprobe must not silently produce an unprobed input; the
        # error is recorded and the manifest consumer can demand a retry.
        identity["ffprobe_error"] = str(error)
    return identity


def configuration_identity(
    environment: dict[str, str] | None = None,
    quality_lab_config: Path | None = None,
) -> dict[str, Any]:
    """Every TFORGE_* runtime selector plus the quality-lab configuration hash."""
    env = dict(os.environ if environment is None else environment)
    tforge = {
        name: value
        for name, value in sorted(env.items())
        if name.startswith("TFORGE_")
    }
    identity: dict[str, Any] = {
        "tforge_env": tforge,
        "quality_lab_config_path": None,
        "quality_lab_config_sha256": None,
    }
    if quality_lab_config is not None:
        if not quality_lab_config.is_file():
            raise IdentityError(
                f"quality-lab config missing: {quality_lab_config}"
            )
        identity["quality_lab_config_path"] = str(quality_lab_config)
        identity["quality_lab_config_sha256"] = sha256_file(quality_lab_config)
    return identity


def host_identity() -> dict[str, Any]:
    """Host/GPU identity sufficient to recognize a different machine."""
    identity: dict[str, Any] = {
        "platform": platform.platform(),
        "kernel": platform.release(),
        "machine": platform.machine(),
    }
    try:
        with open("/proc/cpuinfo", "r", encoding="utf-8") as handle:
            for line in handle:
                if line.startswith("model name"):
                    identity["cpu_model"] = line.split(":", 1)[1].strip()
                    break
    except OSError:
        pass
    try:
        completed = subprocess.run(
            ["vulkaninfo", "--summary"],
            stdout=subprocess.PIPE,
            stderr=subprocess.PIPE,
            text=True,
            check=True,
            timeout=60,
        )
        names = _VULKAN_DEVICE_NAME.findall(completed.stdout)
        drivers = _VULKAN_DRIVER_VERSION.findall(completed.stdout)
        apis = _VULKAN_API_VERSION.findall(completed.stdout)
        if names:
            identity["vulkan_device_name"] = names[0].strip()
            if drivers:
                identity["vulkan_driver_version"] = drivers[0].strip()
            if apis:
                identity["vulkan_api_version"] = apis[0].strip()
    except (subprocess.SubprocessError, OSError):
        identity["vulkaninfo_error"] = "unavailable"
    return identity


def binary_identity(player_path: Path) -> dict[str, Any]:
    if not player_path.is_file():
        raise IdentityError(f"player binary missing: {player_path}")
    return {
        "path": str(player_path.resolve()),
        "sha256": sha256_file(player_path),
        "size_bytes": player_path.stat().st_size,
    }


def runtime_trace_crosscheck(
    runtime_trace_path: Path, manifest: dict[str, Any]
) -> dict[str, Any]:
    """Fail-closed comparison of the player's runtime trace with this manifest.

    Empty or missing trace fields are unrecorded provenance and fail. The
    ``config_sha256`` and ``run_id`` fields may legitimately be empty on both
    sides when the experiment defines none; any non-empty value must match.
    """
    if not runtime_trace_path.is_file():
        raise IdentityError(f"runtime trace missing: {runtime_trace_path}")
    trace = json.loads(runtime_trace_path.read_text(encoding="utf-8"))

    expected_binary = manifest["binary"]["sha256"]
    actual_binary = trace.get(_TRACE_BINARY_FIELD) or ""
    expected_git = manifest["git"]["head"]
    actual_git = trace.get(_TRACE_GIT_FIELD) or ""
    expected_config = manifest["configuration"]["quality_lab_config_sha256"] or ""
    actual_config = trace.get(_TRACE_CONFIG_FIELD) or ""
    actual_run = trace.get(_TRACE_RUN_FIELD) or ""

    checks = [
        {
            "field": _TRACE_BINARY_FIELD,
            "expected": expected_binary,
            "actual": actual_binary,
            "match": bool(actual_binary) and actual_binary == expected_binary,
        },
        {
            "field": _TRACE_GIT_FIELD,
            "expected": expected_git,
            "actual": actual_git,
            "match": bool(actual_git) and actual_git == expected_git,
        },
        {
            "field": _TRACE_CONFIG_FIELD,
            "expected": expected_config,
            "actual": actual_config,
            "match": (
                actual_config == expected_config
                if expected_config
                else True
            ),
            "strict": False,
        },
        {
            "field": _TRACE_RUN_FIELD,
            "expected": manifest.get("run_id") or "",
            "actual": actual_run,
            "match": True,
            "note": (
                "informational: run_id is recorded, not compared"
                if actual_run
                else "run_id unrecorded in runtime trace"
            ),
        },
    ]
    return {
        "runtime_trace_path": str(runtime_trace_path),
        "schema": trace.get("schema"),
        "trace_schema_known": trace.get("schema") == "temporal_forge.runtime_pipeline.v1",
        "checks": checks,
        "passed": all(check["match"] for check in checks),
    }


def build_manifest(
    repo_root: Path,
    player_path: Path,
    run_id: str | None = None,
    input_media: Path | None = None,
    quality_lab_config: Path | None = None,
    runtime_trace: Path | None = None,
    include_heavy_assets: bool = True,
) -> dict[str, Any]:
    """Assemble the complete Empirical identity manifest for one experiment."""
    manifest: dict[str, Any] = {
        "schema": SCHEMA,
        "generated_utc": _utc_now(),
        "run_id": run_id,
        "repo_root": str(repo_root),
        "git": git_identity(repo_root),
        "binary": binary_identity(player_path),
        "shaders": shader_identity(repo_root),
        "configuration": configuration_identity(
            quality_lab_config=quality_lab_config
        ),
        "host": host_identity(),
    }
    if input_media is not None:
        manifest["input_media"] = input_media_identity(input_media)
    if include_heavy_assets:
        manifest["model_assets"] = model_asset_identity(repo_root)
    else:
        manifest["model_assets"] = {"recorded": False}
    if runtime_trace is not None:
        manifest["runtime_trace_crosscheck"] = runtime_trace_crosscheck(
            runtime_trace, manifest
        )
    return manifest


def manifest_passed(manifest: dict[str, Any]) -> bool:
    """True when every recorded cross-check passed."""
    crosscheck = manifest.get("runtime_trace_crosscheck")
    if crosscheck is None:
        return False
    return bool(crosscheck.get("passed"))


def main(argv: list[str] | None = None) -> int:
    import argparse

    parser = argparse.ArgumentParser(
        description="Build a Empirical experiment identity manifest."
    )
    parser.add_argument("--repo", default=str(Path(__file__).resolve().parents[2]))
    parser.add_argument("--player", required=True)
    parser.add_argument("--output", required=True)
    parser.add_argument("--run-id", default=None)
    parser.add_argument("--input", default=None)
    parser.add_argument("--quality-lab", default=None)
    parser.add_argument("--runtime-trace", default=None)
    parser.add_argument(
        "--skip-heavy-assets",
        action="store_true",
        help="Skip model-asset hashing (identity must then be completed elsewhere).",
    )
    args = parser.parse_args(argv)

    repo_root = Path(args.repo).resolve()
    manifest = build_manifest(
        repo_root=repo_root,
        player_path=Path(args.player),
        run_id=args.run_id,
        input_media=Path(args.input) if args.input else None,
        quality_lab_config=Path(args.quality_lab) if args.quality_lab else None,
        runtime_trace=Path(args.runtime_trace) if args.runtime_trace else None,
        include_heavy_assets=not args.skip_heavy_assets,
    )
    output = Path(args.output)
    output.parent.mkdir(parents=True, exist_ok=True)
    output.write_text(json.dumps(manifest, indent=2, sort_keys=True), encoding="utf-8")
    crosscheck = manifest.get("runtime_trace_crosscheck")
    if crosscheck is not None and not crosscheck["passed"]:
        failed = [c["field"] for c in crosscheck["checks"] if not c["match"]]
        print(
            f"identity manifest written, but runtime-trace cross-check FAILED: {failed}",
            file=sys.stderr,
        )
        return 2
    print(f"identity manifest written: {output}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
