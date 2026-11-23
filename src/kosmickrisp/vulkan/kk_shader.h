/*
 * Copyright © 2022 Collabora Ltd. and Red Hat Inc.
 * Copyright 2025 LunarG, Inc.
 * Copyright 2025 Google LLC
 * SPDX-License-Identifier: MIT
 */

#ifndef KK_SHADER_H
#define KK_SHADER_H 1

#include "kk_device_memory.h"
#include "kk_private.h"

#include "kosmickrisp/bridge/mtl_format.h"

#include "poly/nir/poly_nir.h"

#include "vk_pipeline_cache.h"

#include "vk_shader.h"

struct kk_cmd_buffer;

struct kk_tess_info {
   enum tess_primitive_mode mode : 8;
   enum gl_tess_spacing spacing  : 8;
   bool points;
   bool ccw;
};
static_assert(sizeof(struct kk_tess_info) == 4, "packed");

static struct kk_tess_info
kk_tess_info_merge(struct kk_tess_info a, struct kk_tess_info b)
{
   static_assert(TESS_PRIMITIVE_UNSPECIFIED == 0, "zero state");
   static_assert(TESS_SPACING_UNSPECIFIED == 0, "zero state");

   /* Just merge by OR'ing the raw bits */
   uint32_t x, y;
   memcpy(&x, &a, sizeof(x));
   memcpy(&y, &b, sizeof(y));

   x |= y;

   struct kk_tess_info out;
   memcpy(&out, &x, sizeof(out));
   return out;
}

/* Metal silently drops a mesh grid when a dimension exceeds 65535 or
 * bits(x) + bits(y) + bits(z) > 22, with bits(n) = floor(log2(n)) + 1, from a
 * draw without object stage and from an object function alike. Grids of fewer
 * than 2^20 threadgroups always run, and the object grid of a draw has no such
 * limit. Mesh pipelines without task shader therefore get a synthetic object
 * stage, see kk_device_lib.c, whose threadgroup k launches chunk k of the
 * grid: whole Z slices when an XY slice fits in a chunk, else a range of rows
 * of one slice. Chunks follow the linear workgroup order, which keeps the
 * primitive order, and a grid of up to 2^22 workgroups needs no more than 8 of
 * them. The mesh shader adds the chunk base to its workgroup ID and reads
 * NumWorkGroups from the payload, see kk_nir_lower_mesh_grid.
 */
#define KK_MESH_GRID_MAX_CHUNKS   8u
#define KK_MESH_GRID_CHUNK_GROUPS ((1u << 20) - 1u)

/* Synthetic payload: uint base_y, uint base_z, packed_uint3 group counts */
#define KK_MESH_GRID_PAYLOAD_BASE   0u
#define KK_MESH_GRID_PAYLOAD_COUNTS 8u
#define KK_MESH_GRID_PAYLOAD_SIZE   32u

/* Chunks of a grid, must match the synthetic object stage. Grids beyond the
 * limits get the chunks of the largest grid. */
static inline uint32_t
kk_mesh_grid_chunks(uint32_t x, uint32_t y, uint32_t z)
{
   if (x == 0u || y == 0u || z == 0u)
      return 0u;

   uint64_t slice = (uint64_t)x * y;
   uint64_t chunks;
   if (slice <= KK_MESH_GRID_CHUNK_GROUPS) {
      uint32_t slices =
         MIN2(KK_MESH_GRID_CHUNK_GROUPS / (uint32_t)slice, 65535u);
      chunks = DIV_ROUND_UP(z, slices);
   } else {
      uint32_t rows = MAX2(KK_MESH_GRID_CHUNK_GROUPS / x, 1u);
      chunks = (uint64_t)z * DIV_ROUND_UP(y, rows);
   }
   return MIN2(chunks, KK_MESH_GRID_MAX_CHUNKS);
}

struct kk_shader_info {
   mesa_shader_stage stage;
   bool uses_per_draw_data;

   /* Required for fragment shader cull distance discards. */
   uint8_t num_cull_distances;

   /* Outputs a VS, or a TES followed by a GS, writes for the next stage */
   uint64_t outputs_written;

   /* The first stage of a graphics pipeline (vertex, task or mesh) holds the
    * Metal pipeline and the vs data below. */
   bool holds_pipeline;

   /* Task and mesh stages. The pipeline holder carries the data of both. */
   struct {
      struct mtl_size task_local_size;
      struct mtl_size mesh_local_size;
      /* Primitive type the mesh shader outputs */
      enum mesa_prim primitive_type;
      /* Bytes of payload the task shader writes */
      uint32_t payload_size;
   } mesh;

   union {
      /* Pipeline holder data, see holds_pipeline. */
      struct {
         /* Required to know which extra states does the pipeline contain for
          * serialization. */
         uint32_t additional_stages_bits;

         uint32_t tess_local_thread_size;

         /* Data needed to start render pass and bind pipeline. */
         uint32_t attribs_read;
         uint32_t sample_count;

         /* Data needed for serialization. */
         enum mtl_primitive_topology_class topology;
         enum mtl_pixel_format rt_formats[MAX_DRAW_BUFFERS];
         enum mtl_pixel_format d_format;
         enum mtl_pixel_format s_format;
         uint32_t view_mask;
         VkCompareOp depth_compare_op;
         struct {
            uint8_t depth_fail;
            uint8_t fail;
            uint8_t pass;
            uint8_t compare;
            uint8_t compare_mask;
            uint8_t write_mask;
         } stencil_front, stencil_back;
         uint8_t color_attachment_count;
         bool has_ms;
         bool has_ds;
         bool has_depth_write;
         bool has_stencil_test;
      } vs;

      struct {
         uint64_t tcs_per_vertex_outputs;
         uint32_t tcs_output_stride;
         uint8_t tcs_output_patch_size;
         uint8_t tcs_nr_patch_outputs;

         struct kk_tess_info info;
      } tess;

      struct poly_gs_info gs;

      struct {
         bool uses_flat_varyings;
      } fs;

      struct {
         struct mtl_size local_size;
      } cs;
   };
};

/* Threadgroup size the VS, TES and GS compute passes are compiled for and
 * dispatched with */
#define KK_POLY_WG_SIZE 64u

/* Metal handles for binding. */
struct kk_pipeline_handles {
   union {
      struct {
         mtl_render_pipeline_state *render;
         /* VS, TCS, TES and the GS compute pass at most run before the render
          * pipeline. */
         mtl_compute_pipeline_state *pre_render[4];
         mtl_depth_stencil_state *ds_handle;
         uint32_t pre_render_count;
      } gfx;
      mtl_compute_pipeline_state *cs;
   };
};

struct msl_compile_data {
   char *code;
   char *entrypoint_name;
};

struct kk_shader {
   struct vk_shader vk;

   struct kk_pipeline_handles pipeline;
   struct kk_shader_info info;
   struct msl_compile_data msl_data[MESA_SHADER_MESH_STAGES];
   /* Compute pass of a GS. Its rasterization VS is in
    * msl_data[MESA_SHADER_GEOMETRY]. */
   struct msl_compile_data gs_main;
};

VK_DEFINE_NONDISP_HANDLE_CASTS(kk_shader, vk.base, VkShaderEXT,
                               VK_OBJECT_TYPE_SHADER_EXT);

extern const struct vk_device_shader_ops kk_device_shader_ops;

static inline nir_address_format
kk_buffer_addr_format(VkPipelineRobustnessBufferBehaviorEXT robustness)
{
   switch (robustness) {
   case VK_PIPELINE_ROBUSTNESS_BUFFER_BEHAVIOR_DISABLED:
      return nir_address_format_64bit_global_32bit_offset;
   case VK_PIPELINE_ROBUSTNESS_BUFFER_BEHAVIOR_ROBUST_BUFFER_ACCESS:
   case VK_PIPELINE_ROBUSTNESS_BUFFER_BEHAVIOR_ROBUST_BUFFER_ACCESS_2:
      return nir_address_format_64bit_bounded_global;
   default:
      UNREACHABLE("Invalid robust buffer access behavior");
   }
}

bool
kk_nir_lower_descriptors(nir_shader *nir,
                         const struct vk_pipeline_robustness_state *rs,
                         uint32_t set_layout_count,
                         struct vk_descriptor_set_layout *const *set_layouts);

/* KK_WORKAROUND_18 */
bool kk_nir_add_device_barrier_workaround(nir_shader *nir);

bool kk_nir_lower_poly(struct nir_shader *nir);
bool kk_nir_lower_mesh_queries(nir_shader *nir);
bool kk_nir_lower_mesh_grid(nir_shader *nir);
bool kk_nir_lower_null_images(nir_shader *nir);

bool kk_nir_lower_textures(nir_shader *nir);

bool kk_nir_lower_vs_multiview(nir_shader *nir, uint32_t view_mask);
bool kk_nir_lower_mesh_multiview(nir_shader *nir, uint32_t view_mask);
bool kk_nir_lower_fs_multiview(nir_shader *nir, uint32_t view_mask);

/* Indicates that the sampler should be overridden to clamp to 0 instead of 1 */
#define KK_TEXTURE_FLAG_CLAMP_TO_0 (1 << 0)

bool kk_nir_lower_custom_border(nir_shader *nir);
bool kk_nir_lower_image_view_min_lod(nir_shader *nir);

VkResult kk_compile_nir_shader(struct kk_device *dev, nir_shader *nir,
                               const VkAllocationCallbacks *alloc,
                               struct kk_shader **shader_out);

void kk_cmd_bind_compute_shader(struct kk_cmd_buffer *cmd,
                                struct kk_shader *shader);

#endif /* KK_SHADER_H */
