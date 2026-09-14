# Public repository cleanup — 2026-09-13

This is a historical cleanup record, not an active research authority. The
current authority is [`../active/EMPIRICAL_VIABILITY_SPRINT.md`](../active/EMPIRICAL_VIABILITY_SPRINT.md).

## Protected and preserved state

- Remote `main` at audit start: `285a5788f89787bce0ca26f8e8e8ca312890723f`.
- Final Word source tip preserved unchanged at
  `archive/final-word-pre-viability-20260913`:
  `9cc6184ed0ad98cc6b10016914b30f22cfd618fb`.
- Unique stale tips were additionally made reachable from
  `archive/legacy-remote-tips-20260913`:
  `969b719d348ef22f34a73ab8fe979c105f077bc2`.
- The legacy archive has the `main` tree and the exact audited stale tips as
  parents. It is historical preservation only, not an active campaign.
- The initial `origin/pr-7-head` observation was pruned by the remote refresh
  before the current branch inventory; no current remote branch or open PR
  depended on it.
- Open pull-request audit: none (`gh pr list --state open` returned `[]`).

## Remote branch audit

Merge bases are against the protected remote `main` tip above.

| Branch | Tip | Merge base | Classification | Evidence / disposition |
|---|---|---|---|---|
| `main` | `285a5788f89787bce0ca26f8e8e8ca312890723f` | same | KEEP — MAIN | Protected public product branch; not rewritten or changed. |
| `forge-final-word-campaign` | `9cc6184ed0ad98cc6b10016914b30f22cfd618fb` | `285a5788...` | DELETE — SUPERSEDED | Exact tip preserved by the Final Word archive. |
| `forge-final-word-gate0` | `5a23a82bb6bc3d14a53423706c9f28c9dbced60f` | `285a5788...` | DELETE — SUPERSEDED | Earlier Final Word line; history is reachable from the later archive tip. |
| `archive/final-word-pre-viability-20260913` | `9cc6184ed0ad98cc6b10016914b30f22cfd618fb` | `285a5788...` | KEEP — ARCHIVE | Required immutable historical Final Word location. |
| `expected-food-terminal-sprint` | `ca5b906c501b97b2098c5a0021a0830850efed01` | `285a5788...` | DELETE — SUPERSEDED | Its one unique preparation report is an ancestor of the Final Word archive. |
| `portability/clean-clone-remediation` | `d1b5d24931f6dfe02d7838ec09ef8699dd5361fa` | `d1b5d249...` | DELETE — MERGED | Tip is already an ancestor of `main`. |
| `quality-lab-vibecoder` | `db19cfb34f830d4dce147c4d662b5c4b9557d6a4` | `db19cfb...` | DELETE — MERGED | Tip is already an ancestor of `main`. |
| `codex/quality-lab-confidence-fallback` | `7160a7edd7b6d944cd3c242287d41137823f5d58` | `0425ab2e...` | DELETE — SUPERSEDED | Closed fallback review line; exact tip retained by the legacy archive. |
| `codex/quality-lab-confidence-fallback-final` | `41fa76bde8eb97541b41ee1db03daaad3fa45cbf` | `fdfe7c40...` | DELETE — SUPERSEDED | Closed final review stack; exact tip retained by the legacy archive. |
| `codex/quality-lab-confidence-fallback-reviewable` | `5ddd23c47395863e41eb555bd865ee809479b9a4` | `fdfe7c40...` | DELETE — SUPERSEDED | Reviewable predecessor; exact tip retained by the legacy archive. |
| `codex/quality-lab-docs-reviewable` | `9bb24d6d5a8b54db356f82c39b4624edd4e5611a` | `fdfe7c40...` | DELETE — SUPERSEDED | Reviewable documentation stack; exact tip retained by the legacy archive. |
| `codex/quality-lab-production` | `db0af42d993140578456a26bbf08c7c6107a5238` | `0425ab2e...` | DELETE — SUPERSEDED | Closed production-line review branch; exact tip retained by the legacy archive. |
| `codex/quality-lab-tooling-reviewable` | `75844eac94730a5675759eced8e13ab0c291fbba` | `fdfe7c40...` | DELETE — SUPERSEDED | Closed tooling review stack; exact tip retained by the legacy archive. |
| `pr/confidence-fallback` | `6ae08932f2a5a8cd870240a83782a5799acfabca` | `0425ab2e...` | DELETE — SUPERSEDED | Obsolete review branch; exact tip retained by the legacy archive. |
| `motion-campaign-devil` | `83a687aa010791aaeeca2bcbb9feda758c14a155` | `0425ab2e...` | DELETE — ABANDONED | Closed adversarial side-line; not active research and not needed by the reset. |
| `stage0/devil-home-77fbd97` | `77fbd97a7eef52d3e0be37f24e98d2085a751876` | `0425ab2e...` | DELETE — SUPERSEDED | Ancestor of the abandoned motion side-line; retained through the legacy archive. |
| `stage0/tf-pr2-corrected-0560f8a` | `0560f8a4429982b545c5860fbf3594a762bdcf64` | `0425ab2e...` | DELETE — SUPERSEDED | Earlier corrected review line; exact tip retained by the legacy archive. |
| `archive/legacy-remote-tips-20260913` | `969b719d348ef22f34a73ab8fe979c105f077bc2` | `285a5788...` | KEEP — ARCHIVE | Single historical reachability point for unique stale tips. |

## Infrastructure carried forward

Source archival commits reviewed:

- `da3af13ac7fad35b4a9a4daad3aae20c0959d1e8` — identity/provenance spine;
- `1ee922612e476d0fb1407f2d277ece57b6f2f13a` — state-generation trace;
- `ac4f712b321da02caa69df477dabbc1fae307360` — liveness/determinism probes
  and runtime-trace correction;
- `80a696e823f482684c980f9ab044aa16eff241a7` — evaluator policy;
- `dec86736ce89e7e26603631faff40cf9031ee7dc` — capture/determinism launcher
  hardening.

Imported in commit `18d5cb639`:

- `benchmarks/empirical/experiment_identity.py`;
- `benchmarks/empirical/player_run.py`;
- `benchmarks/empirical/control_liveness.py`;
- `benchmarks/empirical/determinism_probe.py`;
- `benchmarks/empirical/evaluator_policy.py`;
- the five corresponding `tests/test_empirical_*.py` contract suites;
- trace-only fields in `src/core/PlaybackEngine.cpp/.hpp` and
  `src/render/GpuImageUploader.cpp/.hpp`.

The Python namespace, schemas, and run identifiers were renamed from the old
Gate-0/FFW naming to `benchmarks.empirical`, `temporal_forge.empirical.*`, and
`EVS-*`. The imported C++ changes only expose provenance/state-generation
traces and correct trace reporting; no reconstruction algorithm, shader,
weight, topology, or UI behavior was changed.

Intentionally excluded: Final Word authority plans and ledgers, Gate-0 audit
and evidence payloads, Gate-1A conclusions, same-session review artifacts,
oracle/DLL hunt material, old campaign runners, and obsolete prompts. Those
remain only on the historical preservation refs where they originated.

## Authority reset

The former active quality, portability, motion-fallback, M6, and progress
documents were moved to `docs/archive/plans/`. The only active research
authority on this branch is
[`docs/active/EMPIRICAL_VIABILITY_SPRINT.md`](../active/EMPIRICAL_VIABILITY_SPRINT.md).
