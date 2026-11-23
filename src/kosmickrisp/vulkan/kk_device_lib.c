/*
 * Copyright 2025 LunarG, Inc.
 * Copyright 2025 Google LLC
 * SPDX-License-Identifier: MIT
 */

#include "kk_cmd_buffer.h"
#include "kk_device.h"
#include "kk_physical_device.h"
#include "kk_shader.h"

#include "kosmickrisp/clc/kk_precompiled_shader.h"

#include "libkk_shaders.h"

#include "kosmickrisp/bridge/mtl_bridge.h"

#include "nir/nir.h"
#include "nir/nir_builder.h"

static VkResult
build_precompiled_shaders(struct kk_device *dev)
{
   struct kk_physical_device *pdev = kk_device_physical(dev);

   uint32_t i = 0u;
   for (; i < LIBKK_NUM_PROGRAMS; ++i) {
      const uint32_t *bin = libkk_AppleSilicon[i];
      const struct kk_precompiled_info *info = (void *)bin;
      const char *msl = (const char *)bin + sizeof(*info);

      mtl_library *library = mtl_new_library(
         dev->mtl_compiler_handle, msl, pdev->info.msl_version,
         MTL_MATH_MODE_FAST, MTL_MATH_FLOATING_POINT_FUNCTIONS_FAST);
      if (library == NULL)
         goto fail;

      struct kk_precompiled_shader *shader = &dev->precompiled_cache.shaders[i];

      /* TODO_KOSMICKRISP Do not hardcode the entrypoint */
      mtl_function_descriptor *function =
         mtl_new_library_function_descriptor(library, "main_entrypoint");
      uint32_t local_size_threads = info->workgroup_size[0] *
                                    info->workgroup_size[1] *
                                    info->workgroup_size[2];
      shader->pipeline = mtl_new_compute_pipeline_state(
         dev->mtl_compiler_handle, function, local_size_threads);
      mtl_release(function);
      mtl_release(library);

      if (!shader->pipeline)
         goto fail;

      memcpy(&shader->info, info, sizeof(*info));
   }

   return VK_SUCCESS;

fail:
   for (uint32_t j = 0u; j < i; ++j)
      mtl_release(dev->precompiled_cache.shaders[j].pipeline);
   return vk_error(dev, VK_ERROR_INVALID_SHADER_NV);
}

/* Object stage of mesh pipelines without task shader, see
 * KK_MESH_GRID_MAX_CHUNKS. Threadgroup k reads the group counts through the
 * per-draw data, which points at the VkDrawMeshTasksIndirectCommandEXT, and
 * launches chunk k. The %u are the index of mesh_grid_addr in the per-draw
 * data, the chunk size and the payload offsets.
 */
static const char mesh_grid_object_msl[] =
   "#include <metal_stdlib>\n"
   "using namespace metal;\n"
   "struct Buffer { uint64_t contents[1]; };\n"
   "[[object]] void kk_mesh_grid_object(\n"
   "    object_data uchar *kk_payload [[payload]],\n"
   "    mesh_grid_properties kk_mesh_grid,\n"
   "    uint3 kk_chunk [[threadgroup_position_in_grid]],\n"
   "    constant Buffer &per_draw [[buffer(2)]])\n"
   "{\n"
   "    uint3 n = uint3(*(const device packed_uint3 *)per_draw.contents[%u]);\n"
   "    const uint chunk_groups = %uu;\n"
   "    uint k = kk_chunk.x;\n"
   "    uint3 grid = uint3(0u);\n"
   "    uint base_y = 0u, base_z = 0u;\n"
   "    if (n.x != 0u && n.y != 0u && n.z != 0u) {\n"
   "        ulong slice = ulong(n.x) * n.y;\n"
   "        if (slice <= chunk_groups) {\n"
   "            uint slices = min(uint(chunk_groups / slice), 65535u);\n"
   "            base_z = k * slices;\n"
   "            if (base_z < n.z)\n"
   "                grid = uint3(n.x, n.y, min(slices, n.z - base_z));\n"
   "        } else {\n"
   "            uint rows = max(chunk_groups / n.x, 1u);\n"
   "            uint chunks_per_slice = (n.y + rows - 1u) / rows;\n"
   "            base_z = k / chunks_per_slice;\n"
   "            base_y = (k %% chunks_per_slice) * rows;\n"
   "            if (base_z < n.z)\n"
   "                grid = uint3(n.x, min(rows, n.y - base_y), 1u);\n"
   "        }\n"
   "    }\n"
   "    *(object_data uint *)&kk_payload[%u] = base_y;\n"
   "    *(object_data uint *)&kk_payload[%u] = base_z;\n"
   "    *(object_data packed_uint3 *)&kk_payload[%u] = n;\n"
   "    kk_mesh_grid.set_threadgroups_per_grid(grid);\n"
   "}\n";

static VkResult
build_mesh_grid_object(struct kk_device *dev)
{
   struct kk_physical_device *pdev = kk_device_physical(dev);

   char msl[sizeof(mesh_grid_object_msl) + 64];
   ASSERTED int len = snprintf(
      msl, sizeof(msl), mesh_grid_object_msl,
      (unsigned)(offsetof(struct kk_per_draw_data, mesh_grid_addr) / 8u),
      KK_MESH_GRID_CHUNK_GROUPS, KK_MESH_GRID_PAYLOAD_BASE,
      KK_MESH_GRID_PAYLOAD_BASE + 4u, KK_MESH_GRID_PAYLOAD_COUNTS);
   assert(len > 0 && len < sizeof(msl));

   dev->mesh_grid_object = mtl_new_library(
      dev->mtl_compiler_handle, msl, pdev->info.msl_version, MTL_MATH_MODE_FAST,
      MTL_MATH_FLOATING_POINT_FUNCTIONS_FAST);
   return dev->mesh_grid_object ? VK_SUCCESS
                                : vk_error(dev, VK_ERROR_INVALID_SHADER_NV);
}

VkResult
kk_device_init_lib(struct kk_device *dev)
{
   VkResult result = build_precompiled_shaders(dev);
   if (result != VK_SUCCESS)
      return result;

   if (dev->vk.enabled_features.meshShader) {
      result = build_mesh_grid_object(dev);
      if (result != VK_SUCCESS)
         kk_device_finish_lib(dev);
   }

   return result;
}

void
kk_device_finish_lib(struct kk_device *dev)
{
   for (uint32_t i = 0; i < LIBKK_NUM_PROGRAMS; ++i)
      mtl_release(dev->precompiled_cache.shaders[i].pipeline);
   if (dev->mesh_grid_object) {
      mtl_release(dev->mesh_grid_object);
      dev->mesh_grid_object = NULL;
   }
}
