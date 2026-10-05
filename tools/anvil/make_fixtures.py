#!/usr/bin/env python3
# ANVIL deterministic fixture generator (docs/anvil/RECONCILIATION_20261005.md
# 3.7: "Synthetic fixtures are build-time generated ... with HR ground truth
# attached").
#
# Renders a 16-frame 64x48 sequence:
#   - static checkerboard background (8 px cells, values 0.2 / 0.8)
#   - a 12x12 square moving +1.0 px/frame in x, +0.5 px/frame in y, with FLOAT
#     positions rendered by exact area-weighted pixel coverage (subpixel truth
#     is analytic, not resampled — no invented phase, reconciliation 3.3(8))
# and writes into --out DIR:
#   frames/obs_XXXXX.npy        (3,H,W) f32  — observations
#   ground_truth/gt_XXXXX.npy   (3,H,W) f32  — identical content (HR truth, 1:1)
#   oracle_index.json           anvil.oracle.v1 sidecar index
#   oracle payload .npy files under flow/ visibility/ geometry/ ground_truth/
#     flow(t, -1):        (gridH*gridW*2,) f32 (dy,dx) per block, 8 px grid
#                         (64x48 => gridWidth 8, gridHeight 6). Blocks covered
#                         by the square at the TARGET frame carry the
#                         destination->reference displacement (dy,dx) =
#                         (-0.5,-1.0) (past neighbor is where the square WAS);
#                         static background blocks are (0,0).
#     visibility(t, -1):  (H,W) f32 all-ones (nothing occludes: clean render)
#     geometry(t):        (6,) f32 identity affine (static camera)
#     ground_truth(t):    records pointing at the identical gt payloads,
#                         meta = sRGB full-range declaration (color_meta in
#                         the record; AVColor values: matrix=0 (GBR),
#                         primaries=1 (BT.709), transfer=13 (sRGB), range=2)
#   clip.mp4                    same frames piped as rawvideo rgb24 to ffmpeg
#                               libx264 (yuv420p, CRF 28, preset veryfast,
#                               fixed gop = frame count). If ffmpeg is absent
#                               the clip step is skipped and the script exits
#                               77 (graceful skip) AFTER writing all files.
#
# Determinism: NO RNG is used anywhere — every pixel is an analytic function
# of (frame, x, y). Fixed seed noted as 0 for manifest compatibility but
# unused. Pure stdlib only (struct/array); no numpy dependency — the .npy
# writer is byte-identical to anvil::writeNpy (Capture.cpp).
#
# CLI: make_fixtures.py --out DIR [--frames N] [--size WxH]
import argparse
import json
import os
import shutil
import subprocess
import sys

SEED = 0  # reserved; no RNG consumer exists in this generator

GRID_STEP = 8
SQUARE = 12
VX = 1.0  # px/frame in x
VY = 0.5  # px/frame in y
X0 = 6.0  # square left edge at frame 0
Y0 = 4.0  # square top edge at frame 0
BG_A = 0.2
BG_B = 0.8
SQ_VAL = 1.0


def square_pos(t, width, height):
    """Float square top-left at frame t; refuses to leave the frame."""
    x = X0 + VX * t
    y = Y0 + VY * t
    if x < 0 or y < 0 or x + SQUARE > width or y + SQUARE > height:
        raise ValueError("square leaves frame at t=%d (increase --size)" % t)
    return x, y


def bg_value(px, py):
    return BG_A if ((px // GRID_STEP) + (py // GRID_STEP)) % 2 == 0 else BG_B


def render_frame(t, width, height):
    """Row-major (3,H,W) f32 as a flat list of floats. Area-weighted coverage:
    a = overlap(pixel, square); value = (1-a)*bg + a*SQ_VAL."""
    sx, sy = square_pos(t, width, height)
    out = [0.0] * (3 * height * width)
    for py in range(height):
        oy = min(py + 1.0, sy + SQUARE) - max(float(py), sy)
        if oy <= 0.0:
            cov_y = 0.0
        else:
            cov_y = oy
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
    """Same frame as RGB24 bytes for the mp4 encode (round-to-nearest)."""
    frame = render_frame(t, width, height)
    return bytes(int(round(min(255.0, max(0.0, v * 255.0)))) & 0xFF for v in frame)


# ---------------------------------------------------------------------------
# .npy v1.0 writer — byte-compatible with anvil::writeNpy (Capture.cpp):
# 6-byte magic, version 1.0, u16 LE header length, dict padded to 64 bytes.
# ---------------------------------------------------------------------------
def npy_bytes(shape, f32_values):
    import struct
    shape_str = "(" + ", ".join(str(s) for s in shape) + ",)"
    d = ("{'descr': '<f4', 'fortran_order': False, 'shape': %s, }" % shape_str)
    base = 6 + 2 + 2 + len(d) + 1
    d += " " * ((64 - (base % 64)) % 64) + "\n"
    header = b"\x93NUMPY" + bytes((1, 0)) + len(d).to_bytes(2, "little") + d.encode("ascii")
    try:
        import array
        a = array.array("f", f32_values)  # 4-byte IEEE f32; numpy NOT required
        if sys.byteorder == "big":
            a.byteswap()
        return header + a.tobytes()
    except ImportError:
        return header + struct.pack("<%df" % len(f32_values), *f32_values)


def flow_payload(t, width, height, window_offset=-1):
    """(dy,dx) per block at the target frame's 8px grid; dest->reference
    convention. window_offset -1: reference is the past frame, displaced by
    -velocity; +1: reference is the future frame, displaced by +velocity."""
    grid_w = width // GRID_STEP
    grid_h = height // GRID_STEP
    sx, sy = square_pos(t, width, height)
    sign = -1.0 if window_offset < 0 else 1.0
    out = [0.0] * (grid_h * grid_w * 2)
    for gy in range(grid_h):
        for gx in range(grid_w):
            bx, by = gx * GRID_STEP, gy * GRID_STEP
            # Axis-aligned interval overlap on both axes.
            overlap = (bx < sx + SQUARE and sx < bx + GRID_STEP and
                       by < sy + SQUARE and sy < by + GRID_STEP)
            if overlap:
                i = (gy * grid_w + gx) * 2
                out[i] = sign * VY  # dy: reference position - target position
                out[i + 1] = sign * VX  # dx
    return out


def write(path, shape, values):
    os.makedirs(os.path.dirname(path), exist_ok=True)
    with open(path, "wb") as f:
        f.write(npy_bytes(shape, values))


def have_ffmpeg():
    return shutil.which("ffmpeg") is not None


def encode_clip(out_dir, frames, width, height, fps=16):
    """Pipe rawvideo rgb24 into libx264. Returns True on success."""
    cmd = [
        "ffmpeg", "-y", "-loglevel", "error",
        "-f", "rawvideo", "-pix_fmt", "rgb24",
        "-s", "%dx%d" % (width, height), "-r", str(fps), "-i", "-",
        "-c:v", "libx264", "-pix_fmt", "yuv420p",
        "-crf", "28", "-preset", "veryfast", "-g", str(frames),
        os.path.join(out_dir, "clip.mp4"),
    ]
    proc = subprocess.Popen(cmd, stdin=subprocess.PIPE)
    try:
        for t in range(frames):
            proc.stdin.write(render_rgb24(t, width, height))
        proc.stdin.close()
        return proc.wait() == 0
    except BrokenPipeError:
        proc.wait()
        return False


def main():
    ap = argparse.ArgumentParser(description="ANVIL deterministic fixture generator")
    ap.add_argument("--out", required=True)
    ap.add_argument("--frames", type=int, default=16)
    ap.add_argument("--size", default="64x48", help="WxH, both multiples of 8")
    args = ap.parse_args()

    if "x" not in args.size:
        ap.error("--size must be WxH")
    w_str, h_str = args.size.split("x", 1)
    width, height = int(w_str), int(h_str)
    if width <= 0 or height <= 0 or width % GRID_STEP or height % GRID_STEP:
        ap.error("--size must be positive multiples of %d" % GRID_STEP)
    if args.frames <= 0:
        ap.error("--frames must be positive")

    out = args.out
    os.makedirs(out, exist_ok=True)
    written = []

    grid_w, grid_h = width // GRID_STEP, height // GRID_STEP
    records = []

    for t in range(args.frames):
        frame = render_frame(t, width, height)
        obs_rel = "frames/obs_%05d.npy" % t
        gt_rel = "ground_truth/gt_%05d.npy" % t
        write(os.path.join(out, obs_rel), (3, height, width), frame)
        write(os.path.join(out, gt_rel), (3, height, width), frame)
        written += [obs_rel, gt_rel]

        # Oracle flow for the past neighbor (windowOffset -1). Frame 0 has no
        # past frame, so flow records start at target 1.
        if t >= 1:
            flow_rel = "flow/flow_t%05d_offm1.npy" % t
            write(os.path.join(out, flow_rel), (grid_h * grid_w * 2,),
                  flow_payload(t, width, height, -1))
            records.append({
                "kind": "flow", "targetIndex": t, "windowOffset": -1,
                "file": flow_rel,
                "gridStep": GRID_STEP, "gridWidth": grid_w, "gridHeight": grid_h,
            })

        # Oracle flow for the future neighbor (windowOffset +1). The last
        # frame has no future frame.
        if t < args.frames - 1:
            flow_rel = "flow/flow_t%05d_offp1.npy" % t
            write(os.path.join(out, flow_rel), (grid_h * grid_w * 2,),
                  flow_payload(t, width, height, +1))
            records.append({
                "kind": "flow", "targetIndex": t, "windowOffset": 1,
                "file": flow_rel,
                "gridStep": GRID_STEP, "gridWidth": grid_w, "gridHeight": grid_h,
            })

        vis_rel = "visibility/vis_t%05d_offm1.npy" % t
        write(os.path.join(out, vis_rel), (height, width),
              [1.0] * (height * width))
        records.append({
            "kind": "visibility", "targetIndex": t, "windowOffset": -1,
            "file": vis_rel,
        })

        # Visibility for the future neighbor: all ones (clean render).
        vis_rel = "visibility/vis_t%05d_offp1.npy" % t
        write(os.path.join(out, vis_rel), (height, width),
              [1.0] * (height * width))
        records.append({
            "kind": "visibility", "targetIndex": t, "windowOffset": 1,
            "file": vis_rel,
        })

        geo_rel = "geometry/geo_t%05d.npy" % t
        write(os.path.join(out, geo_rel), (6,), [1.0, 0.0, 0.0, 0.0, 1.0, 0.0])
        records.append({
            "kind": "geometry", "targetIndex": t, "windowOffset": 0,
            "file": geo_rel,
        })

        # ground_truth records point at the identical HR payloads so
        # loadGroundTruth works from the same sidecar (1:1 fixture).
        records.append({
            "kind": "ground_truth", "targetIndex": t, "windowOffset": 0,
            "file": gt_rel,
            "meta": {
                "matrix": 0,        # AV_COLORSPACE_GBR (RGB, no conversion)
                "primaries": 1,     # BT.709
                "transfer": 13,     # sRGB (IEC 61966-2-1)
                "range": 2,         # full
                "chromaLocation": 0,
                "bitDepth": 8,
            },
        })

    index_path = os.path.join(out, "oracle_index.json")
    with open(index_path, "w", encoding="ascii") as f:
        json.dump(
            {"schema": "anvil.oracle.v1", "seed": SEED, "records": records},
            f, indent=2, sort_keys=True,
        )
        f.write("\n")
    written.append("oracle_index.json")

    manifest = ["fixtures written to %s" % os.path.abspath(out)]
    manifest += ["  " + p for p in written]
    manifest.append("  (%d records in oracle_index.json)" % len(records))
    clip_ok = have_ffmpeg() and encode_clip(out, args.frames, width, height)
    if clip_ok:
        manifest.append("  clip.mp4 (libx264 yuv420p crf28 veryfast gop=%d)" % args.frames)
        print("\n".join(manifest))
        return 0
    if not have_ffmpeg():
        manifest.append("  clip.mp4 SKIPPED: ffmpeg not found (exit 77)")
        print("\n".join(manifest), file=sys.stderr)
        return 77
    manifest.append("  clip.mp4 FAILED: ffmpeg returned an error")
    print("\n".join(manifest), file=sys.stderr)
    return 1


if __name__ == "__main__":
    sys.exit(main())
