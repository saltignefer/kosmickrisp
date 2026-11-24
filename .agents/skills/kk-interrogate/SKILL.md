---
name: kk-interrogate
description: Adversarial code review for KosmicKrisp changes where only findings proven by running real code, MSL dumps, or GPU tests survive. Drops style nits and unproven speculation.
disable-model-invocation: true
---

# KosmicKrisp interrogate (adversarial review)

Try to break the change. Only findings proven by a real run survive. Report findings without editing code during review.

## Rules of attack

1. Restate the change intent in one paragraph first.
2. Prove every finding with a real command and output: a failing XCL GPU test, a `MESA_KK_DEBUG=msl` dump showing invalid Metal, a `ninja` build failure on `x86_64` or `arm64`, or a standalone Metal/XNU probe.
3. Drop everything you did not run (style nits, naming preferences, speculative risks).
4. Rerun the exact command to confirm each finding violates the stated intent.
5. "No verified findings" is a valid result.

## Output

- **Intent:** one-paragraph restatement.
- **Verified findings:** title, `file:line`, command, actual output, expected behavior.
- **Dropped:** count of unproven candidates discarded and why.
