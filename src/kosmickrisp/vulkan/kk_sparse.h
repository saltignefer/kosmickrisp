/*
 * Copyright 2026 The Mesa Authors
 * SPDX-License-Identifier: MIT
 */

#ifndef KK_SPARSE_H
#define KK_SPARSE_H 1

#include "kk_private.h"

#include "kosmickrisp/bridge/mtl_types.h"

struct kk_device;
struct kk_image_plane;
struct kk_queue;
struct vk_queue_submit;

/* Creates the placement sparse texture of a sparse image plane and lays out
 * its opaque address space */
VkResult kk_image_plane_init_sparse(struct kk_device *dev,
                                    struct kk_image_plane *plane);

VkSparseImageFormatProperties kk_sparse_image_format_properties(
   VkFormat format, VkSampleCountFlagBits samples, struct mtl_size tile_px);

VkResult kk_queue_sparse_init(struct kk_queue *queue);
void kk_queue_sparse_finish(struct kk_queue *queue);

/* Applies the submit's sparse binds on the queue timeline, after its waits */
void kk_queue_bind_sparse(struct kk_queue *queue,
                          const struct vk_queue_submit *submit);

#endif /* KK_SPARSE_H */
