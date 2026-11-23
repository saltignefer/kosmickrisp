/*
 * Copyright 2026 The Mesa Authors
 * SPDX-License-Identifier: MIT
 */

#ifndef KK_SPARSE_H
#define KK_SPARSE_H 1

#include "kk_private.h"

#include "kk_image_layout.h"

#include "kosmickrisp/bridge/mtl_types.h"

/* Storage image residency queries read this table instead of using Metal
 * sparse reads. Storage textures have shader write usage, which makes Metal
 * report residency of wrong tiles, ignoring the base level of views, and once
 * a shader contains any sparse operation, even one that never runs, its reads
 * of small mip levels of such textures fault or return garbage. So sampled
 * images of formats with atomics only get shader write usage with storage
 * usage, and sampled residency of images with storage usage stays wrong.
 * Sampled residency follows the filter of the sampler, so sparse gathers with
 * a nearest sampler only check the texel nearest to the coordinates.
 *
 * Sparse binds update a CPU copy, and the queue copies it to the table after
 * the waits of the bind, so work still running before the bind does not see
 * it. One bit per page of the opaque address space of the image, see
 * kk_sparse_image_layout, follows the header. */
struct kk_sparse_residency_table {
   uint32_t tile_width_px;
   uint32_t tile_height_px;
   uint32_t tail_level;
   uint32_t layer_pages;
   uint32_t last_page;
   uint32_t pad[3];
   /* First page of each level within a layer, up to tail_level included */
   uint32_t level_page[KK_MAX_MIP_LEVELS + 1];
   /* Tiles along X of each level before the tail */
   uint32_t level_tiles_x[KK_MAX_MIP_LEVELS];
   uint32_t resident[];
};

struct kk_device;
struct kk_image_plane;
struct kk_queue;
struct vk_queue_submit;

/* Creates the placement sparse texture of a sparse image plane and lays out
 * its opaque address space */
VkResult kk_image_plane_init_sparse(struct kk_device *dev,
                                    struct kk_image_plane *plane);

/* Allocates the residency table of a sparse residency image plane with
 * storage usage, see kk_sparse_residency_table */
VkResult kk_image_plane_init_residency_table(struct kk_device *dev,
                                             struct kk_image_plane *plane);

VkSparseImageFormatProperties kk_sparse_image_format_properties(
   VkFormat format, VkSampleCountFlagBits samples, struct mtl_size tile_px);

VkResult kk_queue_sparse_init(struct kk_queue *queue);
void kk_queue_sparse_finish(struct kk_queue *queue);

struct kk_sparse_barrier;

/* Allocates what the sparse binds of the submit need, NULL without binds */
VkResult kk_queue_prepare_sparse(struct kk_queue *queue,
                                 const struct vk_queue_submit *submit,
                                 struct kk_sparse_barrier **barrier_out);

/* Applies the submit's sparse binds on the queue timeline, after its waits */
void kk_queue_bind_sparse(struct kk_queue *queue,
                          const struct vk_queue_submit *submit,
                          struct kk_sparse_barrier *barrier);

#endif /* KK_SPARSE_H */
