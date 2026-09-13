# Forge's Final Word Campaign Ledger

Append-only material experiment and checkpoint ledger.

## FFW-CP0 — freeze Final Word campaign baseline

- **Experiment/checkpoint ID:** FFW-CP0
- **Hypothesis/purpose:** Freeze the reviewed Gate-0 evidence baseline before
  any final FSR4 research work.
- **Commit:** `5a23a82bb6bc3d14a53423706c9f28c9dbced60f`
- **Configuration:** Remote `origin/forge-final-word-gate0`; clean worktree;
  Gate-0 evidence package dated 2026-09-13; no new capture or production
  behavior change.
- **Evidence:** `benchmarks/gate0/audit/20260913/`; terminal determinism and
  anti-sharpening remediation supplements under
  `benchmarks/gate0/evidence/remediation-20260913/`.
- **Result:** Remote baseline verified at the recorded commit. Gate-0 status
  is internally verified; independent audit and Sol adjudication remain
  pending.
- **Verdict:** CP0 complete; baseline accepted for bounded campaign execution.
- **Resulting action:** Unlock Gate 1A provenance lock only.
- **Status:** active checkpoint; no experiment performed.

## FFW-CP2 / FFW-CP10 — provenance terminal result and campaign completion

- **Experiment/checkpoint ID:** FFW-CP2, finalized by FFW-CP10
- **Experiment/hypothesis:** A credible version-matched AMD FSR4 runtime and
  public-boundary parity oracle can be locked from the available environment.
- **Commit:** `6b28ae1abe7ab5d7ccdfcdcebf1985014aeb175e` baseline; final package
  commit recorded when this entry is committed.
- **Configuration:** Gate 1A provenance inventory only; no player launch,
  parity capture, reconstruction change, or Gate-1B experiment.
- **Evidence:** `docs/reports/20260913_FFW_GATE1A_PROVENANCE.md`, `PROVENANCE.md`,
  Gate-0 audit bundle, and local Steam-prefix DLL inventory.
- **Result:** Forge and generated native-pack provenance is identifiable. The
  only local AMD DLL family is identical at SHA-256
  `4e7dc37aebea3a90e3d3cc43e24cb2b54176b2535315f20dbe63b3b7cfc56b1e` and
  contains `4.0.1` strings; it is not a 4.1.x oracle. The RE source reports
  runtime equivalence as unvalidated.
- **Verdict:** Terminal failure of the parity prerequisite.
- **Resulting action:** `TEMPORAL FORGE FSR4 RESEARCH CONCLUDED`.
- **Status:** killed/terminal; no parity, GT, temporal, performance, or rescue
  work performed.
