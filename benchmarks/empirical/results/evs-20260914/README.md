# EVS 2026-09-14 primary capture evidence

This directory contains the compact, committed evidence index for the frozen
64-cell empirical viability capture. The raw PPM frame, event-trace, and
runtime-trace trees are intentionally kept outside Git because they are
multi-gigabyte capture payloads. `primary_capture_index.json` records their
per-cell SHA-256 values, along with input, fixture, binary, configuration,
identity, and liveness provenance.

The primary capture completed in the host Vulkan context with 64/64 cells
complete and 0 failures. A preceding sandbox preflight is recorded as an
environment anomaly in the index; its 48 Vulkan startup failures are not
quality evidence and were not reused by the successful host run.

Reproduction commands from the repository root:

```sh
python3 benchmarks/empirical/evs_fixture.py \
  --output-root /tmp/temporal-forge-evs-20260914-fixtures

python3 benchmarks/empirical/evs_capture.py \
  --manifest benchmarks/empirical/EVS_CP_A_MANIFEST.json \
  --fixture-root /tmp/temporal-forge-evs-20260914-fixtures \
  --output-root /tmp/temporal-forge-evs-20260914-captures-host \
  --player build-fast/temporal_forge_player \
  --re-root <EVS_FSR4_RE_ROOT> \
  --timeout 900

python3 benchmarks/empirical/evs_evaluate.py \
  --manifest benchmarks/empirical/EVS_CP_A_MANIFEST.json \
  --capture-root /tmp/temporal-forge-evs-20260914-captures-host \
  --output-root /tmp/temporal-forge-evs-20260914-evaluation
```

The exact frozen matrix and method/configuration hashes are in
[`EVS_CP_A_MANIFEST.json`](../../EVS_CP_A_MANIFEST.json). The fixture media
and deterministic generator manifest are preserved here for independent
fixture verification.
