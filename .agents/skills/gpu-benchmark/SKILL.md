---
name: gpu-benchmark
description: Vet any KosmicKrisp, Metal, or vkd3d-proton performance measurement before reporting or acting on it. Enforces limiter identification, cache/build parity, error counting, repeatability, and proof that GPU work ran.
disable-model-invocation: true
---

# GPU benchmark checklist

Use before reporting any FPS, pipeline-compile, memory-import, or shader speedup. Answer every check from a run, not code reading.

1. **Name the limiter:** profile separately (CPU vs Metal GPU vs NIR/MSL compile vs Rosetta vs fence wait).
2. **Verify parity and caches:** compare identical build flags and explicit cache states (`MESA_SHADER_CACHE_DISABLE=1 VKD3D_SHADER_CACHE_PATH=0` for cold-compile; pre-warmed both sides for steady-state FPS).
3. **Check arithmetic limits:** verify the claimed speedup does not exceed the profiled share of the changed phase.
4. **Count errors and skips:** report `ok / SKIPPED / FAILED / ABORT / TIMEOUT` and verify GPU output bytes or pixels are correct.
5. **Prove repeatability:** run at least 5 alternating A/B trials and report median and min..max range.
6. **Check end-to-end relevance:** pair any microbenchmark with its share of frame or test time.
7. **Prove the GPU work happened:** confirm command buffers committed, fences waited, outputs matched, and dead-code elimination did not drop the workload.

Lead with `faster`, `slower`, `no measurable difference`, or `inconclusive` (mandatory when limiter is unknown, caches differed, or checks 4 and 7 were skipped).
