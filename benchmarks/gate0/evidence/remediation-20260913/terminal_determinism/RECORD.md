# Remediation Task B — terminal-tier determinism verification

**Date:** 2026-09-13 · **Scope:** Gate-0 remediation Task B · **Status:** complete

## Question

Does the Gate-0 determinism envelope remain valid at the reconstruction tier
representative of the terminal campaign (2× per dimension, canonical
1920×1080 → 3840×2160), and can terminal-tier A/B differences be distinguished
reliably from run-to-run nondeterminism under the Gate-0 comparison policy?

## Terminal configuration (the actual intended terminal path)

| element | value |
|---|---|
| input | `benchmarks/video_corpus/clips/bbb_branches_1920x1080_medium_crf23.mp4` (SHA-256 `c0997f01e70f56591588dd1aeb73256f5a55395428749936c3cc075f268e31ae`) |
| decode | h264 software (VAAPI init fails in this environment; recorded environment fact) |
| reconstruction | model 1920×1080 → output 3840×2160, **native INT8 `performance_2160` 14-pass graph**, backend `native_int8`, effective scale 2.0 |
| forcing env | `TFORGE_FSR4_FORCE_VIEWPORT=3840x2160`, `TFORGE_FSR4_FORCE_SCALE=2.0` |
| everything else | identical to the original Gate-0 determinism protocol (default control configuration, 2 warmup frames, 6 dumped outputs per run, 3 runs, identical environment) |

**Fixture choice (documented per mandate):** the exact terminal-campaign scene
fixtures are not present in this checkout; the capture plan names four scenes
(`tos_daylight`, `tos_debris`, `sintel_rooftop`, `sintel_cave`) whose media is
not local. The used clip is the closest existing canonical equivalent: a
1920×1080 16:9 h264 yuv420p campaign benchmark clip produced by the
repository's own `prepare_corpus.sh`, exercising the same intended terminal
reconstruction path (native INT8 fixed-target Performance graph at 2× per
dimension). No path was invented, simplified, or downscaled.

## Why the default configuration is NOT the terminal path (recorded finding)

The player's default selected scale is 1.5×, which at a 4K viewport produces a
2560×1440 model. Native fixed-shape INT8 packs apply only to pass inputs of
height ≤ 1080 (`nativeInt8UltraPerformanceTarget`, `FsrTargetMath.hpp`), so the
default configuration at this tier is native-ineligible and falls to the
generic graph — which requires the v4.1 weight blob (`quality.bin`), absent on
this host, yielding an EASU-only fallback. The intended terminal regime is the
Performance 2.0× preset: model = source 1920×1080 → 3840×2160
(`performance_2160`, provisioned, 14/14 passes). Both facts are visible in the
retained probe logs and are the reason the two forcing variables are part of
the recorded terminal configuration.

## Result

**Verdict: `byte_identical`.** All 6 dumped 3840×2160 outputs in all 3 runs
have identical SHA-256; pairwise max MAE = 0.0 (0–255 scale).

| pair | compared | byte-identical | max MAE |
|---|---|---|---|
| det-00 vs det-01 | 6 | 6 | 0.0 |
| det-01 vs det-02 | 6 | 6 | 0.0 |

## Required determination

> Can terminal-tier A/B differences be distinguished reliably from run-to-run
> nondeterminism using the Gate-0 comparison policy? **YES.**

The measured terminal-tier run-to-run floor is exactly zero (byte-identical).
The existing 0.05 MAE comparison threshold is therefore **conservative, not
merely consistent**: it exceeds the measured floor by an effectively unbounded
margin, and it also remains the floor for the 360p→1080p tier where measured
run noise is ~1e-4. No tier-specific tightening is warranted by evidence:
a tighter floor would only reduce protection against the (already tiny)
lower-tier noise while adding no discriminating power at this tier, where any
nonzero delta is immediately attributable to the experimental control.

## Tooling event (recorded, not a player defect)

The first terminal-tier attempt truncated one 4K dump (`run_00` frame 0005):
the Gate-0 launcher's early-termination raced the ~25 MB dump flush at 4K.
The payload validator failed closed (`read_ppm` short-payload error), the
launch was discarded, and the launcher was minimally fixed to require a
size-stable dump inventory across two polls before terminating
(`benchmarks/gate0/player_run.py`). No production behavior changed. The three
retained runs above all used the fixed launcher.

## Provenance

- Report: `determinism_report_terminal_tier.json` (per-run dump SHA-256
  tables; run IDs carry the probe's internal `FFW-T0-2-det-NN` prefix — a
  tooling label, not a task assignment).
- Identity manifest: `experiment_identity.json` (runtime-trace cross-check
  passed: binary/git/config provenance).
- Dump payloads (18 × ~25 MB) and player logs retained in
  `/tmp/remediation/t0b-terminal/` (hashes recorded in the report);
  regenerable via the command below.
- Reproduction:
  `python3` invocation of `benchmarks.gate0.determinism_probe.run_determinism`
  with `runs=3, frames=6, warmup=2, timeout_s=300,
  env_overrides={'TFORGE_FSR4_FORCE_VIEWPORT': '3840x2160',
  'TFORGE_FSR4_FORCE_SCALE': '2.0'}` against the input above.

## Uncertainties

- Envelope measured on one terminal-tier scene (Big Buck Bunny branches clip);
  other terminal-tier scenes inherit the comparison policy but were not
  individually measured.
- The generic-graph terminal tier remains unverifiable on this host (missing
  v4.1 blob); the determination covers the intended native terminal path.
