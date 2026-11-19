## What this changes

<!-- One or two sentences. If this changes behavior, link the maintainer request. -->

## Checklist

- [ ] This does not reopen the closed FSR campaign. No quality tuning, expected-input experiments, or changes to shaders, model assets, weights, or backend policy without an explicit maintainer request.
- [ ] Claims carry evidence labels (measured fact, observation, inference, hypothesis, unresolved, project decision) per `docs/closure/EVALUATION_STANDARD.md`.
- [ ] Negative results are kept. Invalidated evidence stays invalidated. Conditional results stay conditional.
- [ ] Old reports and archived plans are not rewritten. Any correction keeps the causal record and says why it changed.
- [ ] Third-party and reverse-engineering-derived material is recorded in `PROVENANCE.md` and `THIRD_PARTY_LICENSES.md` and is not relabeled as Apache-2.0. No weight blobs or compiled `*.spv` are committed.
- [ ] Links to the closure set, provenance records, benchmark evidence, and lattice history still resolve.
- [ ] `ctest --test-dir build --output-on-failure` passes. Tests that need external data report SKIP, not FAIL.
- [ ] Changes that touch a historical invariant in `CONTRIBUTING.md` add or adjust tests. Unrelated files are not reformatted.
