/*
 * Copyright 2026 The Mesa Authors
 * SPDX-License-Identifier: MIT
 */

#include "kk_sparse.h"

#include "kk_buffer.h"
#include "kk_device.h"
#include "kk_device_memory.h"
#include "kk_queue.h"

#include "kosmickrisp/bridge/mtl_bridge.h"

#include "vk_queue.h"

VkResult
kk_queue_sparse_init(struct kk_queue *queue)
{
   struct kk_device *dev = kk_queue_device(queue);

   if (!dev->vk.enabled_features.sparseBinding)
      return VK_SUCCESS;

   queue->sparse_flush.heap =
      mtl_new_heap(dev->mtl_handle, MTL_SPARSE_PAGE_SIZE_B,
                   KK_MTL_RESOURCE_OPTIONS, true);
   queue->sparse_flush.buffer =
      mtl_new_placement_sparse_buffer(dev->mtl_handle, MTL_SPARSE_PAGE_SIZE_B);
   if (!queue->sparse_flush.heap || !queue->sparse_flush.buffer) {
      kk_queue_sparse_finish(queue);
      return vk_error(queue, VK_ERROR_OUT_OF_DEVICE_MEMORY);
   }

   kk_device_add_heap_to_residency_set(dev, queue->sparse_flush.heap);
   kk_device_add_buffer_to_residency_set(dev, queue->sparse_flush.buffer);
   return VK_SUCCESS;
}

void
kk_queue_sparse_finish(struct kk_queue *queue)
{
   struct kk_device *dev = kk_queue_device(queue);

   if (queue->sparse_flush.buffer) {
      kk_device_remove_buffer_from_residency_set(dev,
                                                 queue->sparse_flush.buffer);
      mtl_release(queue->sparse_flush.buffer);
   }
   if (queue->sparse_flush.heap) {
      kk_device_remove_heap_from_residency_set(dev, queue->sparse_flush.heap);
      mtl_release(queue->sparse_flush.heap);
   }
   queue->sparse_flush.buffer = NULL;
   queue->sparse_flush.heap = NULL;
}

static mtl_heap *
kk_sparse_bind_heap(const VkSparseMemoryBind *bind)
{
   VK_FROM_HANDLE(kk_device_memory, mem, bind->memory);
   return mem ? mem->bo->mtl_handle : NULL;
}

/* Returns true if the bind unmaps pages */
static bool
kk_sparse_bind_buffer(struct kk_queue *queue,
                      const VkSparseBufferMemoryBindInfo *info)
{
   VK_FROM_HANDLE(kk_buffer, buffer, info->buffer);
   bool unmaps = false;

   for (uint32_t i = 0; i < info->bindCount; i++) {
      const VkSparseMemoryBind *bind = &info->pBinds[i];
      mtl_heap *heap = kk_sparse_bind_heap(bind);
      const struct mtl_sparse_buffer_mapping mapping = {
         .buffer_page = bind->resourceOffset / MTL_SPARSE_PAGE_SIZE_B,
         .page_count = DIV_ROUND_UP(bind->size, MTL_SPARSE_PAGE_SIZE_B),
         .heap_page = bind->memoryOffset / MTL_SPARSE_PAGE_SIZE_B,
         .map = heap != NULL,
      };
      mtl_command_queue_update_buffer_mappings(
         queue->mtl_handle, buffer->metal.handle, heap, &mapping, 1u);
      unmaps |= heap == NULL;
   }

   return unmaps;
}

void
kk_queue_bind_sparse(struct kk_queue *queue,
                     const struct vk_queue_submit *submit)
{
   bool unmaps = false;

   for (uint32_t i = 0; i < submit->buffer_bind_count; i++)
      unmaps |= kk_sparse_bind_buffer(queue, &submit->buffer_binds[i]);

   if (unmaps) {
      const struct mtl_sparse_buffer_mapping flush = {
         .page_count = 1u,
         .map = true,
      };
      mtl_command_queue_update_buffer_mappings(
         queue->mtl_handle, queue->sparse_flush.buffer,
         queue->sparse_flush.heap, &flush, 1u);
   }
}
