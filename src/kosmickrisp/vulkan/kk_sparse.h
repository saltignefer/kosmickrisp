/*
 * Copyright 2026 The Mesa Authors
 * SPDX-License-Identifier: MIT
 */

#ifndef KK_SPARSE_H
#define KK_SPARSE_H 1

#include "kk_private.h"

struct kk_queue;
struct vk_queue_submit;

VkResult kk_queue_sparse_init(struct kk_queue *queue);
void kk_queue_sparse_finish(struct kk_queue *queue);

/* Applies the submit's sparse binds on the queue timeline, after its waits */
void kk_queue_bind_sparse(struct kk_queue *queue,
                          const struct vk_queue_submit *submit);

#endif /* KK_SPARSE_H */
