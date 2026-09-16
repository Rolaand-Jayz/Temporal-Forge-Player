# Temporal Forge Agent Instructions

## Repository status

**CLOSED HISTORICAL RESEARCH LINE — 2026-09-15**

This repository preserves the FSR-centered Temporal Forge Player research era. It is no longer the active architecture for future Temporal Forge development.

There is **no active quality campaign** and no standing instruction to continue FSR 4.1 optimization, expected-input reconstruction, motion/jitter tuning, or campaign capture.

Start with:

- [`docs/current/STATE.md`](docs/current/STATE.md)
- [`docs/closure/README.md`](docs/closure/README.md)
- [`docs/closure/FSR41_FINAL_ADJUDICATION_20260915.md`](docs/closure/FSR41_FINAL_ADJUDICATION_20260915.md)
- [`docs/closure/EVALUATION_STANDARD.md`](docs/closure/EVALUATION_STANDARD.md)

## Authority rule

Archived plans, historical progress logs, benchmark runners, and old campaign prompts are evidence and causal context only. They are **not executable authority** merely because they contain imperative language.

Do not resume a closed campaign unless the maintainer explicitly directs that historical work to be reopened.

## Default behavior in this repository

Without an explicit maintainer request to change behavior:

- preserve source, tests, benchmark evidence, provenance, and historical artifacts;
- inspect and explain evidence without rewriting history;
- correct broken links or demonstrably false documentation only when the correction preserves causal context;
- do not tune FSR quality parameters;
- do not start new FSR expected-input experiments;
- do not change shaders, reconstruction behavior, model assets, weights, or backend policy;
- do not reinterpret invalidated evidence as valid;
- do not promote a historical candidate to current/default status;
- do not treat this repository as the template for a successor Temporal Forge architecture.

Behavioral code changes require an explicit maintainer request.

## Evidence standard

Use [`docs/closure/EVALUATION_STANDARD.md`](docs/closure/EVALUATION_STANDARD.md).

Keep measured facts, observations, inferences, hypotheses, unresolved questions, and project decisions distinct. Preserve negative results. Keep conditional results conditional. Do not claim that FSR 4.1 can never work for video; the closure decision is narrower: the accumulated evidence no longer justifies FSR adaptation as the architectural center of Temporal Forge.

## Documentation model

[`docs/README.md`](docs/README.md) is the documentation entry point. [`docs/DOCUMENTATION_SYSTEM.md`](docs/DOCUMENTATION_SYSTEM.md) defines the historical documentation model.

The closure set supersedes stale imperative language in older active-era documents. Completed plans and progress records belong in the archive and must not regain authority through quotation or relocation.

## Successor boundary

Future Temporal Forge development should begin from the video-reconstruction objective and independently choose its architecture. Reuse from this repository is permitted when evidence supports it, but FSR-specific motion, jitter, history, scaling, graph, and composition assumptions are not inherited by default.

The old repository is a source of evidence, not a solution template.
