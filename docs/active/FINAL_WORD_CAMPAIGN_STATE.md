# Forge's Final Word Campaign State

**Campaign branch:** `forge-final-word-campaign`
**State date:** 2026-09-13
**Current checkpoint:** FFW-CP0 — baseline freeze

## Current gate and task

CP0 is complete. The accepted Gate-0 baseline is frozen locally from the
remote `forge-final-word-gate0` branch at commit
`5a23a82bb6bc3d14a53423706c9f28c9dbced60f`.

The next authorized task is Gate 1A: lock the strongest defensible FSR4
provenance chain and identify the version-matched AMD reference target. No
Gate-1 experiment has started in this checkpoint.

## Baseline and evidence

- Gate-0 implementation and internal adversarial verification are complete.
- Gate-0 evidence package: `benchmarks/gate0/audit/20260913/`.
- Terminal-tier determinism supplement: `benchmarks/gate0/evidence/remediation-20260913/terminal_determinism/`.
- Live anti-sharpening supplement: `benchmarks/gate0/evidence/remediation-20260913/anti_sharpening_challenge/`.
- Same-session review remains reproduction/supporting evidence, not an independent audit.
- `FINAL INDEPENDENT AUDIT: PENDING`.
- `SOL GATE-0 ADJUDICATION: PENDING`.

## Unlocked and prohibited work

Unlocked after CP0: Gate 1A provenance lock, limited to repository/source,
binary, model, shader, version, and reference-runtime identification.

Still prohibited: Gate 1B parity capture until provenance and an oracle target
are recorded; GT-A/GT-B/GT-C; reconstruction changes; broad reverse
engineering; rescue paths; UI work; and unrelated cleanup/refactoring.

## Hypotheses and conditional branches

No new campaign hypothesis has been tested. No conditional branch has
triggered. Gate-0 killed or limited hypotheses remain recorded in the Gate-0
plan and evidence records; none is resurrected here.

## Blockers and next action

No CP0 blocker. Next action: inspect the current repository's FSR4 provenance
and available reference-runtime evidence, then record a bounded Gate 1A
decision or the authorized terminal conclusion if a credible match cannot be
established.
