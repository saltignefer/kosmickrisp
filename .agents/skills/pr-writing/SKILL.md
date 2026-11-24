---
name: pr-writing
description: Concise, unslopped technical writing rules for KosmicKrisp commit messages, release notes, PR descriptions, and docs.
disable-model-invocation: true
---

# PR and technical writing

Write so a tired graphics engineer understands on the first read.

- Lead with the root cause, mechanism, or measured result. Cut filler and chatbot phrasing.
- One thought per sentence; name exact files, NIR ops, Metal APIs, flags, and commands.
- Avoid em dashes, decorative emoji, abstract buzzwords, and speculative claims. Separate `measured` results from `inferred` hypotheses.
- Always include `Generated-by: LLM` (or `Assisted-by: LLM`) on Mesa commits and verify zero personal emails or local home paths appear in commits, docs, or release notes.
