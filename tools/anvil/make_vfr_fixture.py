#!/usr/bin/env python3
# ANVIL variable-frame-rate (VFR) fixture generator.
#
# Renders the SAME moving-square content as make_fixtures.py (rendering
# functions copied verbatim, no cross-import fragility) but emits an mp4
# with NON-uniform PTS deltas: the first N_A frames are stamped at 10 fps
# and the remaining N_B frames at 30 fps, in one ffmpeg invocation (two
# rawvideo inputs at different -r joined with the concat filter) and
# -fps_mode passthrough so the input timestamps survive encoding.
#
# Property under test (tests/anvil/test_anvil_arms_e2e.py::test_vfr_clip):
# the anvil_runner consumes this clip without error, emits exactly one
# output frame per decoded frame, and the manifest's frameRange accounting
# matches. The run manifest records frameRange aggregates only (no
# per-frame ptsUs — src/anvil/runner/Manifest.cpp toJson), so the
# strictly-increasing / non-uniform PTS property is verified HERE, on the
# fixture itself, via ffprobe when ffprobe is available (and the runner
# side asserts the emitted-count evidence).
#
# Exit codes: 0 written; 77 ffmpeg (or ffprobe, when --verify-pts) absent;
# 1 encode/verify failure. Prints every file written.
#
# CLI: make_vfr_fixture.py --out DIR [--frames-a N] [--frames-b N]
#                          [--size WxH] [--verify-pts]
import argparse
import json
import os
import shutil
import subprocess
import sys

# Same analytic render constants as make_fixtures.py (copied deliberately).
GRID_STEP = 8
SQUARE = 12
VX = 1.0
VY = 0.5
X0 = 6.0
Y0 = 4.0
BG_A = 0.2
BG_B = 0.8
SQ_VAL = 1.0


def square_pos(t, width, height):
    x = X0 + VX * t
    y = Y0 + VY * t
    if x < 0 or y < 0 or x + SQUARE > width or y + SQUARE > height:
        raise ValueError("square leaves frame at t=%d (increase --size)" % t)
    return x, y


def bg_value(px, py):
    return BG_A if ((px // GRID_STEP) + (py // GRID_STEP)) % 2 == 0 else BG_B


def render_frame(t, width, height):
    """Row-major (3,H,W) f32 as a flat list of floats (area-weighted
    square coverage over the checkerboard — identical to make_fixtures)."""
    sx, sy = square_pos(t, width, height)
    out = [0.0] * (3 * height * width)
    for py in range(height):
        oy = min(py + 1.0, sy + SQUARE) - max(float(py), sy)
        cov_y = oy if oy > 0.0 else 0.0
        for px in range(width):
            if cov_y > 0.0:
                ox = min(px + 1.0, sx + SQUARE) - max(float(px), sx)
                cov = ox * cov_y if ox > 0.0 else 0.0
            else:
                cov = 0.0
            v = (1.0 - cov) * bg_value(px, py) + cov * SQ_VAL
            i = (py * width + px) * 3
            out[i] = v
            out[i + 1] = v
            out[i + 2] = v
    return out


def render_rgb24(t, width, height):
    frame = render_frame(t, width, height)
    return bytes(int(round(min(255.0, max(0.0, v * 255.0)))) & 0xFF for v in frame)


def _encode_segment(out_path, first_frame, count, fps, width, height, tmp_dir):
    """Encode `count` frames starting at global frame `first_frame` as a CFR
    segment mp4 at `fps`. Frames are staged in a rawvideo temp file because
    two `-i -` stdin inputs cannot coexist in one invocation."""
    raw_path = os.path.join(tmp_dir, "seg_raw.rgb")
    with open(raw_path, "wb") as f:
        for t in range(first_frame, first_frame + count):
            f.write(render_rgb24(t, width, height))
    cmd = [
        "ffmpeg", "-y", "-loglevel", "error",
        "-f", "rawvideo", "-pix_fmt", "rgb24",
        "-s", "%dx%d" % (width, height), "-r", str(fps), "-i", raw_path,
        "-c:v", "libx264", "-pix_fmt", "yuv420p",
        "-crf", "28", "-preset", "veryfast",
        out_path,
    ]
    proc = subprocess.run(cmd)
    os.unlink(raw_path)
    return proc.returncode == 0


def encode_vfr(out_path, frames_a, frames_b, width, height, tmp_dir):
    """Two CFR segments (frames_a @ 10 fps, frames_b @ 30 fps) joined with
    the concat DEMUXER and -c copy. Each segment keeps its own timestamps;
    the demuxer offsets segment B to continue after segment A's duration,
    so decoded PTS stay strictly increasing while per-frame deltas change
    mid-clip (the VFR property). Returns True on success."""
    seg_a = os.path.join(tmp_dir, "seg_a.mp4")
    seg_b = os.path.join(tmp_dir, "seg_b.mp4")
    if not _encode_segment(seg_a, 0, frames_a, 10, width, height, tmp_dir):
        return False
    if not _encode_segment(seg_b, frames_a, frames_b, 30, width, height,
                           tmp_dir):
        return False
    list_path = os.path.join(tmp_dir, "concat.txt")
    with open(list_path, "w", encoding="ascii") as f:
        f.write("file '%s'\nfile '%s'\n" % (seg_a, seg_b))
    cmd = [
        "ffmpeg", "-y", "-loglevel", "error",
        "-f", "concat", "-safe", "0", "-i", list_path,
        "-c", "copy", "-movflags", "+faststart",
        out_path,
    ]
    proc = subprocess.run(cmd)
    os.unlink(seg_a)
    os.unlink(seg_b)
    os.unlink(list_path)
    return proc.returncode == 0


def probe_pts(path):
    """Decoded frame count and the list of PTS (seconds, float) via ffprobe,
    in presentation order. Returns (count, pts_list) or None without
    ffprobe. Raises RuntimeError on ffprobe failure."""
    if shutil.which("ffprobe") is None:
        return None
    cmd = [
        "ffprobe", "-loglevel", "error", "-select_streams", "v:0",
        "-show_entries", "frame=pts_time",
        "-of", "json", path,
    ]
    proc = subprocess.run(cmd, capture_output=True, text=True)
    if proc.returncode != 0:
        raise RuntimeError("ffprobe failed: " + proc.stderr.strip())
    frames = json.loads(proc.stdout).get("frames", [])
    pts = []
    for f in frames:
        ts = f.get("pts_time")
        if ts is not None:
            pts.append(float(ts))
    return len(frames), pts


def main():
    ap = argparse.ArgumentParser(description="ANVIL VFR fixture generator")
    ap.add_argument("--out", required=True, help="output directory")
    ap.add_argument("--frames-a", type=int, default=8,
                    help="frames stamped at 10 fps (default 8)")
    ap.add_argument("--frames-b", type=int, default=8,
                    help="frames stamped at 30 fps (default 8)")
    ap.add_argument("--size", default="64x48", help="WxH, both multiples of 8")
    ap.add_argument("--verify-pts", action="store_true",
                    help="after encoding, verify via ffprobe that decoded "
                         "PTS are strictly increasing with non-uniform "
                         "deltas (requires ffprobe; without it the check "
                         "is skipped, not failed)")
    args = ap.parse_args()

    if "x" not in args.size:
        ap.error("--size must be WxH")
    w_str, h_str = args.size.split("x", 1)
    width, height = int(w_str), int(h_str)
    if width <= 0 or height <= 0 or width % GRID_STEP or height % GRID_STEP:
        ap.error("--size must be positive multiples of %d" % GRID_STEP)
    if args.frames_a <= 0 or args.frames_b <= 0:
        ap.error("--frames-a/--frames-b must be positive")

    if shutil.which("ffmpeg") is None:
        print("make_vfr_fixture: ffmpeg not found", file=sys.stderr)
        return 77

    os.makedirs(args.out, exist_ok=True)
    clip_path = os.path.join(args.out, "vfr_clip.mp4")
    total = args.frames_a + args.frames_b
    square_pos(total - 1, width, height)  # fail before encoding if out of range

    if not encode_vfr(clip_path, args.frames_a, args.frames_b, width, height,
                      args.out):
        print("make_vfr_fixture: ffmpeg encode failed", file=sys.stderr)
        return 1

    written = ["%s (%d frames: %d @ 10fps + %d @ 30fps, non-uniform PTS)"
               % (clip_path, total, args.frames_a, args.frames_b)]

    pts_info = probe_pts(clip_path)
    if pts_info is not None:
        count, pts = pts_info
        written.append("ffprobe: %d decoded frames" % count)
        if len(pts) >= 3:
            deltas = [round(b - a, 6) for a, b in zip(pts, pts[1:])]
            written.append("ffprobe pts deltas (s): %s" % deltas)
            non_uniform = len(set(deltas)) > 1
            strictly_increasing = all(d > 0 for d in deltas)
            written.append("ffprobe: strictly increasing pts = %s; "
                           "non-uniform deltas = %s"
                           % (strictly_increasing, non_uniform))
            if args.verify_pts and not (strictly_increasing and non_uniform
                                        and count == total):
                print("\n".join(written), file=sys.stderr)
                print("make_vfr_fixture: PTS verification FAILED",
                      file=sys.stderr)
                return 1
        elif args.verify_pts:
            print("\n".join(written), file=sys.stderr)
            print("make_vfr_fixture: ffprobe returned too few frames to "
                  "verify", file=sys.stderr)
            return 1
    elif args.verify_pts:
        print("make_vfr_fixture: ffprobe not found; PTS verification "
              "skipped (exit 77)", file=sys.stderr)
        return 77

    print("wrote:")
    for line in written:
        print("  " + line)
    return 0


if __name__ == "__main__":
    sys.exit(main())
