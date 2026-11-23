/*
 * Copyright 2025 LunarG, Inc.
 * Copyright 2025 Google LLC
 * SPDX-License-Identifier: MIT
 */

#pragma once

#include "util/string_buffer.h"
#include "nir.h"

struct io_slot_info {
   nir_alu_type type;
   uint32_t interpolation;
   unsigned num_components;
   bool centroid;
   bool sample;
   bool uses_interpolant;
   /* Components holding two 16-bit values, see mesh_packed16_access */
   uint8_t packed16_mask;
};

struct nir_to_msl_ctx {
   FILE *output;
   struct hash_table *types;
   nir_shader *shader;
   struct _mesa_string_buffer *text;
   uint64_t disabled_workarounds;
   bool uses_per_draw_data;
   /* Mesh outputs are staged in device memory, see msl_mesh_device_staging */
   bool mesh_device_staging;
   /* 64-bit atomics on device or threadgroup memory, see kk_a64_device */
   bool atomic64_device;
   bool atomic64_shared;
   unsigned short indentlevel;
   struct io_slot_info inputs_info[NUM_TOTAL_VARYING_SLOTS];
   struct io_slot_info outputs_info[NUM_TOTAL_VARYING_SLOTS];
};

#define P_IND(ctx, ...)                                                        \
   do {                                                                        \
      for (unsigned i = 0; i < (ctx)->indentlevel; i++)                        \
         _mesa_string_buffer_append((ctx)->text, "    ");                      \
      _mesa_string_buffer_printf((ctx)->text, __VA_ARGS__);                    \
   } while (0);

#define P(ctx, ...) _mesa_string_buffer_printf((ctx)->text, __VA_ARGS__);

#define P_INDENT(ctx)                                                          \
   do {                                                                        \
      for (unsigned i = 0; i < (ctx)->indentlevel; i++)                        \
         _mesa_string_buffer_append((ctx)->text, "    ");                      \
   } while (0)

/* Perform type inference. The returned value is a
 * map from nir_def* to base type.*/

struct hash_table *msl_infer_types(nir_shader *shader);

const char *msl_type_for_def(struct hash_table *types, nir_def *def);

const char *msl_uint_type(uint8_t bit_size, uint8_t num_components);

const char *msl_type_for_src(struct hash_table *types, nir_src *src);

const char *msl_bitcast_for_src(struct hash_table *types, nir_src *src);

void msl_src_as_const(struct nir_to_msl_ctx *ctx, nir_src *src);

void msl_emit_io_blocks(struct nir_to_msl_ctx *ctx, nir_shader *shader);

void msl_emit_output_var(struct nir_to_msl_ctx *ctx, nir_shader *shader);

void msl_gather_io_info(struct nir_to_msl_ctx *ctx,
                        struct io_slot_info *info_array_input,
                        struct io_slot_info *info_array_output);

void msl_input_name(struct nir_to_msl_ctx *ctx, unsigned location,
                    unsigned component);

void msl_output_name(struct nir_to_msl_ctx *ctx, unsigned location,
                     unsigned component, bool from_input);

nir_alu_type msl_varying_member_type(unsigned location, nir_alu_type type);

void msl_varying_member_type_name(struct nir_to_msl_ctx *ctx, unsigned location,
                                  nir_alu_type type, unsigned num_components);

uint64_t msl_mesh_vertex_outputs(const nir_shader *shader);

uint64_t msl_mesh_primitive_outputs(const nir_shader *shader);

unsigned msl_mesh_indices_per_primitive(const nir_shader *shader);

bool msl_mesh_needs_default_point_size(const nir_shader *shader);

bool msl_mesh_device_staging(struct nir_to_msl_ctx *ctx, nir_shader *shader);

void msl_mesh_output_name(struct nir_to_msl_ctx *ctx, unsigned location,
                          unsigned component);

uint32_t msl_input_num_components(struct nir_to_msl_ctx *ctx,
                                  uint32_t location);

uint32_t msl_output_num_components(struct nir_to_msl_ctx *ctx,
                                   uint32_t location);

bool msl_src_is_float(struct nir_to_msl_ctx *ctx, nir_src *src);
bool msl_def_is_sampler(struct nir_to_msl_ctx *ctx, nir_def *def);
bool msl_def_is_texture(struct nir_to_msl_ctx *ctx, nir_def *def);

void msl_nir_lower_subgroups(nir_shader *nir);

bool msl_nir_lower_algebraic_late(nir_shader *shader);
