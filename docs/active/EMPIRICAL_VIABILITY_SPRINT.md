# Empirical viability sprint

**Status:** COMPLETE — CP-E VERDICT RECORDED

**Authority:** This is the sole active Temporal Forge research authority.

**Branch:** `empirical-viability-sprint`

**Pre-sprint baseline:** `b8bbb86b4bbeff32e50427e64c3c257369b0b693`

**Purpose:** Determine empirically whether the existing Forge reconstruction
path provides repeatable, supported high-frequency recovery beyond the strongest
existing deterministic spatial baseline, under a controlled pair of sampling
regimes.

AMD parity/oracle work, DLL or SDK archaeology, provenance hunting, and the
historical Final Word campaign are out of scope. The Final Word material is
preserved on `archive/final-word-pre-viability-20260913`; it is not active
authority. Detailed experiment execution has not begun at the time this
contract is established.

## Scope and invariants

- Work only on `empirical-viability-sprint`.
- Do not modify `main` or either archive branch.
- Do not change reconstruction behavior, model weights, convolution topology,
  UI/player behavior, or runtime defaults during the sprint.
- Do not perform additional FSR research or oracle/parity investigation.
- Keep every quality parameter runtime-configurable and record the exact
  configuration used for every cell.
- Preserve the existing baseline path as a selectable control.
- Capture workflows must not stop or terminate user processes, including games.
- Failed cells, negative evidence, anomalies, and rejected interpretations are
  retained rather than hidden.
- The complete primary matrix is 64 cells. Confirmation is optional and is
  capped at 16 cells. The hard maximum is 80 cells total.

## Frozen primary matrix

The primary matrix is the Cartesian product of:

| Axis | Frozen values |
| --- | --- |
| Structural class | `S1` thin geometry; `S2` fine text/glyphs; `S3` repeating texture; `S4` natural detail |
| Sampling regime | `R1` phase-rich known fractional HR motion before filtering/decimation; `R2` strong-prefilter control that deliberately suppresses complementary recoverable high frequency |
| Reconstruction method | `M1` external deterministic Lanczos3; `M2` `benchmarks/quality_sweeps/stage_a/base_only_bilinear.json`; `M3` `benchmarks/quality_sweeps/stage_h_temporal_tiny/current_control.json` (pre-sprint Forge default, Quality Lab disabled); `M4` `benchmarks/quality_sweeps/swarm/agent_composition_audit/current_control.json` (existing explicit current-composition control) |
| Frozen sequence | `SEQ-A`; `SEQ-B` |

This is `4 × 2 × 4 × 2 = 64` cells. The frozen fixture contract is
`1920×1080` pristine ground truth, `640×360` encoded input, `1920×1080`
output, eight scored frames per sequence, and a 3:1 spatial decimation. The
exact transforms, phases, filter kernels, method configuration paths, and
environment are frozen in the machine-readable experiment manifest at
checkpoint CP-A before any primary capture is run. The committed contract is
[`benchmarks/empirical/EVS_CP_A_MANIFEST.json`](../../benchmarks/empirical/EVS_CP_A_MANIFEST.json).
Sequence definitions are fixed before reconstruction results are inspected.

### Fixtures and regimes

Each structural class has a pristine high-resolution ground-truth sequence.
The low-resolution input is generated from that same sequence with a known
transform, filtering, and decimation pipeline. `R1` uses a Gaussian prefilter
with `sigma=0.70` and phase-rich fractional motion. `R2` uses the same
transform family with a stronger Gaussian prefilter, `sigma=2.20`, which is
the deliberate suppression control. `SEQ-A` uses fractional translations
`(0.37, 0.23)` pixels per frame with phase origin `(0.11, 0.07)`; `SEQ-B`
uses `(0.19, 0.41)` pixels per frame with phase origin `(0.43, 0.29)`.
The exact class generators, seed, frame range, and filter implementation are
recorded in the CP-A manifest. No regime is redefined after results are
observed.

### Reconstruction methods

`M1` is a deterministic Lanczos3 image-space resize of the decoded low-
resolution input. `M2` is the strongest existing deterministic spatial
baseline supported by the matched spatial record in
`docs/reports/M6_RECAPTURE_REPORT_20260901.md`: explicit base-only bilinear.
`M3` is the pre-sprint default Forge path with the existing Quality Lab
configuration disabled. `M4` is the existing explicit current-composition
control (`learnedStrength=0.55`, `residualStrength=1.0`, Catmull-Rom base,
neutral tone/sharpen/presentation settings). Both Forge arms use the same
frozen `Quality` 1.5x, 1920×1080 viewport, software-decode, CAS-disabled
capture environment; only the method identity/configuration differs.

The exact source hashes and environment are written into the manifest at CP-A.
No new method, strength, filter value, or model setting may be introduced
after CP-A.

## Required provenance and liveness

Every cell must have a stable experiment identity and a provenance record
covering:

- source and configuration commit SHAs, dirty-state status, and experiment ID;
- fixture, ground-truth, low-resolution input, output, and manifest hashes;
- exact HR/LR/output dimensions, frame range, sequence, phase, motion, and
  sampling regime;
- reconstruction method, complete config, relevant environment, and capture
  command;
- runtime/toolchain/device details needed to reproduce the result;
- output frame hashes and capture logs;
- control-liveness evidence for each new control path.

The evaluator must reject incomplete identity or mismatched runtime trace
provenance. A successful process exit without valid output and identity is not
a successful cell.

## Measurements

The evaluator reports per-frame and aggregate measurements against ground
truth and against the strongest spatial control:

- SSIM and PSNR;
- high-frequency residual correlation;
- signed/phase agreement;
- edge-position error;
- thin-structure precision and overlap;
- Fourier-band error;
- unsupported-detail energy;
- registered temporal error;
- edge variance, static flicker, periodicity, and temporal artifact flags.

For the causal residual comparison, define
`GT_residual = highpass(GT - B)` and
`FSR_residual = highpass(FSR - B)`, where `B` is the frozen strongest spatial
baseline output for the same cell. Report residual magnitude and
phase/sign correspondence, not only global image scores.

Temporal review must record crawling, flicker, lattice patterns, edge drift,
ghosts, history persistence, unstable detail, and phase oscillation when
present. Representative artifacts and failed outputs remain in the evidence
package. Visual descriptions are made only from inspected artifacts; numeric
metrics do not substitute for artifact review.

## Decision rule

The central causal comparison is Forge advantage in `R1` versus `R2`.

The survival target is repeatable, ground-truth-correlated high-frequency
recovery of at least 10% relative to the strongest spatial baseline on at
least 3 of 4 structural classes in `R1`, with supported structure, no global
fidelity collapse, and usable temporal behavior. This target is interpreted
with the full evidence; it is not applied mechanically.

Failure indicators include the strongest spatial method winning, unsupported
high-frequency energy, sharpening-only behavior, equal advantage in `R1` and
`R2`, wrong phase, a narrow or cherry-picked fixture, worse global fidelity,
or unstable temporal behavior/noise-floor differences.

After the 64 primary cells and their provenance are complete, run at most 16
confirmation cells only if the primary matrix contains a credible, repeatable
signal that needs confirmation. Otherwise skip confirmation and document why.

The final verdict is exactly one of:

1. **EMPIRICAL SURVIVAL** — the target is met with causal and temporal support.
2. **EMPIRICAL HYBRID SIGNAL** — a bounded, reproducible signal exists but the
   full survival target is not met; state the supported subset and limits.
3. **EMPIRICAL FAILURE** — the evidence does not support a useful Forge
   advantage under this contract.

## Checkpoints and stopping rule

Before primary capture:

- **CP-A:** commit and push exactly
  `EVS-CP-A: freeze empirical viability experiment`.

After all 64 primary captures and complete provenance:

- **CP-B:** commit and push exactly
  `EVS-CP-B: complete 64-cell primary capture matrix`.

After primary evaluation:

- **CP-C:** commit and push exactly
  `EVS-CP-C: evaluate primary empirical matrix`.

If confirmation is justified:

- **CP-D:** commit and push exactly
  `EVS-CP-D: complete empirical confirmation`.

After the final evidence package and one verdict are recorded:

- **CP-E:** commit and push exactly
  `EVS-CP-E: empirical viability verdict`.

Do not begin a later stage when an earlier result changes the correct path.
Stop after CP-E. Do not begin another campaign or the 64-cell matrix again
without a separately authorized change to this authority document.

## Evidence package

The completed package must make the result reproducible from the repository:
exact baseline and final commits; frozen matrix and fixture definitions;
sequence phases and transforms; method identities and complete configs;
capture manifests and hashes; per-cell metrics; R1/R2 causal comparison;
temporal findings; inspected artifacts; anomalies; killed interpretations;
confirmation decision; final verdict; and exact commands used.

The active document is updated at each checkpoint with measurements,
observations, rejected hypotheses, conclusions, and links to the committed
artifacts. No experiment is considered complete without its required
validation and provenance.

## Execution record

### CP-A — frozen contract

The frozen contract was committed and pushed as
`626641100df48157417e10de1e27e23b14d3df0a`
(`EVS-CP-A: freeze empirical viability experiment`). No player capture had
started at this checkpoint. The commit changed only the active authority,
experiment tooling, the machine-readable contract, and contract tests; it did
not change reconstruction behavior or runtime defaults.

### CP-B — primary capture complete

The host Vulkan rerun completed all 64 primary cells: 64 complete, 0 failed,
16 cells per method, and eight output frames per cell. All 64 cell identities
verified; all 48 Forge cells had matching runtime traces and passed the binary,
commit, configuration, and run-identity cross-checks. The capture used the
CP-A commit, `build-fast/temporal_forge_player` SHA
`2e77637b009671b89b75f5ce64ec014bf857958404c0b967b3c2fdd48d13c27a`, and
the committed fixture manifest SHA
`6ac763a76ff093d09645559366ee4b3bc213ff95be4c10464fa3fdda5bd6453e`.

A sandbox preflight attempted the same 64 cells but produced 48 `SIGABRT`
Vulkan-startup failures because that context exposed neither `/dev/dri` nor
`/dev/kfd`. Those failures are recorded as an environment anomaly, not as
image-quality evidence. The successful host rerun used a separate output root
and did not reuse failed cells.

The compact capture evidence is committed under
[`benchmarks/empirical/results/evs-20260914/`](../../benchmarks/empirical/results/evs-20260914/),
especially `primary_capture_index.json`, `capture_identity.json`, and
`fixture_manifest.json`. Raw frame payloads remain outside Git with their
hashes recorded in the index.

### CP-C — primary evaluation complete; confirmation skipped

The frozen evaluator completed all 64 cells. The current aggregate means are:

| Method | Mean PSNR (dB) | Mean SSIM | Mean HF correlation | Mean registered temporal error |
| --- | ---: | ---: | ---: | ---: |
| M1 Lanczos3 | 29.7264 | 0.887036 | 0.489379 | 0.004605 |
| M2 base-only bilinear | 27.5335 | 0.882298 | 0.462123 | 0.004797 |
| M3 Forge pre-sprint default | 27.2653 | 0.873217 | 0.442507 | 0.009236 |
| M4 Forge current-composition control | 26.9863 | 0.863540 | 0.351128 | 0.009966 |

The committed evaluation package contains the 64 per-cell metric JSON files,
`primary_metrics.csv`, `primary_evaluation.json`, and
`primary_interpretation.json`. The R1 high-frequency-correlation comparison
against M2 is:

| Class | M2 baseline | M3 Forge default | M3 delta | M4 current composition | M4 delta |
| --- | ---: | ---: | ---: | ---: | ---: |
| S1 | 0.464745 | 0.439715 | -5.39% | 0.366283 | -21.19% |
| S2 | 0.551084 | 0.529424 | -3.93% | 0.442442 | -19.71% |
| S3 | 0.789226 | 0.770379 | -2.39% | 0.658287 | -16.59% |
| S4 | 0.279316 | 0.267091 | -4.38% | 0.183700 | -34.23% |

Neither Forge arm reaches the `+10%` R1 survival target on any structural
class (`0/4` for M3 and `0/4` for M4). Relative to M2, M3 loses `0.2682 dB`
PSNR and `0.009082` SSIM in the global mean and increases registered temporal
error by `0.004439`; M4 loses `0.5472 dB` PSNR and `0.018759` SSIM and
increases temporal error by `0.005169`.

The causal R1 residual comparison against M2 is also negative for both Forge
arms:

| Method | Residual correlation | Signed projection | Phase agreement | Residual magnitude ratio |
| --- | ---: | ---: | ---: | ---: |
| M3 | -0.064007 | -0.007675 | 0.469044 | 0.149649 |
| M4 | -0.074770 | -0.018572 | 0.470161 | 0.294606 |

The R1/R2 contrast does not provide a Forge advantage over M2. The primary
matrix therefore contains no credible, repeatable signal requiring a bounded
confirmation subset; confirmation is skipped and CP-D is not required. This
is a numeric conclusion only. No visual artifact claim is made here without a
separate inspected-artifact record.

### Temporal and visual artifact review

A delegated read-only artifact review inspected all eight frames for `SEQ-A`
across `S1_R1`, `S1_R2`, `S2_R1`, `S2_R2`, `S3_R1`, `S3_R2`, `S4_R1`, and
`S4_R2`, comparing GT, M1, M2, M3, and M4. `SEQ-B` was not covered by the
visual pass; that limitation is preserved in
[`visual_review.md`](../../benchmarks/empirical/results/evs-20260914/visual_review.md).

The review found visible softness/lower contrast in M2–M4 versus GT/M1 for
thin geometry and text, with the strongest artifact in S3 repeating texture:
reduced lattice contrast and a changing broad diagonal envelope producing
periodic crawling/phase modulation. It found no clear ghost trails, history
persistence, edge drift, or unstable large-scale detail in the inspected
SEQ-A set. These observations are bounded to the inspected artifacts and are
not generalized to unreviewed SEQ-B frames.

The liveness record shows 16 complete traces per Forge method, 128 events per
method, complete frame and transition indices, one expected forced reset per
cell, zero detector scene-cut events, and no ghost cause beyond the expected
initial forced reset. M2 had history/recurrent disabled and jitter off; M3/M4
had history/recurrent enabled with synthetic Halton-23 upload jitter, matching
their frozen method identities. M3/M4 registered temporal errors were
`0.009236` and `0.009966`, versus `0.004797` for M2. See
[`temporal_liveness_summary.json`](../../benchmarks/empirical/results/evs-20260914/temporal_liveness_summary.json).

### CP-E — empirical viability verdict

**Verdict: EMPIRICAL FAILURE**

The existing Forge reconstruction path does not meet the frozen survival
target. Both Forge arms lose to the strongest spatial baseline on HF
correlation in all four R1 structural classes, neither reaches the required
`+10%` relative recovery on any class, both have worse global fidelity and
registered temporal error, and the causal R1 residual correlations and signed
projections are negative. The bounded visual review adds softness and periodic
crawling/phase modulation evidence for S3 rather than support for recovered
structure. No confirmation subset is run, no CP-D is created, and no
reconstruction-quality change is retained.

The final evidence package is under
[`benchmarks/empirical/results/evs-20260914/`](../../benchmarks/empirical/results/evs-20260914/).
This sprint stops here. No AMD parity/oracle work, additional FSR research,
or follow-on empirical campaign begins under this authority.
