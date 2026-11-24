---
name: kk-architect
description: Sketch C/ObjC boundaries, Metal4 object ownership, and NIR lowering invariants before coding non-trivial changes in src/kosmickrisp/.
disable-model-invocation: true
---

# KosmicKrisp architect

Before coding a non-trivial change in `src/kosmickrisp/`, trace every layer touched (NIR compiler, Vulkan frontend, Objective-C Metal4 bridge) and sketch **two materially different shapes**.

## Checklist before picking a shape

1. **Right layer:** fix compiler semantics in NIR/MSL (`src/kosmickrisp/compiler/`), Vulkan contract in `vulkan/`, and Metal4 API calls in `bridge/` — never hide a compiler bug with a runtime hack or vice versa.
2. **Memory and Metal lifetime ownership:** name who retains/releases every `MTLBuffer`, `MTLHeap`, `MTL4CommandBuffer`, and residency set entry; ensure failure paths clean up with zero leaks.
3. **Cross-stage invariants:** verify how VS, PS, CS, and MS each interact with the new pass or descriptor layout.
4. **Upstream cleanliness:** keep diffs scoped to `src/kosmickrisp/` (never touch AI-restricted Mesa directories) and avoid breaking `arm64` native builds while fixing `x86_64` CrossOver paths.

Pick the simpler shape, write a 3-5 line tradeoff note against the rejected shape, and implement in atomic verifiable commits.
