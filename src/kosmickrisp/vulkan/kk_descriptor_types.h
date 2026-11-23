/*
 * Copyright © 2024 Collabora Ltd. and Red Hat Inc.
 * Copyright 2025 LunarG, Inc.
 * Copyright 2025 Google LLC
 * SPDX-License-Identifier: MIT
 */
#ifndef KK_DESCRIPTOR_TYPES
#define KK_DESCRIPTOR_TYPES 1

#include "kk_private.h"

/* TODO_KOSMICKRISP Reduce size to 32 bytes by moving border to a heap. */
struct kk_sampled_image_descriptor {
   uint64_t image_gpu_resource_id;
   uint16_t sampler_index;

   /* Negative if there is no border color, else the clamp=0 sampler index used
    * for custom border color emulation.
    */
   int16_t clamp_0_sampler_index_or_negative;

   uint16_t sampler_lod_bias_fp16;
   uint16_t sampler_lod_min_fp16;
   uint16_t sampler_lod_max_fp16;

   uint16_t image_min_lod_fp16;
   uint16_t image_min_lod_uint16;

   uint16_t pad_to_64_bits;
   uint32_t border[4];
   uint64_t pad_to_power_2[3];
};

static_assert(sizeof(struct kk_sampled_image_descriptor) == 64,
              "kk_sampled_image_descriptor has no holes");

struct kk_storage_image_descriptor {
   uint64_t image_gpu_resource_id;

   /* Sparse residency of the image, 0 if it has none, and the first level and
    * layer of the view in it. See kk_sparse_residency_table. */
   uint64_t residency_table_addr;
   uint32_t residency_base_level;
   uint32_t residency_base_layer;
   uint64_t pad_to_power_2;
};

static_assert(sizeof(struct kk_storage_image_descriptor) == 32,
              "kk_storage_image_descriptor has no holes");

/* Texel buffers have no residency, so they keep the short descriptor. Its
 * resource ID is where image intrinsics expect it. */
struct kk_texel_buffer_descriptor {
   uint64_t image_gpu_resource_id;
};

static_assert(sizeof(struct kk_texel_buffer_descriptor) == 8,
              "kk_texel_buffer_descriptor has no holes");
static_assert(offsetof(struct kk_texel_buffer_descriptor,
                       image_gpu_resource_id) ==
                 offsetof(struct kk_storage_image_descriptor,
                          image_gpu_resource_id),
              "image intrinsics load both the same way");

/* This has to match nir_address_format_64bit_bounded_global */
struct kk_buffer_address {
   uint64_t base_addr;
   uint32_t size;
   uint32_t zero; /* Must be zero! */
};

static_assert(sizeof(struct kk_buffer_address) == 16,
              "kk_buffer_address has no holes");

#endif /* KK_DESCRIPTOR_TYPES */
