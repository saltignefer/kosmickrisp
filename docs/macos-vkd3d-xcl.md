# KosmicKrisp + vkd3d-proton 3.1 on macOS (Apple Silicon)

Notes for running D3D12 SM 6.6 / Xbox Series X (XCL) workloads on Apple Silicon via **CrossOver / Wine + vkd3d-proton 3.1 + KosmicKrisp**.

## Runtime requirements

1. **CrossOver / Wine bottle (`win10_64`)**:
   - Place `vkd3d-proton 3.1` `d3d12.dll` and `d3d12core.dll` in `drive_c/windows/system32/`.
   - Set bottle registry `HKCU\Software\Wine\DllOverrides`: `d3d12=native`, `d3d12core=native`, `dxgi=builtin` (DXVK 3.1.1 currently requires `fillModeNonSolid = true`, which KosmicKrisp reports `false`).
2. **KosmicKrisp ICD + `libMoltenVK.dylib` shim**:
   - Install `libvulkan_kosmickrisp.dylib` and the thin `libMoltenVK.dylib` shim into `CrossOver.app/Contents/SharedSupport/CrossOver/lib/x86_64/`.
   - The shim exports `vkCreateMetalSurfaceEXT` (required by `winemac.so` `macdrv_VulkanInit` to advertise `VK_KHR_win32_surface`) and sets `MESA_KK_EXPERIMENTAL=mesh_shader,sparse,atomic_int64,custom_border,image_view_min_lod`.
3. **Environment variables**:
   ```sh
   KK_SHIM_VKD3D_COMPAT=1
   VKD3D_FEATURE_LEVEL=12_1
   VKD3D_SHADER_MODEL=6_6
   ```
   `KK_SHIM_VKD3D_COMPAT=1` bridges three `vkd3d-proton 3.1` init gates (`VK_EXT_transform_feedback` queries, single-texel buffer offset alignment, and the UE5 `VK_KHR_fragment_shader_barycentric` gate that otherwise disables `VK_EXT_mesh_shader`).

## Key macOS / Rosetta 2 fixes in this fork

- **`VK_EXT_mesh_shader` on Metal 3 mesh/object shaders**: enables Xbox NGG mesh pipelines (`gpu_mesh_mode_two_passes`, `gpu_mesh_mode_reads_memory_windows`).
- **FP64 software library (`shaderFloat64`) + `nir_fp_preserve_nan` on `fdiv`/`frcp`/`frsq`**: preserves IEEE NaN for `1.0 / NaN` and `rsq(x < 0)` while keeping `rsq(-0.0) = -inf`.
- **Vertex-shader 1-lane subgroup lowering (`msl_nir_lower_vs_subgroups`)**: Metal MSL forbids `[[thread_index_in_simdgroup]]` and `simd_*` in `vertex` functions; native (non-emulated) VS subgroup ops lower to a valid 1-lane fallback while compute/fragment/emulated stages keep 32-lane hardware simdgroups.
- **Rosetta 2 `MEM_WRITE_WATCH` UMA import (`kk_AllocateMemory`)**: 4 KiB write-watch faults split 16 KiB-aligned mappings into uncoalesceable 4 KiB `vm_map_entry` regions that `newBufferWithBytesNoCopy:` rejects. On failure, KosmicKrisp verifies the range is readable and writable-max, defragments it in place at the exact same VA via `mach_vm_remap(VM_FLAGS_FIXED | VM_FLAGS_OVERWRITE)`, and creates a zero-copy coherent `MTLBuffer`.
