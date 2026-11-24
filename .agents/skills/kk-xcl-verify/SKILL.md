---
name: kk-xcl-verify
description: Build and verify KosmicKrisp Vulkan-on-Metal ICD (x86_64 CrossOver + arm64) against vkd3d-proton 3.1 and XCL GPU tests on Apple Silicon. Use when changing src/kosmickrisp/ compiler, memory import, mesh shader, or Wine shim code.
---

# KosmicKrisp x86_64 + XCL verification

Verify every `src/kosmickrisp/` change against real Metal execution before committing. Compiling alone proves nothing.

## 1. Build both targets

Always build with Homebrew LLVM (`clang` 23) and unset `SDKROOT` so `<endian.h>` resolves cleanly:

```sh
export PATH="/opt/homebrew/opt/llvm/bin:/opt/homebrew/bin:/usr/bin:/bin:/usr/sbin:/sbin"
unset SDKROOT
ninja -C ../mesa-ws/build-x86 src/kosmickrisp/vulkan/libvulkan_kosmickrisp.dylib
ninja -C ../mesa-ws/build src/kosmickrisp/vulkan/libvulkan_kosmickrisp.dylib
```

Completion criterion: both `libvulkan_kosmickrisp.dylib` targets link with exit code 0.

## 2. Install into CrossOver Preview and clear stale caches

Shader and pipeline caches hide compiler fixes. Clear caches or disable them when testing compiler changes:

```sh
cp -p ../mesa-ws/build-x86/src/kosmickrisp/vulkan/libvulkan_kosmickrisp.dylib \
  "/Applications/CrossOver Preview.app/Contents/SharedSupport/CrossOver/lib/x86_64/"
```

Pass `MESA_SHADER_CACHE_DISABLE=1 VKD3D_SHADER_CACHE_PATH=0` during compiler verification so old SPIR-V/MSL pipelines are never replayed.

## 3. Run targeted red-to-green check, then full suite

1. Run the exact failing test(s) first with `--exact --nocapture` and `KK_SHIM_VKD3D_COMPAT=1 VKD3D_FEATURE_LEVEL=12_1 VKD3D_SHADER_MODEL=6_6`.
2. Inspect generated MSL with `MESA_KK_DEBUG=msl` whenever a shader test fails (never guess what NIR/DXIL emitted).
3. Run the full XCL GPU suite (`scripts/preflight_macos.py --suite`) and compare `ok / FAILED / ABORT / SKIPPED` against the previous baseline. A change that lowers `ok` or introduces an abort is a regression.

Completion criterion: target test flips red -> green, full suite has zero regressions, and reported counts exclude `SKIPPED` tests.

## 4. Commit and release hygiene

- Author: `saltis <127887626+saltignefer@users.noreply.github.com>`.
- Include `Generated-by: LLM` trailer on Mesa commits.
- Never open upstream Mesa MRs or push personal paths/emails; audit release tarballs with `strings` before `gh release upload`.
