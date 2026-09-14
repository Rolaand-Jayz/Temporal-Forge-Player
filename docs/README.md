# Temporal Forge documentation

This is the documentation entry point. The governing structure is defined by
[`DOCUMENTATION_SYSTEM.md`](DOCUMENTATION_SYSTEM.md).

| Need | Start here |
|---|---|
| What is true now? | [`current/STATE.md`](current/STATE.md) |
| How does the current system work? | [`reference/ARCHITECTURE.md`](reference/ARCHITECTURE.md), [`reference/environment.md`](reference/environment.md) (the `TFORGE_*` environment contract) |
| What is being worked on now? | [`active/EMPIRICAL_VIABILITY_SPRINT.md`](active/EMPIRICAL_VIABILITY_SPRINT.md) (the single active research authority) |
| Where are completed campaign plans and status records? | [`archive/plans/`](archive/plans/) |
| Why did the design change? | [`decisions/TECHNICAL_HISTORY.md`](decisions/TECHNICAL_HISTORY.md) |
| What did a dated campaign find? | [`reports/`](reports/) (e.g. the review adjudication of 2026-09-06, [`reports/QUALITY_LAB_REVIEW_ADJUDICATION_20260906.md`](reports/QUALITY_LAB_REVIEW_ADJUDICATION_20260906.md)) and the benchmark READMEs |
| Where is exploratory research? | [`research/`](research/) |
| Where are completed plans and old prompts? | [`archive/`](archive/) |
| Where is detailed measurement evidence? | [`../benchmarks/quality_sweeps/`](../benchmarks/quality_sweeps/) |

Root-level [`README.md`](../README.md) describes the product. Root-level
[`AGENTS.md`](../AGENTS.md) contains operating instructions and points back to
this map.

## Authority map

- Current state: [`current/STATE.md`](current/STATE.md).
- Current architecture and durable implementation invariants:
  [`reference/ARCHITECTURE.md`](reference/ARCHITECTURE.md).
- Current FSR input and motion contracts: [`reference/motion/`](reference/motion/).
- Active research: [`active/EMPIRICAL_VIABILITY_SPRINT.md`](active/EMPIRICAL_VIABILITY_SPRINT.md)
  is the sole current research authority. Detailed experiment execution and
  its checkpoint evidence are recorded there and under the linked EVS result
  package.
- Completed quality, portability, and remediation plans are historical under
  [`archive/plans/`](archive/plans/); they do not direct current execution.
- Detailed measurements: benchmark manifests and artifacts, not copied tables
  in narrative documents.
- Completed plans, dated reports, and research are historical or exploratory
  unless they explicitly link a verified conclusion into a current document.
