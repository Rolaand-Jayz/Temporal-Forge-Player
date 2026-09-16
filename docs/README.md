# Temporal Forge documentation

This is the documentation entry point. The FSR-centered research line is **closed as of 2026-09-15**.

The governing historical structure is defined by [`DOCUMENTATION_SYSTEM.md`](DOCUMENTATION_SYSTEM.md). The closure set is authoritative for the end-state and for interpretation of formerly active plans.

| Need | Start here |
|---|---|
| What is true now? | [`current/STATE.md`](current/STATE.md) |
| Why was the FSR era closed? | [`closure/FSR41_FINAL_ADJUDICATION_20260915.md`](closure/FSR41_FINAL_ADJUDICATION_20260915.md) |
| What evaluation standard governs the closure? | [`closure/EVALUATION_STANDARD.md`](closure/EVALUATION_STANDARD.md) |
| What claims survived or failed? | [`closure/CLAIM_EVIDENCE_LEDGER.md`](closure/CLAIM_EVIDENCE_LEDGER.md) |
| What remains unresolved? | [`closure/LIMITATIONS_AND_OPEN_QUESTIONS.md`](closure/LIMITATIONS_AND_OPEN_QUESTIONS.md) |
| How does the historical player work? | [`reference/ARCHITECTURE.md`](reference/ARCHITECTURE.md), [`reference/environment.md`](reference/environment.md) |
| Why did the design change over time? | [`decisions/TECHNICAL_HISTORY.md`](decisions/TECHNICAL_HISTORY.md) |
| What did dated campaigns find? | [`reports/`](reports/) and benchmark READMEs |
| Where is exploratory research? | [`research/`](research/) |
| Where are completed/superseded plans and progress records? | [`archive/`](archive/) |
| Where is detailed measurement evidence? | [`../benchmarks/quality_sweeps/`](../benchmarks/quality_sweeps/) and [`../benchmarks/video_corpus/`](../benchmarks/video_corpus/) |

Root-level [`README.md`](../README.md) describes the repository publicly. Root-level [`AGENTS.md`](../AGENTS.md) freezes agent authority for the historical line.

## Authority map

1. **Current repository state:** [`current/STATE.md`](current/STATE.md).
2. **FSR-era closure and evaluation:** [`closure/`](closure/).
3. **Historical executable architecture:** [`reference/ARCHITECTURE.md`](reference/ARCHITECTURE.md) and related references.
4. **Causal direction changes:** [`decisions/TECHNICAL_HISTORY.md`](decisions/TECHNICAL_HISTORY.md).
5. **Primary experiment evidence:** benchmark manifests/artifacts plus dated reports.
6. **Archived plans/progress:** historical context only; not executable authority.

## Active work

There is **no active FSR campaign in this repository**.

The former quality, M6, motion-confidence, portability, and orchestration authorities are retired. Their exact pre-closure contents are indexed in [`archive/FSR_ERA_ACTIVE_RECORDS_20260915.md`](archive/FSR_ERA_ACTIVE_RECORDS_20260915.md). The old `docs/active/` paths remain only as explicit closure tombstones so historical links stay meaningful; they contain no current execution authority.

A future successor project may reuse evidence, algorithms, tests, tooling, datasets, or code from this repository when independently justified. It must not inherit FSR-specific architecture merely because it existed here.
