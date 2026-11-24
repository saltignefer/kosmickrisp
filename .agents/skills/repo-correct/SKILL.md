---
name: repo-correct
description: Turn repeated agent mistakes in saltignefer/kosmickrisp into permanent structural checks (architecture -> preflight_kk.py/CI -> tests -> AGENTS.md Rule -> Enforcer).
disable-model-invocation: true
---

# Repo correct

Assume every future contributor is an agent that opens only two files and stops once `ninja` exits 0. Eliminate repeated mistake classes at the highest level that holds:

1. **Architecture:** one owner per Metal resource/lowering invariant, delete stale paths agents might copy.
2. **Preflight or CI:** fail `scripts/preflight_kk.py` and `.github/workflows/fork-hygiene.yml` with an error naming the exact fix (e.g., missing `Generated-by: LLM`, forbidden Mesa dir, missing `unset SDKROOT`, leaked personal path).
3. **Behavioral tests:** add a targeted XCL GPU test or Metal probe that fails when the bug returns and survives internal refactors.
4. **Docs / `AGENTS.md` last:** keep the `Rule -> Enforcer` table current; escalate any rule that gets broken twice instead of rewording prose.
