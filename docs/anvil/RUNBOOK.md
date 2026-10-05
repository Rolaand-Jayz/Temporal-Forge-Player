# ANVIL successor — build & run book

Successor offline reconstruction harness. See [`RECONCILIATION_20261005.md`](RECONCILIATION_20261005.md)
for architecture decisions and [`BUILD_READY_CHECKLIST.md`](BUILD_READY_CHECKLIST.md) for the
qualification gate. The FSR-era player in this repository is historical; ANVIL does not require
any FSR asset, the Qt UI, or a display server.

## Build (clean clone)

System requirements (Arch package names; CI installs the same list):

```
cmake ninja pkgconf base-devel
ffmpeg            (dev headers: libavformat/libavcodec/libavutil/libswscale)
vulkan-headers vulkan-icd-loader   (header find only; the v1 successor core is CPU-only)
glslang           (historical FSR-era shader targets; not used by ANVIL targets)
qt6-base qt6-declarative qt6-shadertools   (historical player targets; not used by ANVIL targets)
```

No git submodules. No downloaded weights. No LFS needed for ANVIL (LFS carries only
historical review images).

```
git clone https://github.com/Rolaand-Jayz/Temporal-Forge-Player
cd Temporal-Forge-Player
git checkout anvil/successor-build
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release
ninja -C build anvil_core anvil_runner        # successor targets only
ninja -C build                                 # or everything, incl. the historical player
ctest --test-dir build -R anvil_ --output-on-failure
```

ANVIL targets compile an explicit FSR-free, Qt-free source set (`src/anvil/`). They never
link the FSR-era library.

## Offline run

```
build/anvil_runner --config pipeline.json
```

`pipeline.json` fields (canonical order; all fields required except where a default is
noted): `inputPath`, `firstFrame`, `lastFrame` (-1 = to end), `past`, `future`,
`sideInfoNormalizer` (`passthrough` | `densify_blocks`), `coarse` (`codec` | `blocksearch` |
`zero` | `oracle`), `refine` (`none` | `hillclimb2`), `visibility` (`unity` | `photo_fwbw` |
`oracle`), `geometry` (`unknown` | `global_xform` | `oracle`), `reconstruct` (`center` |
`weighted_blend`), `backend` (`null` | `passthrough`), `oracleSidecar` ("" = none),
`deterministic` (true), `seed`, `captureDir`, `captureStages`, `outputDir`,
`transferOverride` ("" | bt709|srgb|linear|pq|hlg — required when the input's transfer
metadata is unspecified), `dropNeighbors` ("" | comma-separated window offsets).

Deterministic fixtures for self-verification:

```
python3 tools/anvil/make_fixtures.py --out /tmp/anvil_fixture
```

writes a 16-frame 64×48 synthetic sequence (known motion), HR ground truth, an oracle
sidecar (exact flow/visibility/geometry), and `clip.mp4` (H.264). Example arms:

- single-frame control: `past=0, future=0, reconstruct="center"`
- codec-prior reconstruction (H.264): `coarse="codec", refine="hillclimb2", past=1`
- oracle-flow arm: `coarse="oracle", oracleSidecar="<fixture>/oracle_index.json", past=1`
- ablation: `past=1, future=1, dropNeighbors="-1"` (drop the past neighbor)

Outputs: `out_XXXXXXXX.npy` (3,H,W float32 RGB, declared display-referred space) plus
`run_manifest.json` (provenance: git/binary/config/input hashes, replay key, stage
selections, oracle arms used, skipped frames) and `timings.json` (per-stage host ms).

## Exact replay

```
build/anvil_runner --replay <outputDir>/run_manifest.json
```

re-hashes the binary, input, and config; any provenance mismatch exits 3. Otherwise it
re-runs and byte-compares every output frame; exit 0 = identical, exit 1 lists differing
frames.

## Test arms (isolating failures)

Every arm is a config, not a fork; artifacts and manifests make them auditable:

| Arm | Config |
|---|---|
| oracle flow + estimated confidence | `coarse="oracle"`, `visibility="photo_fwbw"` |
| estimated flow + oracle confidence | `coarse="blocksearch"`, `visibility="oracle"` |
| oracle sampling geometry | `geometry="oracle"` |
| estimated sampling geometry | `geometry="global_xform"` |
| single-frame control | `reconstruct="center"` or `past=0,future=0` |
| selected-neighbor ablation | `dropNeighbors="<offsets>"` |
| codec prior on/off | `coarse="codec"` vs `coarse="blocksearch"` |
| refinement on/off | `refine="hillclimb2"` vs `refine="none"` |
| future frames on/off | `future=N` vs `future=0` |
| backend comparison | `backend=` swap (core output unchanged) |
| exact replay | `--replay` |

## Codec side-information truth (measured, FFmpeg n9.0.2 on RDNA3/Mesa 26.2.4)

- H.264 software decode exports block motion vectors (`AV_CODEC_FLAG2_EXPORT_MVS`).
- HEVC and AV1 do **not** export motion vectors on this stack — marked
  `UnsupportedCodec`, never fabricated; use estimator/oracle arms instead.
- No hardware-decode path (VAAPI/Vulkan) exports motion vectors — marked
  `HwDecodeNoExport`; requesting codec MVs forces software decode.

Historical FSR-era documentation: [`../docs/closure/`](../closure/) and
[`../docs/current/STATE.md`](../current/STATE.md). ANVIL working documents:
[`README.md`](README.md).
