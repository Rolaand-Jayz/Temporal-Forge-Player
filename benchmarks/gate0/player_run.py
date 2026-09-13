"""Gate-0 controlled player launcher.

One place owns the launch recipe so every Gate-0 capture has identical
mechanics: isolated configuration home, headless benchmark auto-exit, output
dump selection, runtime-trace destination, and git provenance environment.
The launcher records what it did — a failed or timed-out launch is reported,
never silently ignored.
"""

from __future__ import annotations

import hashlib
import os
import subprocess
import time
from pathlib import Path
from typing import Any

REPO_ROOT = Path(__file__).resolve().parents[2]

_GIT_ENV = ("TFORGE_GIT_HEAD", "TFORGE_GIT_DIRTY")


def _git_provenance() -> dict[str, str]:
    head = subprocess.run(
        ["git", "-C", str(REPO_ROOT), "rev-parse", "HEAD"],
        stdout=subprocess.PIPE, stderr=subprocess.PIPE, text=True, check=False,
    )
    status = subprocess.run(
        ["git", "-C", str(REPO_ROOT), "status", "--porcelain"],
        stdout=subprocess.PIPE, stderr=subprocess.PIPE, text=True, check=False,
    )
    return {
        "TFORGE_GIT_HEAD": head.stdout.strip() if head.returncode == 0 else "",
        "TFORGE_GIT_DIRTY": "1" if status.stdout.strip() else "0",
    }


def _dump_inventory(dump_dir: Path) -> dict[str, str]:
    return {
        entry.name: hashlib.sha256(entry.read_bytes()).hexdigest()
        for entry in sorted(dump_dir.iterdir())
        if entry.is_file()
    }


def run_player(
    player_path: Path,
    input_media: Path,
    output_dir: Path,
    run_id: str,
    frames: int = 8,
    warmup: int = 0,
    env_overrides: dict[str, str] | None = None,
    timeout_s: int = 300,
) -> dict[str, Any]:
    """Launch the player once under a fully recorded environment.

    The player is polled and terminated as soon as the complete dump sequence
    and runtime trace exist — playback is realtime-paced, so waiting for the
    full capture timeout would idle for minutes after the capture completed.
    The timeout remains a hard guard against a stalled player.

    Returns a record with the exit status, dumped-output inventory (SHA-256
    per file), the runtime-trace path, and the exact environment used. A
    timeout is a recorded failure: the player process is terminated and the
    record says so.
    """
    player_path = player_path.resolve()
    input_media = input_media.resolve()
    if not player_path.is_file():
        raise FileNotFoundError(f"player binary missing: {player_path}")
    if not input_media.is_file():
        raise FileNotFoundError(f"input media missing: {input_media}")

    dumps_dir = output_dir / "dumps"
    events_dir = output_dir / "events"
    config_home = output_dir / "config"
    dumps_dir.mkdir(parents=True, exist_ok=True)
    events_dir.mkdir(parents=True, exist_ok=True)
    (config_home / "temporal-forge-player").mkdir(parents=True, exist_ok=True)
    output_dir.mkdir(parents=True, exist_ok=True)

    env = dict(os.environ)
    env["TFORGE_HEADLESS_BENCHMARK"] = "1"
    env["XDG_CONFIG_HOME"] = str(config_home)
    env["TFORGE_FSR4_DUMP_SEQUENCE"] = str(frames)
    env["TFORGE_FSR4_DUMP_SEQUENCE_WARMUP"] = str(warmup)
    env["TFORGE_FSR4_DUMP_SEQUENCE_DIR"] = str(dumps_dir)
    # Every Gate-0 capture carries the per-frame state-generation trace so
    # producer identity and reset generations are always auditable (R3).
    env["TFORGE_FSR4_DUMP_EVENT_TRACE"] = "1"
    env["TFORGE_FSR4_DUMP_EVENT_DIR"] = str(events_dir)
    env["TFORGE_RUNTIME_TRACE_PATH"] = str(output_dir / "runtime_trace.json")
    env["TFORGE_EXPERIMENT_ID"] = run_id
    env.update(_git_provenance())
    for name in _GIT_ENV:
        if not env[name]:
            env.pop(name, None)
    if env_overrides:
        env.update(env_overrides)

    log_path = output_dir / "player.log"
    runtime_trace = output_dir / "runtime_trace.json"

    def capture_complete(previous_inventory: dict[str, int]) -> bool:
        """Complete when all dumps exist, the trace exists, AND the dump
        inventory (name → size) is unchanged since the previous poll. At 4K a
        25 MB dump file can exist for several hundred milliseconds while still
        being flushed; terminating on mere existence truncates the payload."""
        entries = {
            entry.name: entry.stat().st_size
            for entry in dumps_dir.glob("temporal_forge_fsr4_*.ppm")
            if entry.is_file()
        }
        dumped = len(entries)
        stable = bool(entries) and entries == previous_inventory
        previous_inventory.clear()
        previous_inventory.update(entries)
        return dumped >= frames and runtime_trace.is_file() and stable

    def terminate(process: subprocess.Popen) -> None:
        process.terminate()
        try:
            process.wait(timeout=10)
        except subprocess.TimeoutExpired:
            process.kill()
            process.wait()

    started = time.time()
    timed_out = False
    terminated_after_capture = False
    exit_code: int | None = None
    previous_inventory: dict[str, int] = {}
    with open(log_path, "w", encoding="utf-8") as log:
        process = subprocess.Popen(
            [str(player_path), str(input_media)],
            cwd=str(REPO_ROOT),
            env=env,
            stdout=log,
            stderr=subprocess.STDOUT,
        )
        while True:
            code = process.poll()
            if code is not None:
                exit_code = code
                break
            elapsed = time.time() - started
            if elapsed >= timeout_s:
                timed_out = True
                terminate(process)
                break
            if capture_complete(previous_inventory):
                terminated_after_capture = True
                terminate(process)
                exit_code = process.returncode
                break
            time.sleep(0.5)
    elapsed_s = time.time() - started

    record: dict[str, Any] = {
        "run_id": run_id,
        "player": str(player_path),
        "input_media": str(input_media),
        "output_dir": str(output_dir),
        "exit_code": exit_code,
        "timed_out": timed_out,
        "terminated_after_capture": terminated_after_capture,
        "timeout_s": timeout_s,
        "elapsed_s": round(elapsed_s, 3),
        "frames_requested": frames,
        "warmup_requested": warmup,
        "dumps": _dump_inventory(dumps_dir),
        "runtime_trace_path": str(output_dir / "runtime_trace.json"),
        "player_log": str(log_path),
        "environment": {
            name: value
            for name, value in sorted(env.items())
            if name.startswith("TFORGE_") or name == "XDG_CONFIG_HOME"
        },
    }
    record["dumps_present"] = len(record["dumps"])
    record["dumps_expected"] = frames
    # The player does not self-exit; the capture-harness timeout is the
    # documented termination guard (run_temporal_quality.sh convention). A
    # timed-out launch is therefore acceptable iff the complete dump sequence
    # and the runtime trace were written before the guard fired.
    trace_ok = (output_dir / "runtime_trace.json").is_file()
    record["runtime_trace_present"] = trace_ok
    record["ok"] = record["dumps_present"] == frames and trace_ok
    return record
