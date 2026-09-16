# Temporal Forge FSR-era closure

**Closure date:** 2026-09-15  
**Repository line:** FSR 4.1 adaptation / Temporal Forge Player  
**Status:** **CLOSED — historical research record**

This directory closes the FSR-centered phase of Temporal Forge. The repository remains a reproducible engineering and research record, but it is no longer the active architecture for future Temporal Forge development.

Closure does **not** claim that AMD FSR 4.1 is defective, that temporal reconstruction from ordinary video is impossible, or that every missing game-renderer input was exhaustively reconstructed. It records a narrower decision: the accumulated evidence no longer justifies keeping FSR 4.1 adaptation as the architectural center of Temporal Forge.

## Closure set

- [`EVALUATION_STANDARD.md`](EVALUATION_STANDARD.md) — canonical evidence/validation standard used to judge claims and closure.
- [`FSR41_FINAL_ADJUDICATION_20260915.md`](FSR41_FINAL_ADJUDICATION_20260915.md) — final evidence-based adjudication of the FSR 4.1 adaptation path.
- [`CLAIM_EVIDENCE_LEDGER.md`](CLAIM_EVIDENCE_LEDGER.md) — major research claims mapped to evidence and dispositions.
- [`LIMITATIONS_AND_OPEN_QUESTIONS.md`](LIMITATIONS_AND_OPEN_QUESTIONS.md) — explicit boundaries, unresolved questions, confounders, and nonclaims.

## Primary evidence entry points

- [`../../benchmarks/quality_sweeps/MOTION_CAMPAIGN.md`](../../benchmarks/quality_sweeps/MOTION_CAMPAIGN.md)
- [`../../benchmarks/video_corpus/RESULTS.md`](../../benchmarks/video_corpus/RESULTS.md)
- [`../reports/FSR4_SUPERSAMPLING_REPORT_20260831.md`](../reports/FSR4_SUPERSAMPLING_REPORT_20260831.md)
- [`../reports/M6_RECAPTURE_REPORT_20260901.md`](../reports/M6_RECAPTURE_REPORT_20260901.md)
- [`../reports/QUALITY_LAB_REVIEW_ADJUDICATION_20260906.md`](../reports/QUALITY_LAB_REVIEW_ADJUDICATION_20260906.md)
- [`../LATTICE_CORRUPTION_DIAGNOSTIC.md`](../LATTICE_CORRUPTION_DIAGNOSTIC.md)
- [`../reports/20260911_WORKTREE_CLEANUP_REPORT.md`](../reports/20260911_WORKTREE_CLEANUP_REPORT.md)
- [`../../PROVENANCE.md`](../../PROVENANCE.md)

## Successor boundary

Future Temporal Forge work should begin from the research objective — recovering genuine source-supported detail from temporally distributed video observations — rather than inheriting FSR-specific input contracts, shader topology, motion assumptions, or implementation constraints by default.

Historical code and evidence in this repository may be reused when independently justified. They are evidence, not inherited architectural authority.
