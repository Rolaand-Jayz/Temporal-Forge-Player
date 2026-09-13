# Forge's Final Word — Gate 1A provenance lock

**Date:** 2026-09-13
**Checkpoint:** FFW-CP2
**Campaign branch:** `forge-final-word-campaign`
**Source baseline:** `6b28ae1abe7ab5d7ccdfcdcebf1985014aeb175e`

## Decision

**TEMPORAL FORGE FSR4 RESEARCH CONCLUDED — parity claim cannot be established.**

The strongest defensible chain identifies the active Forge implementation and
its generated native INT8 assets, but no version-matched, usable AMD reference
runtime is available in this execution environment. Gate 1B is therefore not
started. No GT-A, GT-B, GT-C, temporal qualification, performance
qualification, or rescue branch is authorized after this terminal result.

## Provenance chain

| Link | Evidence | Status |
|---|---|---|
| Forge implementation | Source commit `6b28ae1abe7ab5d7ccdfcdcebf1985014aeb175e`; Gate-0 identity manifests and audit bundle | identified |
| Native model assets | `resources/fsr4/native_i8/`; `performance_2160/initializers.bin` SHA-256 `fe2ad00e9e7fc563bd9ee01ab72c82bb557bf0d8365a085e833749547e089aab`; `quality_1080/initializers.bin` SHA-256 `6fce93853235bbf8742210fb2bfd8653b70c90c722af17f5a37d1a3390156356` | identified |
| Model/HLSL generation source | `Rolaand-Jayz/fsr4-rdna3-optimization` commit `49015b72104eb19c1563db77af68ada9a6c254df` (remote `main` verified) | identified, reverse-engineered/generated |
| RE source | `Rolaand-Jayz/RE-of-FSR-4.1.0-Upscaling`; repository remote `main` currently resolves to `b75f3482bfae948e46e0569d0fe0c2b2bc2392b`; project records historical snapshot `5c1ff9a537e755c38e446f0b4eaa426a0000b3ed` | identified, static research |
| Exact upstream FSR runtime | No exact AMD provider/DLL provenance tied to the Forge captures; the RE source explicitly limits its claims to static analysis and reports runtime equivalence as unvalidated | unresolved |
| Local reference runtime | `amdxcffx64.dll` exists in Steam prefixes `0`, `1493710`, and `2180100`, all identical SHA-256 `4e7dc37aebea3a90e3d3cc43e24cb2b54176b2535315f20dbe63b3b7cfc56b1e`; PE32+ DLL strings identify `4.0.1`, not the required 4.1.0/4.1.1 target | version mismatch |
| Usable AMD oracle | No version-matched AMD capture/runtime was available for the required public-boundary parity capture | unavailable |

## Exact checks performed

```text
git ls-remote https://github.com/Rolaand-Jayz/RE-of-FSR-4.1.0-Upscaling.git refs/heads/main
  b75f3482bfae948e46e0569d0fe0c2b2bc2392b
git ls-remote https://github.com/Rolaand-Jayz/fsr4-rdna3-optimization.git refs/heads/main
  49015b72104eb19c1563db77af68ada9a6c254df
find /home/rolaandjayz/.local/share/Steam ... -name 'amdxcffx64.dll'
  prefixes 0, 1493710, 2180100; identical SHA-256; embedded version string 4.0.1
```

The local project provenance is recorded in `PROVENANCE.md`. The Gate-0
package remains at `benchmarks/gate0/audit/20260913/` and retains all R1–R6,
terminal-determinism, anti-sharpening, and legitimate-acceptance evidence.

## Uncertainty and terminal boundary

The Forge-to-generated-pack relationship is reproducible, but it does not
establish functional parity with AMD’s proprietary runtime. A 4.0.1 DLL is
not silently substituted for a 4.1.x oracle. Obtaining a matching runtime or
native AMD capture would require external state/capability beyond this
bounded terminal campaign and would change the result from a provenance lock
to renewed binary/runtime archaeology.

## Reproduction and hashes

- Baseline commit: `6b28ae1abe7ab5d7ccdfcdcebf1985014aeb175e`
- `PROVENANCE.md`: `fbf0f8b5a42af0a87352bd85da99db3bd6da001fed1ab3f156cb20769315ae5b`
- Gate-0 audit index: `benchmarks/gate0/audit/20260913/audit_index.json`
- Gate-0 evidence report: `docs/reports/20260913_FFW_GATE0_EVIDENCE_REPORT.md`
