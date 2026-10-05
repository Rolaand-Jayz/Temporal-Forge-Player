#!/usr/bin/env python3
"""ANVIL end-to-end arms evidence suite (BUILD_READY contract: arms isolate
failures; docs/anvil/RUNBOOK.md "Test arms" table).

Scope: EVIDENCE, not quality. Each test runs the anvil_runner binary as a
subprocess with one arm's config and asserts exit code plus manifest /
artifact properties (frame accounting, stage names, oracle-arm list, output
hashes/bytes). No reconstruction-quality claim is made anywhere.

The runner binary is BUILT ONCE per session into the suite's own build dir
(repo_root/build-arms-e2e) via cmake+ninja; the deterministic fixture is
generated once per session by tools/anvil/make_fixtures.py. Skips cleanly
when ffmpeg is absent (fixture exit 77) or when cmake/ninja/ffmpeg tools
are missing.

Run: python3 -m pytest tests/anvil/test_anvil_arms_e2e.py -v
stdlib only (plus pytest as the test runner).
"""
import array
import json
import os
import shutil
import struct
import subprocess
import sys
from pathlib import Path

import pytest

REPO_ROOT = Path(__file__).resolve().parents[2]
BUILD_DIR = REPO_ROOT / "build-arms-e2e"
RUNNER = BUILD_DIR / "anvil_runner"
MAKE_FIXTURES = REPO_ROOT / "tools" / "anvil" / "make_fixtures.py"
MAKE_VFR = REPO_ROOT / "tools" / "anvil" / "make_vfr_fixture.py"
# Session scratch root; prefer /mnt/workdrive (task contract), fall back to
# the system temp dir. Set TFORGE_ANVIL_E2E_KEEP=1 to keep artifacts.
SCRATCH_PARENT = "/mnt/workdrive" if Path("/mnt/workdrive").is_dir() else None

# The fixture clip is encoded by libx264 without transfer metadata; the
# working-space boundary refuses unspecified transfer without an override
# (measured: exit 4 without, exit 0 with). "srgb" is the fixture's declared
# space (oracle ground-truth meta transfer=13).
TRANSFER_OVERRIDE = "srgb"

BASE_CONFIG = {
    "inputPath": "",          # filled by the fixture
    "firstFrame": 0,
    "lastFrame": -1,
    "past": 0,
    "future": 0,
    "sideInfoNormalizer": "passthrough",
    "coarse": "blocksearch",
    "refine": "none",
    "visibility": "photo_fwbw",
    "geometry": "unknown",
    "reconstruct": "weighted_blend",
    "backend": "passthrough",
    "oracleSidecar": "",
    "deterministic": True,
    "seed": 0,
    "captureDir": "",
    "captureStages": "",
    "outputDir": "",          # filled per run
    "transferOverride": TRANSFER_OVERRIDE,
    "dropNeighbors": "",
}


# ---------------------------------------------------------------------------
# Session fixtures: build the runner once, generate the fixture once.
# ---------------------------------------------------------------------------
@pytest.fixture(scope="session")
def scratch_root(request):
    root = Path(__import__("tempfile").mkdtemp(prefix="anvil_arms_e2e_",
                                               dir=SCRATCH_PARENT))

    def _cleanup():
        if not os.environ.get("TFORGE_ANVIL_E2E_KEEP"):
            shutil.rmtree(root, ignore_errors=True)

    request.addfinalizer(_cleanup)
    return root


@pytest.fixture(scope="session")
def runner(scratch_root):
    """Build anvil_runner once into the suite's own build dir."""
    if not MAKE_FIXTURES.exists():  # repo layout sanity
        pytest.fail("tools/anvil/make_fixtures.py not found relative to "
                    "the test file")
    try:
        cfg = subprocess.run(
            ["cmake", "-S", str(REPO_ROOT), "-B", str(BUILD_DIR),
             "-G", "Ninja", "-DCMAKE_BUILD_TYPE=Release"],
            capture_output=True, text=True)
        cfg.check_returncode()
        bld = subprocess.run(
            ["ninja", "-C", str(BUILD_DIR), "anvil_runner"],
            capture_output=True, text=True)
        bld.check_returncode()
    except FileNotFoundError as e:
        pytest.skip(f"build tool missing: {e}")
    except subprocess.CalledProcessError as e:
        pytest.fail(f"anvil_runner build failed:\n{e.stdout}\n{e.stderr}")
    return RUNNER


@pytest.fixture(scope="session")
def fixture_dir(runner, scratch_root):
    """Generate the deterministic CFR fixture once (includes clip.mp4)."""
    out = scratch_root / "fixture"
    try:
        proc = subprocess.run(
            [sys.executable, str(MAKE_FIXTURES), "--out", str(out)],
            capture_output=True, text=True)
    except OSError as e:
        pytest.skip(f"cannot run make_fixtures.py: {e}")
    if proc.returncode == 77:
        pytest.skip("ffmpeg absent: make_fixtures.py exited 77 (clip "
                    "not generated)")
    assert proc.returncode == 0, f"make_fixtures.py failed:\n{proc.stderr}"
    assert (out / "clip.mp4").is_file(), "fixture clip.mp4 missing"
    assert (out / "oracle_index.json").is_file()
    return out


# ---------------------------------------------------------------------------
# Helpers
# ---------------------------------------------------------------------------
def run_arm(runner, scratch_root, name, overrides):
    """Write one arm's config and run the runner. Returns (rc, manifest,
    output_dir). The config carries every canonical field explicitly so the
    config hash is stable regardless of parser defaults."""
    out_dir = scratch_root / "arms" / name
    cfg = dict(BASE_CONFIG)
    cfg["inputPath"] = str(scratch_root / "fixture" / "clip.mp4")
    cfg["outputDir"] = str(out_dir)
    cfg.update(overrides)
    cfg_path = scratch_root / "arms" / (name + ".json")
    cfg_path.parent.mkdir(parents=True, exist_ok=True)
    cfg_path.write_text(json.dumps(cfg, sort_keys=True), encoding="ascii")
    proc = subprocess.run([str(runner), "--config", str(cfg_path)],
                          capture_output=True, text=True)
    manifest_path = out_dir / "run_manifest.json"
    manifest = None
    if manifest_path.is_file():
        manifest = json.loads(manifest_path.read_text(encoding="ascii"))
    return proc.returncode, proc, manifest, out_dir


def read_npy(path):
    """Minimal np v1.0 reader for '<f4' payloads. Returns (shape, floats).
    Byte format pinned by anvil::writeNpy (src/anvil/capture/Capture.cpp)."""
    blob = Path(path).read_bytes()
    assert blob[:6] == b"\x93NUMPY", f"{path}: not an npy file"
    major, _minor = blob[6], blob[7]
    assert major == 1, f"{path}: unexpected npy version {major}"
    (hdr_len,) = struct.unpack_from("<H", blob, 8)
    header = blob[10:10 + hdr_len].decode("ascii")
    assert "'descr': '<f4'" in header, f"{path}: expected <f4 payload"
    shape_str = header.split("'shape':")[1].split(")")[0].strip("( ")
    shape = tuple(int(s) for s in shape_str.split(",") if s.strip())
    data = blob[10 + hdr_len:]
    a = array.array("f")
    a.frombytes(data[: len(data) // 4 * 4])
    if sys.byteorder == "big":
        a.byteswap()
    return shape, a.tolist()


def npy_files(out_dir):
    return sorted(p for p in Path(out_dir).glob("out_*.npy"))


def assert_outputs_identical(dir_a, dir_b):
    fa, fb = npy_files(dir_a), npy_files(dir_b)
    assert [p.name for p in fa] == [p.name for p in fb]
    for pa, pb in zip(fa, fb):
        assert pa.read_bytes() == pb.read_bytes(), \
            f"{pa.name} differs between {dir_a} and {dir_b}"


# ---------------------------------------------------------------------------
# Arms: one test per arm; exit code + manifest/artifact evidence.
# ---------------------------------------------------------------------------
def test_single_frame_control(runner, scratch_root, fixture_dir):
    """reconstruct=center with past=0,future=0: the run succeeds, every one
    of the 16 fixture frames is emitted (no warmup/trailing loss), and two
    identical-config runs produce byte-identical outputs."""
    rc, proc, man, out1 = run_arm(runner, scratch_root, "single_frame", {
        "reconstruct": "center", "past": 0, "future": 0,
    })
    assert rc == 0, proc.stderr
    assert man["window"] == {"past": 0, "future": 0}
    assert man["frameRange"]["emitted"] == 16
    assert man["frameRange"]["skippedWarmup"] == 0
    assert man["frameRange"]["skippedTrailing"] == 0
    assert man["frameRange"]["first"] == 0 and man["frameRange"]["last"] == 15
    rc2, proc2, man2, out2 = run_arm(runner, scratch_root, "single_frame_b", {
        "reconstruct": "center", "past": 0, "future": 0,
    })
    assert rc2 == 0, proc2.stderr
    assert_outputs_identical(out1, out2)


def test_determinism_same_config_identical_outputs_and_runid(runner,
                                                             scratch_root,
                                                             fixture_dir):
    """The same config run twice: byte-identical output frames and an
    identical runId (exact-determinism evidence, reconciliation 3.7).
    The config — and therefore the outputDir, which is part of the hashed
    canonical config — must be byte-identical across the two runs, so the
    re-run targets the SAME output dir and the outputs are snapshotted
    between runs. runId excludes createdAt, so manifest timestamps may
    differ; runId and frame bytes may not."""
    overrides = {"past": 1, "future": 1, "coarse": "blocksearch",
                 "refine": "hillclimb2"}
    rc1, proc1, man1, out = run_arm(runner, scratch_root, "determinism",
                                    overrides)
    assert rc1 == 0, proc1.stderr
    run_id_1 = man1["runId"]
    outputs_1 = man1["outputs"]
    bytes_1 = {p.name: p.read_bytes() for p in npy_files(out)}

    rc2, proc2, man2, out2 = run_arm(runner, scratch_root, "determinism",
                                     overrides)
    assert rc2 == 0, proc2.stderr
    assert out2 == out, "the two runs must target the same output dir"
    assert man2["runId"] == run_id_1, "runId differs for identical config"
    assert man2["outputs"] == outputs_1  # same files, same sha256
    bytes_2 = {p.name: p.read_bytes() for p in npy_files(out2)}
    assert bytes_2 == bytes_1, "output frame bytes differ across runs"


def test_oracle_flow_estimated_confidence(runner, scratch_root, fixture_dir):
    """coarse=oracle + visibility=photo_fwbw: oracle flow consumed (arm
    recorded), confidence stays estimated, capture disabled (output dir
    holds only outputs + manifest + timings)."""
    rc, proc, man, out = run_arm(runner, scratch_root, "oracle_flow", {
        "past": 1, "coarse": "oracle", "visibility": "photo_fwbw",
        "oracleSidecar": str(scratch_root / "fixture" / "oracle_index.json"),
    })
    assert rc == 0, proc.stderr
    assert man["oracle"]["arms"] == ["coarse"]
    assert man["oracle"]["sidecar"] != ""
    assert man["stages"]["coarse"] == "oracle"
    assert man["stages"]["visibility"] == "photo_fwbw"
    # capture disabled: no captureDir configured, and nothing beyond the
    # declared artifacts landed in the output dir.
    entries = sorted(p.name for p in Path(out).iterdir())
    expected = sorted(
        ["run_manifest.json", "timings.json"] +
        [p.name for p in npy_files(out)])
    assert entries == expected, f"unexpected capture/output files: {entries}"


def test_estimated_flow_oracle_confidence(runner, scratch_root, fixture_dir):
    """coarse=blocksearch + visibility=oracle: the confidence arm is the
    only oracle consumer."""
    rc, proc, man, out = run_arm(runner, scratch_root, "est_flow", {
        "past": 1, "coarse": "blocksearch", "visibility": "oracle",
        "oracleSidecar": str(scratch_root / "fixture" / "oracle_index.json"),
    })
    assert rc == 0, proc.stderr
    assert man["oracle"]["arms"] == ["visibility"]
    assert man["stages"]["coarse"] == "blocksearch"
    assert man["stages"]["visibility"] == "oracle"


def test_oracle_geometry(runner, scratch_root, fixture_dir):
    rc, proc, man, _ = run_arm(runner, scratch_root, "geo_oracle", {
        "geometry": "oracle",
        "oracleSidecar": str(scratch_root / "fixture" / "oracle_index.json"),
    })
    assert rc == 0, proc.stderr
    assert man["stages"]["geometry"] == "oracle"
    assert "geometry" in man["oracle"]["arms"]


def test_estimated_geometry(runner, scratch_root, fixture_dir):
    rc, proc, man, _ = run_arm(runner, scratch_root, "geo_xform", {
        "geometry": "global_xform",
    })
    assert rc == 0, proc.stderr
    assert man["stages"]["geometry"] == "global_xform"
    assert man["oracle"]["arms"] == []


def test_neighbor_ablation(runner, scratch_root, fixture_dir):
    """past=1,future=1,dropNeighbors="-1": 16 frames minus 1 warmup minus 1
    trailing = 14 emitted; the dropped offset is recorded verbatim."""
    rc, proc, man, _ = run_arm(runner, scratch_root, "ablation", {
        "past": 1, "future": 1, "dropNeighbors": "-1",
    })
    assert rc == 0, proc.stderr
    assert man["dropNeighbors"] == "-1"
    assert man["frameRange"]["emitted"] == 14
    assert man["frameRange"]["skippedWarmup"] == 1
    assert man["frameRange"]["skippedTrailing"] == 1


def test_codec_prior_on_off(runner, scratch_root, fixture_dir):
    """coarse=codec (H.264 MV prior) vs coarse=blocksearch (estimator):
    both succeed and the manifests record the respective coarse stages."""
    rc_c, proc_c, man_c, _ = run_arm(runner, scratch_root, "codec_prior", {
        "past": 1, "coarse": "codec",
    })
    rc_e, proc_e, man_e, _ = run_arm(runner, scratch_root, "codec_off", {
        "past": 1, "coarse": "blocksearch",
    })
    assert rc_c == 0, proc_c.stderr
    assert rc_e == 0, proc_e.stderr
    assert man_c["stages"]["coarse"] == "codec"
    assert man_e["stages"]["coarse"] == "blocksearch"
    assert man_c["stages"]["coarse"] != man_e["stages"]["coarse"]


def test_refine_on_off(runner, scratch_root, fixture_dir):
    rc_on, proc_on, man_on, _ = run_arm(runner, scratch_root, "refine_on", {
        "past": 1, "refine": "hillclimb2",
    })
    rc_off, proc_off, man_off, _ = run_arm(runner, scratch_root,
                                           "refine_off", {
        "past": 1, "refine": "none",
    })
    assert rc_on == 0, proc_on.stderr
    assert rc_off == 0, proc_off.stderr
    assert man_on["stages"]["refine"] == "hillclimb2"
    assert man_off["stages"]["refine"] == "none"


def test_future_on_off(runner, scratch_root, fixture_dir):
    """future=1 vs future=0 with past=1: emitted frames follow the window
    size. DOCUMENTED ACTUALS (16-frame fixture): future=1 -> warmup 1 +
    trailing 1 = 14 emitted; future=0 -> warmup 1 + trailing 0 = 15."""
    rc_f, proc_f, man_f, _ = run_arm(runner, scratch_root, "future_on", {
        "past": 1, "future": 1,
    })
    rc_n, proc_n, man_n, _ = run_arm(runner, scratch_root, "future_off", {
        "past": 1, "future": 0,
    })
    assert rc_f == 0, proc_f.stderr
    assert rc_n == 0, proc_n.stderr
    assert man_f["frameRange"]["emitted"] == 14, man_f["frameRange"]
    assert man_n["frameRange"]["emitted"] == 15, man_n["frameRange"]
    assert man_f["window"] == {"past": 1, "future": 1}
    assert man_n["window"] == {"past": 1, "future": 0}


def test_backend_comparison(runner, scratch_root, fixture_dir):
    """backend=null zeroes every presented plane (OutputBackend.cpp:15-29);
    backend=passthrough presents the reconstruction unchanged. Core output
    is unchanged behind the backend; the manifests record the backend swap
    and the null run's npy planes are exactly zero."""
    rc_p, proc_p, man_p, out_p = run_arm(runner, scratch_root, "backend_pt", {
        "past": 1, "backend": "passthrough",
    })
    rc_n, proc_n, man_n, out_n = run_arm(runner, scratch_root, "backend_null",
                                         {
        "past": 1, "backend": "null",
    })
    assert rc_p == 0, proc_p.stderr
    assert rc_n == 0, proc_n.stderr
    assert man_p["stages"]["backend"] == "passthrough"
    assert man_n["stages"]["backend"] == "null"
    files_n = npy_files(out_n)
    # DOCUMENTED ACTUAL: past=1, future=0 on the 16-frame fixture emits 15
    # frames (1 warmup skip, 0 trailing).
    assert len(files_n) == 15
    # one frame suffices per the arm contract; verify its full payload
    shape, values = read_npy(files_n[0])
    assert shape == (3, 48, 64)
    assert all(v == 0.0 for v in values), \
        "null backend produced non-zero planes"
    # passthrough must NOT be all-zero (the fixture has real content), so
    # the two arms are actually distinguishable.
    _, values_p = read_npy(npy_files(out_p)[0])
    assert any(v != 0.0 for v in values_p)


def test_exact_replay_and_tamper(runner, scratch_root, fixture_dir):
    """--replay of an untampered manifest exits 0 (byte-identical re-run);
    a tampered input sha exits 3 (provenance mismatch, main.cpp:979-985)."""
    rc, proc, man, out = run_arm(runner, scratch_root, "replay", {
        "past": 1,
    })
    assert rc == 0, proc.stderr
    manifest_path = out / "run_manifest.json"
    rep = subprocess.run([str(runner), "--replay", str(manifest_path)],
                         capture_output=True, text=True)
    assert rep.returncode == 0, rep.stderr

    tampered = json.loads(manifest_path.read_text(encoding="ascii"))
    sha = tampered["input"]["sha256"]
    tampered["input"]["sha256"] = ("0" if sha[0] != "0" else "1") + sha[1:]
    manifest_path.write_text(json.dumps(tampered), encoding="ascii")
    rep2 = subprocess.run([str(runner), "--replay", str(manifest_path)],
                          capture_output=True, text=True)
    assert rep2.returncode == 3, (rep2.returncode, rep2.stderr)
    assert "provenance mismatch" in rep2.stderr


def test_vfr_clip(runner, scratch_root, fixture_dir):
    """VFR fixture: the runner consumes a non-uniform-PTS clip without
    error and emits exactly one frame per decoded frame.

    What IS asserted, precisely:
      - the generator verifies ON THE FIXTURE (via ffprobe, inside
        --verify-pts) that decoded PTS are strictly increasing with
        non-uniform deltas and that the decoded frame count equals the
        rendered count;
      - the run_manifest records frameRange AGGREGATES ONLY (no per-frame
        ptsUs — src/anvil/runner/Manifest.cpp toJson), so the runner-side
        evidence is: exit 0, frameRange.emitted == the rendered/decoded
        frame count (16), frameRange first/last == 0/15, zero skips.
    """
    vfr_dir = scratch_root / "vfr"
    gen = subprocess.run(
        [sys.executable, str(MAKE_VFR), "--out", str(vfr_dir),
         "--verify-pts"],
        capture_output=True, text=True)
    if gen.returncode == 77:
        pytest.skip(f"ffmpeg/ffprobe absent: make_vfr_fixture exited 77")
    assert gen.returncode == 0, f"make_vfr_fixture failed:\n{gen.stderr}"
    clip = vfr_dir / "vfr_clip.mp4"
    assert clip.is_file()

    out_dir = scratch_root / "arms" / "vfr"
    cfg = dict(BASE_CONFIG)
    cfg["inputPath"] = str(clip)
    cfg["outputDir"] = str(out_dir)
    cfg["coarse"] = "blocksearch"
    cfg["past"] = 0
    cfg["future"] = 0
    cfg_path = scratch_root / "arms" / "vfr.json"
    cfg_path.write_text(json.dumps(cfg, sort_keys=True), encoding="ascii")
    proc = subprocess.run([str(runner), "--config", str(cfg_path)],
                          capture_output=True, text=True)
    assert proc.returncode == 0, proc.stderr
    man = json.loads((out_dir / "run_manifest.json").read_text("ascii"))
    assert man["frameRange"]["emitted"] == 16, man["frameRange"]
    assert man["frameRange"]["first"] == 0
    assert man["frameRange"]["last"] == 15
    assert man["frameRange"]["skippedWarmup"] == 0
    assert man["frameRange"]["skippedTrailing"] == 0
    assert len(npy_files(out_dir)) == 16
