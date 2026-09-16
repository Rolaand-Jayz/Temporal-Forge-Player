# Temporal Forge Documentation System

**Status:** CURRENT  
**As of:** 2026-09-15  
**Purpose:** Define authority, historical integrity, and evidence navigation for the closed FSR-era repository.

## 1. Governing principle

The repository must make it easy to distinguish:

```text
WHAT IS TRUE NOW
        ↓
WHY THE FSR ERA ENDED
        ↓
HOW THE HISTORICAL PLAYER WORKS
        ↓
WHAT THE EXPERIMENTS FOUND
        ↓
WHAT WAS BELIEVED OR PLANNED AT EACH TIME
        ↓
WHERE THE PRIMARY EVIDENCE LIVES
```

Historical truth must be preserved without allowing historical instructions to become current authority.

## 2. Current authority

Authority order for project direction is:

1. [`current/STATE.md`](current/STATE.md) — what this repository is now.
2. [`closure/`](closure/) — final FSR-era adjudication, evaluation standard, claim dispositions, limitations, and successor boundary.
3. [`reference/`](reference/) — how the preserved historical implementation works.
4. [`decisions/TECHNICAL_HISTORY.md`](decisions/TECHNICAL_HISTORY.md) — causal direction changes.
5. primary benchmark manifests/artifacts and dated reports — experiment evidence.
6. [`archive/`](archive/) and former `active/` records — historical context only.
7. exploratory research, hypotheses, prompts, and unverified narrative.

Executable code remains authoritative for what the preserved player actually does. Closure documents remain authoritative for whether FSR work is active: **it is not**.

## 3. Documentation classes

### Current state

Answers: **What is true now?**

Maintain one concise current-state document. It should identify closure status, relevant source identity, final implementation state, evidence state, boundaries, and current authority.

### Closure

Answers: **Why did this research era end, what survived evaluation, and what may be carried forward?**

Closure documents are not a rewrite of history. They synthesize the historical record while keeping measured facts, inference, unresolved questions, and project decisions distinct.

### Reference / architecture

Answers: **How does the preserved implementation work?**

Reference documents must match code or explicitly state divergence. After closure they describe the historical player; they do not imply that its architecture is the successor architecture.

### Decisions / causal history

Answers: **Why did the project change direction?**

Preserve material chains such as:

```text
problem → hypothesis → experiment → evidence → conclusion → decision
```

### Reports / experimental results

Answers: **What happened in a dated experiment or campaign?**

Reports are evidence under a specific project state. They do not become current architecture merely because they were once authoritative.

### Research

Answers: **What external or exploratory information informed the work?**

Research does not silently become verified project behavior.

### Archive

Contains completed, superseded, abandoned, or historical plans, prompts, gates, and progress records. Imperative wording inside archived material is historical text, not execution authority.

### Former `active/` paths

At closure, several widely linked files still lived under `docs/active/`. Their paths are retained as **tombstones only** for link stability. They point to immutable pre-closure records and current closure authority. Nothing under `docs/active/` is an active plan after 2026-09-15.

## 4. Evidence and interpretation

Where materially important, distinguish:

- measured fact;
- direct observation;
- inference;
- hypothesis;
- unresolved question;
- project decision;
- actual implementation state;
- intended behavior;
- invalidated evidence.

Use [`closure/EVALUATION_STANDARD.md`](closure/EVALUATION_STANDARD.md) for the FSR-era evidence standard.

Conditional evidence stays conditional. Negative results stay visible. Invalid evidence may remain historically important but cannot support a stronger claim after invalidation.

## 5. Historical integrity

Never rewrite old plans or reports to manufacture hindsight.

Correct pattern:

```text
At the time: X was believed because of evidence A.
Experiment: B tested X.
Result: B contradicted or qualified X.
Decision: the project changed direction.
```

When a historical document contains stale authority language, prefer one of:

- leave it in an unmistakable archive context;
- add an archive index/tombstone;
- link to an immutable commit containing its exact former contents.

Do not silently edit a historical experiment so it appears to have predicted its later outcome.

## 6. Contradiction resolution

When documents conflict, start from:

```text
validated actual runtime behavior
        ↓
current code
        ↓
validated experiment provenance
        ↓
current Git state
        ↓
dated primary evidence
        ↓
current state / closure / reference docs
        ↓
historical plans
        ↓
research hypotheses / prompts / unverified narrative
```

This is a reasoning hierarchy, not a mechanical override. For project direction after 2026-09-15, the closure decision is explicit even though historical code remains executable.

## 7. Benchmark evidence

Raw benchmark/evidence systems remain authoritative for detailed measurements. Narrative documents should provide the conclusion, critical supporting numbers, scope/qualification, and a path to primary evidence rather than duplicating giant result tables.

## 8. Git history

Git history is evidence, not the sole user interface for understanding the project.

The closure deliberately uses immutable commit links for exact pre-closure active records. A clone with history retains those documents even where the working tree now contains closure tombstones.

## 9. Agent/contributor rule

A new agent or contributor must read:

1. [`../AGENTS.md`](../AGENTS.md);
2. [`README.md`](README.md);
3. [`current/STATE.md`](current/STATE.md);
4. [`closure/README.md`](closure/README.md).

No historical plan may be resumed merely because it contains instructions or unfinished gates. Behavioral code changes and reopening of FSR research require an explicit maintainer request.

## 10. Successor boundary

This documentation system governs the historical FSR-era repository only.

A successor Temporal Forge repository should establish its own current architecture and active-work authority. It may cite this repository as prior evidence, but it should not copy FSR-specific assumptions into its governing documentation unless independently justified.

## 11. Maintenance after closure

Permitted maintenance can include:

- correcting broken links;
- clarifying false or ambiguous historical claims;
- improving reproducibility without changing the historical conclusion;
- security or licensing/provenance corrections;
- evidence indexing;
- explicitly requested restoration or historical investigation.

Do not create a new standing FSR quality plan inside this repository by default.

The FSR era is closed; documentation maintenance should preserve that fact.
