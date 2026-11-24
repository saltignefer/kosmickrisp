---
name: kk-xcl-verify
description: Build and verify KosmicKrisp Vulkan-on-Metal ICD (x86_64 CrossOver + arm64) against vkd3d-proton 3.1 and XCL GPU tests on Apple Silicon. Use when changing src/kosmickrisp/ compiler, memory import, mesh shader, or Wine shim code.
---

# KosmicKrisp x86_64 + XCL verification

Verify every `src/kosmickrisp/` change against real Metal execution before committing. Compiling alone proves nothing.

## 1. Hygiene gate and dual-arch build

Run the preflight script to enforce Mesa AI rules, fork privacy, and clean Homebrew LLVM 23 builds (`unset SDKROOT` + venv `PATH`):

```sh
python3 scripts/preflight_kk.py --check-hygiene --build --install
```

Completion criterion: `hygiene ok`, both `x86_64` and `arm64` `libvulkan_kosmickrisp.dylib` targets link with exit code 0, and `x86_64` is installed into CrossOver Preview.

## 2. Clear stale caches and check history

Before editing an existing NIR lowering or Metal workaround, check why it exists (`git log -S <symbol> -p -n 3`).

Pass `MESA_SHADER_CACHE_DISABLE=1 VKD3D_SHADER_CACHE_PATH=0` during compiler verification so old SPIR-V/MSL pipelines are never replayed.

## 3. Red-to-green targeted check, premise test, then full suite

1. Run the exact failing XCL test first with `--exact --nocapture` and `KK_SHIM_VKD3D_COMPAT=1 VKD3D_FEATURE_LEVEL=12_1 VKD3D_SHADER_MODEL=6_6`.
2. Inspect generated MSL with `MESA_KK_DEBUG=msl` whenever a shader test fails (never guess what NIR/DXIL emitted).
3. **Attack the premise:** when two fixes sharing one premise fail the same gate, stop. Write the premise down and run a 15-line standalone Metal/`arch -x86_64` probe before writing a third fix.
4. **Test behavior, not implementation:** verify the test fails when the fix is reverted and keeps passing across internal refactors.
5. Run the full XCL GPU suite (`scripts/preflight_macos.py --suite`) and compare `ok / SKIPPED / FAILED / ABORT` against the previous baseline. A change that lowers `ok` or introduces an abort is a regression.

Completion criterion: target test flips red -> green, full suite has zero regressions, and reported counts exclude `SKIPPED` tests.

## 4. Commit and release hygiene

- Author: `saltis <127887626+saltignefer@users.noreply.github.com>`.
- Include `Generated-by: LLM` trailer on Mesa commits.
- Never open upstream Mesa MRs or push personal paths/emails; audit release tarballs with `strings` before `gh release upload`.
