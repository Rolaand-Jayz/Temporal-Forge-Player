# Contributing

Temporal Forge Player's **FSR-centered research line is closed as of 2026-09-15**. Read [`docs/current/STATE.md`](docs/current/STATE.md), [`docs/closure/README.md`](docs/closure/README.md), and [`AGENTS.md`](AGENTS.md) before changing the repository.

There is no standing FSR quality campaign, refactor campaign, or expected-input research task. Behavioral changes require an explicit maintainer request. Historical plans and tombstones are not work queues.

## Build & test

```sh
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build
ctest --test-dir build --output-on-failure
```

The preserved suite registers 23 `tforge_add_test` targets plus one conditional `add_test` (`file_switch_tests`, gated on fixture logic). Four GPU diagnostics — `gpu_probe`, `cm_dump`, `fsr4_harness_tests`, and `jitter_gpu_contract_tests` — are intentionally disabled under ordinary ctest because they require a live Vulkan device and generated/real FSR4 assets.

On a clean machine without external test data, external-data-dependent tests should report **SKIP, not FAIL**. A failing clean-machine run can indicate a portability regression.

### Headless smoke test

```sh
timeout --signal=TERM 8 ./build/temporal_forge_player benchmarks/video_corpus/clips/<clip>.mp4
```

Exit code 124 is the timeout. Runtime diagnostics can be inspected to determine which backend actually executed; do not infer successful FSR dispatch merely from requested configuration.

## Code style

- **Language:** C++23 with the warnings configured by `CMakeLists.txt`.
- **Formatting:** use the committed `.clang-format`; do not reformat unrelated files.
- **Linting:** `.clang-tidy` is advisory. Never accept mechanical fixes that silently change GPU/resource/threading semantics.

## Historical invariants

The preserved player contains invariants established through regression work. If an explicitly requested change touches them, add/adjust tests and preserve the causal record.

Notable examples include:

1. UI-thread teardown versus decode-thread FSR dispatch synchronization through `fsrDispatchMutex_`.
2. GPU retirement through `vkQueueWaitIdle()` before teardown destroys resources visible to rendering.
3. Reconstruction target changes remaining distinct from ordinary window resize.
4. Decode-thread fence/dispatch lifecycle remaining safe during stop/teardown.

These are properties of the historical implementation, not mandatory design choices for a successor project.

## Documentation and evidence

Use [`docs/DOCUMENTATION_SYSTEM.md`](docs/DOCUMENTATION_SYSTEM.md) and [`docs/closure/EVALUATION_STANDARD.md`](docs/closure/EVALUATION_STANDARD.md).

- Preserve negative results.
- Keep invalidated evidence invalidated.
- Keep conditional results conditional.
- Do not rewrite old reports to create hindsight.
- Do not convert archived imperative text back into current authority.
- If maintenance changes what the preserved player actually does, update current/reference documentation and state why the historical record changed.

## What should remain discoverable

Before deleting historical material, verify its evidentiary role. In particular, preserve or deliberately replace links to:

- FSR reconstruction/provenance records;
- benchmark manifests and temporal evidence;
- disabled opt-in diagnostics;
- lattice and quality adjudication history;
- clean-clone portability evidence;
- the 2026-09-15 closure set.

## Successor work

New custom Temporal Forge architecture belongs in its own active project/repository. This codebase may be mined for techniques, tests, evidence, datasets, or code when justified, but it should not be converted in place into the successor merely because doing so is convenient.
