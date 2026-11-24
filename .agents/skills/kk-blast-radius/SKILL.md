---
name: kk-blast-radius
description: Find what a KosmicKrisp compiler, Metal4 bridge, sparse, or host-memory import change could break across shader stages or Wine/Rosetta callers, and prove the core safety fact by running real GPU tests.
disable-model-invocation: true
---

# KosmicKrisp blast radius

Find what a change in `src/kosmickrisp/` breaks outside its immediate diff before shipping.

## Proof ladder

State the one or two facts the change's safety depends on and push each as far down this ladder as is cheap:

1. Claimed in prose (worthless alone).
2. Pointed to exact `file:line` in Mesa/KosmicKrisp or Metal docs.
3. Traced the failure path step by step.
4. Verified with a real test or standalone `arch -x86_64` Metal probe.
5. Verified on the full XCL GPU suite under CrossOver + `vkd3d-proton 3.1`.

## Where grep stops in KosmicKrisp

- **Cross-stage NIR / MSL lowering:** a change in `msl_nir_lower_*` or `nir_to_msl.c` can fix CS while breaking VS, PS, or MS (e.g., Metal vertex shaders disallow subgroup intrinsics; FP64 `nir_lower_double_ops` flushes `1.0 / NaN` unless `nir_fp_preserve_nan` is set). Always inspect generated MSL with `MESA_KK_DEBUG=msl` and `MESA_SHADER_CACHE_DISABLE=1`.
- **Rosetta 2 / XNU VM mappings:** `kk_device_memory.c` host import must handle Wine `MEM_WRITE_WATCH` 4 KiB splits and COW (`shared=1`) mappings while keeping the exact VA and live CPU-GPU coherence.
- **Metal 4 residency and sparse bindings:** `kk_sparse.c`, `kk_bo.c`, and `kk_buffer.c` require `MTLHeapTypePlacement` and `MTLSparsePageSize64` alignment plus `kk_queue_sparse_barrier` ordering.
- **Dual-arch linkage:** both `x86_64` (CrossOver) and `arm64` (native macOS) must compile cleanly with Homebrew LLVM 23 and `unset SDKROOT`.

## Output

1. **What changed** (including behavior outside the diff).
2. **Core safety fact** (ladder step + exact command/output).
3. **Confirmed risks** (`file:line`, failure mode, repro).
4. **Checked and cleared** (boundaries verified safe).
5. **Before merge** (cheapest test or probe to keep).
