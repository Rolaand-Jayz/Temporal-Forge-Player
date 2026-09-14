"""Generate the frozen empirical viability fixture set.

The fixture is deterministic and self-describing.  Ground truth is authored at
1920x1080, the player input is a lossless FFV1 640x360 video, and R1/R2 differ
only in the frozen pre-decimation Gaussian sigma.  The generated media lives
outside Git; ``fixture_manifest.json`` records hashes and the exact recipe.
"""

from __future__ import annotations

import argparse
import hashlib
import json
import subprocess
import sys
from pathlib import Path
from typing import Any

import cv2
import numpy as np
from PIL import Image, ImageDraw, ImageFont

try:
    from .evs_manifest import (
        FRAME_COUNT,
        FRAME_RATE,
        HR_HEIGHT,
        HR_WIDTH,
        LR_HEIGHT,
        LR_WIDTH,
        REGIMES,
        SEED,
        SEQUENCES,
        STRUCTURAL_CLASSES,
    )
except ImportError:  # direct script execution
    from evs_manifest import (  # type: ignore
        FRAME_COUNT,
        FRAME_RATE,
        HR_HEIGHT,
        HR_WIDTH,
        LR_HEIGHT,
        LR_WIDTH,
        REGIMES,
        SEED,
        SEQUENCES,
        STRUCTURAL_CLASSES,
    )


def sha256_file(path: Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as handle:
        for chunk in iter(lambda: handle.read(1024 * 1024), b""):
            digest.update(chunk)
    return digest.hexdigest()


def sequence_hash(paths: list[Path]) -> str:
    digest = hashlib.sha256()
    for path in paths:
        digest.update(path.name.encode("utf-8"))
        digest.update(path.read_bytes())
    return digest.hexdigest()


def _font(size: int) -> ImageFont.FreeTypeFont | ImageFont.ImageFont:
    candidates = (
        "/usr/share/fonts/truetype/dejavu/DejaVuSansMono.ttf",
        "/usr/share/fonts/TTF/DejaVuSansMono.ttf",
    )
    for candidate in candidates:
        path = Path(candidate)
        if path.is_file():
            return ImageFont.truetype(str(path), size=size)
    return ImageFont.load_default()


def _thin_geometry() -> np.ndarray:
    y, x = np.indices((HR_HEIGHT, HR_WIDTH), dtype=np.float32)
    image = np.empty((HR_HEIGHT, HR_WIDTH, 3), dtype=np.float32)
    image[:] = (0.075, 0.09, 0.12)
    image += 0.025 * np.stack(
        [np.sin(x / 53.0), np.sin(y / 71.0), np.sin((x + y) / 89.0)], axis=-1
    )
    lines = (
        (0.23, 132.0, 0.58),
        (-0.41, 790.0, 0.83),
        (1.00, -180.0, 0.70),
        (-1.00, 1810.0, 0.48),
    )
    colors = (
        (0.94, 0.75, 0.22),
        (0.20, 0.86, 0.95),
        (0.95, 0.33, 0.42),
        (0.66, 0.91, 0.42),
    )
    for (slope, intercept, width), color in zip(lines, colors):
        distance = np.abs(y - (slope * x + intercept)) / np.sqrt(1.0 + slope * slope)
        mask = distance <= width
        image[mask] = color
    cx, cy = 1450.0, 650.0
    radius = np.sqrt((x - cx) ** 2 + (y - cy) ** 2)
    ring = np.abs(radius - 185.0) <= 0.9
    image[ring] = (0.95, 0.95, 0.95)
    return np.clip(image, 0.0, 1.0)


def _fine_text_glyphs() -> np.ndarray:
    image = Image.new("RGB", (HR_WIDTH, HR_HEIGHT), (18, 23, 30))
    draw = ImageDraw.Draw(image)
    draw.rectangle((80, 70, HR_WIDTH - 80, HR_HEIGHT - 85), outline=(105, 135, 165), width=2)
    draw.text((130, 135), "PHASE / DETAIL", fill=(243, 245, 232), font=_font(78))
    draw.text((135, 260), "A7  B4  C2  D9", fill=(235, 191, 84), font=_font(54))
    draw.text((138, 350), "thin strokes survive sampling", fill=(129, 215, 224), font=_font(35))
    for index in range(14):
        x = 145 + index * 112
        draw.line((x, 520, x + 42, 760), fill=(232, 103 + index * 6, 126), width=2)
        draw.line((x + 44, 520, x, 760), fill=(89, 181, 238), width=2)
    draw.rectangle((1230, 500, 1740, 780), outline=(242, 242, 242), width=3)
    draw.text((1280, 575), "g8", fill=(248, 248, 248), font=_font(96))
    draw.text((1280, 690), "pq", fill=(248, 182, 78), font=_font(84))
    return np.asarray(image, dtype=np.float32) / 255.0


def _repeating_texture() -> np.ndarray:
    y, x = np.indices((HR_HEIGHT, HR_WIDTH), dtype=np.float32)
    fine = 0.5 + 0.5 * np.sin(2.0 * np.pi * x / 13.0 + 0.7 * np.sin(y / 43.0))
    diagonal = 0.5 + 0.5 * np.sin(2.0 * np.pi * (x + 1.7 * y) / 29.0)
    checker = ((np.floor(x / 37.0) + np.floor(y / 31.0)) % 2.0)
    image = np.stack(
        [0.16 + 0.44 * fine + 0.14 * checker,
         0.18 + 0.30 * diagonal + 0.12 * (1.0 - checker),
         0.21 + 0.32 * fine + 0.16 * diagonal],
        axis=-1,
    )
    return np.clip(image, 0.0, 1.0)


def _natural_detail() -> np.ndarray:
    rng = np.random.default_rng(SEED + 4)
    coarse = rng.random((135, 240, 3), dtype=np.float32)
    coarse = cv2.resize(coarse, (HR_WIDTH, HR_HEIGHT), interpolation=cv2.INTER_CUBIC)
    fine = rng.normal(0.0, 0.045, (HR_HEIGHT, HR_WIDTH, 1)).astype(np.float32)
    y, x = np.indices((HR_HEIGHT, HR_WIDTH), dtype=np.float32)
    gradient = np.stack(
        [0.12 + 0.23 * (1.0 - y / HR_HEIGHT),
         0.16 + 0.25 * (x / HR_WIDTH),
         0.18 + 0.20 * (1.0 - x / HR_WIDTH)],
        axis=-1,
    )
    base = np.clip(gradient + 0.24 * coarse + fine, 0.0, 1.0)
    # Soft, deterministic forms create a natural-detail scene without using
    # an external corpus or a learned image prior.
    canvas = Image.fromarray(np.rint(base * 255.0).astype(np.uint8), mode="RGB")
    draw = ImageDraw.Draw(canvas, "RGBA")
    for index in range(18):
        px = 90 + ((index * 347) % 1700)
        py = 80 + ((index * 191) % 850)
        rx = 35 + ((index * 29) % 120)
        ry = 24 + ((index * 17) % 90)
        color = (55 + (index * 31) % 150, 75 + (index * 17) % 135,
                 45 + (index * 23) % 150, 75)
        draw.ellipse((px - rx, py - ry, px + rx, py + ry), fill=color)
    return np.asarray(canvas, dtype=np.float32) / 255.0


def make_base(structural: str) -> np.ndarray:
    if structural == "S1":
        return _thin_geometry()
    if structural == "S2":
        return _fine_text_glyphs()
    if structural == "S3":
        return _repeating_texture()
    if structural == "S4":
        return _natural_detail()
    raise ValueError(f"unknown structural class: {structural}")


def shifted(image: np.ndarray, dx: float, dy: float) -> np.ndarray:
    matrix = np.array([[1.0, 0.0, dx], [0.0, 1.0, dy]], dtype=np.float32)
    return cv2.warpAffine(
        image,
        matrix,
        (HR_WIDTH, HR_HEIGHT),
        flags=cv2.INTER_LINEAR,
        borderMode=cv2.BORDER_REFLECT_101,
    )


def write_png(path: Path, image: np.ndarray) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    Image.fromarray(np.rint(np.clip(image, 0.0, 1.0) * 255.0).astype(np.uint8), mode="RGB").save(
        path, format="PNG", compress_level=9
    )


def encode_video(frame_dir: Path, output: Path, log_path: Path) -> None:
    command = [
        "ffmpeg", "-hide_banner", "-loglevel", "error", "-y",
        "-framerate", str(FRAME_RATE), "-start_number", "0",
        "-i", str(frame_dir / "frame_%04d.png"),
        "-frames:v", str(FRAME_COUNT),
        "-c:v", "ffv1", "-level", "3", "-g", "1", "-pix_fmt", "yuv444p",
        str(output),
    ]
    completed = subprocess.run(command, text=True, stdout=subprocess.PIPE, stderr=subprocess.PIPE, check=False)
    log_path.write_text(
        json.dumps({"command": command, "returncode": completed.returncode, "stderr": completed.stderr}, indent=2) + "\n",
        encoding="utf-8",
    )
    if completed.returncode != 0:
        raise RuntimeError(f"ffmpeg fixture encode failed for {output}: {completed.stderr.strip()}")


def generate(root: Path) -> dict[str, Any]:
    if root.exists() and any(root.iterdir()):
        raise RuntimeError(f"refusing to overwrite non-empty fixture root: {root}")
    root.mkdir(parents=True, exist_ok=False)
    gt_root = root / "gt"
    lr_root = root / "lr"
    media_root = root / "media"
    logs_root = root / "encode_logs"
    media_root.mkdir(parents=True, exist_ok=True)
    logs_root.mkdir(parents=True, exist_ok=True)
    media: dict[str, Any] = {}
    bases = {name: make_base(name) for name in STRUCTURAL_CLASSES}

    for structural in STRUCTURAL_CLASSES:
        base = bases[structural]
        for sequence, sequence_spec in SEQUENCES.items():
            gt_paths: list[Path] = []
            dx0, dy0 = sequence_spec["phase_origin_hr_pixels"]
            vx, vy = sequence_spec["velocity_hr_pixels_per_frame"]
            gt_dir = gt_root / structural / sequence
            for frame_index in range(FRAME_COUNT):
                frame = shifted(base, dx0 + frame_index * vx, dy0 + frame_index * vy)
                path = gt_dir / f"frame_{frame_index:04d}.png"
                write_png(path, frame)
                gt_paths.append(path)
            gt_key = f"{structural}_{sequence.lower()}"
            media[gt_key] = {
                "structural_class": structural,
                "sequence": sequence,
                "frame_paths": [str(path.relative_to(root)) for path in gt_paths],
                "combined_sha256": sequence_hash(gt_paths),
            }

            for regime, regime_spec in REGIMES.items():
                lr_paths: list[Path] = []
                lr_dir = lr_root / structural / regime / sequence
                sigma = float(regime_spec["prefilter"]["sigma"])
                for frame_index, gt_path in enumerate(gt_paths):
                    frame = np.asarray(Image.open(gt_path).convert("RGB"), dtype=np.float32) / 255.0
                    blurred = cv2.GaussianBlur(
                        frame,
                        (0, 0),
                        sigmaX=sigma,
                        sigmaY=sigma,
                        borderType=cv2.BORDER_REFLECT_101,
                    )
                    low = cv2.resize(blurred, (LR_WIDTH, LR_HEIGHT), interpolation=cv2.INTER_AREA)
                    path = lr_dir / f"frame_{frame_index:04d}.png"
                    write_png(path, low)
                    lr_paths.append(path)
                media_path = media_root / f"{structural}_{regime}_{sequence}.mkv"
                encode_video(lr_dir, media_path, logs_root / f"{structural}_{regime}_{sequence}.json")
                key = f"{structural}_{regime}_{sequence.lower()}"
                media[key] = {
                    "structural_class": structural,
                    "regime": regime,
                    "sequence": sequence,
                    "gt_sequence_key": gt_key,
                    "lr_frame_paths": [str(path.relative_to(root)) for path in lr_paths],
                    "lr_combined_sha256": sequence_hash(lr_paths),
                    "input_media_relative": str(media_path.relative_to(root)),
                    "input_media_sha256": sha256_file(media_path),
                    "input_media_dimensions": [LR_WIDTH, LR_HEIGHT],
                    "frame_count": FRAME_COUNT,
                }

    document = {
        "schema": "temporal_forge.empirical_viability.fixture.v1",
        "campaign_id": "evs-20260914",
        "seed": SEED,
        "generator": "benchmarks/empirical/evs_fixture.py",
        "generator_contract": {
            "warp": "cv2.warpAffine INTER_LINEAR BORDER_REFLECT_101",
            "prefilter": "cv2.GaussianBlur BORDER_REFLECT_101",
            "decimation": "cv2.resize INTER_AREA",
            "encoding": "ffmpeg FFV1 level 3 g=1 yuv444p",
        },
        "dimensions": {
            "ground_truth": [HR_WIDTH, HR_HEIGHT],
            "input": [LR_WIDTH, LR_HEIGHT],
            "output": [HR_WIDTH, HR_HEIGHT],
        },
        "frame_rate": FRAME_RATE,
        "frame_count": FRAME_COUNT,
        "classes": STRUCTURAL_CLASSES,
        "regimes": REGIMES,
        "sequences": SEQUENCES,
        "media": media,
    }
    manifest_path = root / "fixture_manifest.json"
    manifest_path.write_text(json.dumps(document, indent=2, sort_keys=True) + "\n", encoding="utf-8")
    return document


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--output-root", type=Path, required=True)
    args = parser.parse_args()
    document = generate(args.output_root)
    media_count = sum(1 for key in document["media"] if "regime" in document["media"][key])
    print(f"generated {media_count} input videos and {len(document['media']) - media_count} GT sequences")
    print(args.output_root / "fixture_manifest.json")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
