---
name: show-me-your-work
description: Keep an append-only TSV decision log for long-running or multi-phase KosmicKrisp debugging sessions so reviewers can audit every hypothesis, probe, and verification quickly.
disable-model-invocation: true
---

# Show me your work

Keep one canonical append-only TSV log (`decisions.tsv`, local by default and untracked unless a reviewer needs it committed).

```tsv
ts	phase	decision	why	evidence	result
```

- Single-line tab-separated cells; `evidence` is a pointer (`commit SHA`, `file:line`, `MESA_KK_DEBUG=msl` dump path, test JSON), never prose.
- Log decision points (approach chosen, probe result, pivot/revert with trigger, verified unit), not every shell command.
- Append-only: supersede wrong calls with a new row instead of rewriting history.
- Before handback, audit rows against actual runs, strip any local paths/personal info before sharing, and use cross-model review for high-stakes runs.
