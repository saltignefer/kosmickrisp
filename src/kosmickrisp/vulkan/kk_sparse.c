/*
 * Copyright 2026 The Mesa Authors
 * SPDX-License-Identifier: MIT
 */

#include "kk_sparse.h"

#include "kk_bo.h"
#include "kk_buffer.h"
#include "kk_device.h"
#include "kk_device_memory.h"
#include "kk_image.h"
#include "kk_queue.h"

#include "kosmickrisp/bridge/mtl_bridge.h"

#include "util/u_dynarray.h"
#include "util/u_math.h"
#include "vk_format.h"
#include "vk_queue.h"

#define KK_SPARSE_PAGE_B MTL_SPARSE_PAGE_SIZE_B

VkResult
kk_image_plane_init_sparse(struct kk_device *dev, struct kk_image_plane *plane)
{
   const struct kk_image_layout *layout = &plane->layout;
   struct kk_sparse_image_layout *sparse = &plane->sparse;

   plane->mtl_handle =
      mtl_new_placement_sparse_texture(dev->mtl_handle, layout);
   if (!plane->mtl_handle)
      return vk_error(dev, VK_ERROR_OUT_OF_DEVICE_MEMORY);

   sparse->tile_px = mtl_placement_sparse_tile_size(dev->mtl_handle, layout);
   sparse->tail_level =
      MIN2(mtl_texture_first_mipmap_in_tail(plane->mtl_handle), layout->levels);
   sparse->tail_pages =
      sparse->tail_level < layout->levels
         ? DIV_ROUND_UP(mtl_texture_tail_size_in_bytes(plane->mtl_handle),
                        KK_SPARSE_PAGE_B)
         : 0u;

   uint64_t page = 0u;
   for (uint32_t level = 0u; level < sparse->tail_level; level++) {
      const struct mtl_size tiles = {
         .x =
            DIV_ROUND_UP(u_minify(layout->width_px, level), sparse->tile_px.x),
         .y =
            DIV_ROUND_UP(u_minify(layout->height_px, level), sparse->tile_px.y),
         .z =
            DIV_ROUND_UP(u_minify(layout->depth_px, level), sparse->tile_px.z),
      };
      sparse->level_tiles[level] = tiles;
      sparse->level_page[level] = page;
      page += tiles.x * tiles.y * tiles.z;
   }
   sparse->level_page[sparse->tail_level] = page;
   sparse->layer_pages = page + sparse->tail_pages;

   return VK_SUCCESS;
}

VkResult
kk_image_plane_init_residency_table(struct kk_device *dev,
                                    struct kk_image_plane *plane)
{
   const struct kk_sparse_image_layout *sparse = &plane->sparse;
   const uint64_t pages = sparse->layer_pages * plane->layout.layers;
   const uint64_t size_B = sizeof(struct kk_sparse_residency_table) +
                           DIV_ROUND_UP(pages, 32u) * sizeof(uint32_t);

   VkResult result =
      kk_alloc_bo(dev, &dev->vk.base, size_B, 16u, &plane->residency_table);
   if (result != VK_SUCCESS)
      return result;

   /* kk_alloc_bo may round the size up, and uploads copy all of it */
   struct kk_sparse_residency_table *table =
      calloc(1, plane->residency_table->size_B);
   if (!table) {
      kk_destroy_bo(dev, plane->residency_table);
      plane->residency_table = NULL;
      return VK_ERROR_OUT_OF_HOST_MEMORY;
   }
   plane->residency_cpu = table;
   table->tile_width_px = sparse->tile_px.x;
   table->tile_height_px = sparse->tile_px.y;
   table->tail_level = sparse->tail_level;
   table->layer_pages = sparse->layer_pages;
   table->last_page = pages - 1u;
   for (uint32_t level = 0u; level <= sparse->tail_level; level++)
      table->level_page[level] = sparse->level_page[level];
   for (uint32_t level = 0u; level < sparse->tail_level; level++)
      table->level_tiles_x[level] = sparse->level_tiles[level].x;
   memcpy(plane->residency_table->cpu, table, size_B);

   return VK_SUCCESS;
}

static void
kk_sparse_set_resident(struct kk_image_plane *plane, uint64_t page,
                       uint64_t count, bool resident)
{
   if (!plane->residency_table)
      return;

   struct kk_sparse_residency_table *table = plane->residency_cpu;
   const uint64_t end = MIN2(page + count, (uint64_t)table->last_page + 1u);
   for (; page < end; page++) {
      const uint32_t bit = 1u << (page % 32u);
      if (resident)
         table->resident[page / 32u] |= bit;
      else
         table->resident[page / 32u] &= ~bit;
   }
}

/* Image binds only cover levels before the mip tail */
static void
kk_sparse_set_region_resident(struct kk_image_plane *plane,
                              const struct mtl_sparse_texture_mapping *m)
{
   const struct kk_sparse_image_layout *sparse = &plane->sparse;
   if (!plane->residency_table || m->level >= sparse->tail_level)
      return;

   const struct mtl_size tiles = sparse->level_tiles[m->level];
   const uint64_t x_end = MIN2(m->origin.x + m->size.x, tiles.x);
   const uint64_t y_end = MIN2(m->origin.y + m->size.y, tiles.y);
   const uint64_t z_end = MIN2(m->origin.z + m->size.z, tiles.z);
   for (uint64_t z = m->origin.z; z < z_end; z++) {
      for (uint64_t y = m->origin.y; y < y_end; y++) {
         const uint64_t page = m->slice * sparse->layer_pages +
                               sparse->level_page[m->level] +
                               (z * tiles.y + y) * tiles.x + m->origin.x;
         if (x_end > m->origin.x)
            kk_sparse_set_resident(plane, page, x_end - m->origin.x, m->map);
      }
   }
}

/* Vulkan standard sparse image block shapes in texel blocks, indexed by
 * log2 of the texel block size in bytes */
static const struct mtl_size standard_2d[] = {
   {256, 256, 1}, {256, 128, 1}, {128, 128, 1}, {128, 64, 1}, {64, 64, 1},
};
/* Indexed by log2 of the sample count minus one, then block size */
static const struct mtl_size standard_ms[][5] = {
   {{128, 256, 1}, {128, 128, 1}, {64, 128, 1}, {64, 64, 1}, {32, 64, 1}},
   {{128, 128, 1}, {128, 64, 1}, {64, 64, 1}, {64, 32, 1}, {32, 32, 1}},
   {{64, 128, 1}, {64, 64, 1}, {32, 64, 1}, {32, 32, 1}, {16, 32, 1}},
   {{64, 64, 1}, {64, 32, 1}, {32, 32, 1}, {32, 16, 1}, {16, 16, 1}},
};

static bool
kk_sparse_block_is_standard(VkFormat format, VkSampleCountFlagBits samples,
                            struct mtl_size tile_px)
{
   const enum pipe_format p_format = vk_format_to_pipe_format(format);
   const uint32_t block_B = util_format_get_blocksize(p_format);
   if (!util_is_power_of_two_nonzero(block_B) || block_B > 16u)
      return false;

   const uint32_t size_idx = util_logbase2(block_B);
   const struct mtl_size shape =
      samples > 1u ? standard_ms[util_logbase2(samples) - 1u][size_idx]
                   : standard_2d[size_idx];

   return shape.x * util_format_get_blockwidth(p_format) == tile_px.x &&
          shape.y * util_format_get_blockheight(p_format) == tile_px.y &&
          tile_px.z == 1u;
}

VkSparseImageFormatProperties
kk_sparse_image_format_properties(VkFormat format,
                                  VkSampleCountFlagBits samples,
                                  struct mtl_size tile_px)
{
   /* Each array layer has its own mip tail, and the tail starts at the first
    * level smaller than a tile, so ALIGNED_MIP_SIZE does not apply. */
   return (VkSparseImageFormatProperties){
      .aspectMask = vk_format_aspects(format),
      .imageGranularity = {tile_px.x, tile_px.y, tile_px.z},
      .flags = kk_sparse_block_is_standard(format, samples, tile_px)
                  ? 0u
                  : VK_SPARSE_IMAGE_FORMAT_NONSTANDARD_BLOCK_SIZE_BIT,
   };
}

VkResult
kk_queue_sparse_init(struct kk_queue *queue)
{
   struct kk_device *dev = kk_queue_device(queue);

   if (!dev->vk.enabled_features.sparseBinding)
      return VK_SUCCESS;

   queue->sparse_flush.heap = mtl_new_heap(
      dev->mtl_handle, MTL_SPARSE_PAGE_SIZE_B, KK_MTL_RESOURCE_OPTIONS, true);
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
kk_sparse_heap(VkDeviceMemory memory)
{
   VK_FROM_HANDLE(kk_device_memory, mem, memory);
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
      mtl_heap *heap = kk_sparse_heap(bind->memory);
      const struct mtl_sparse_buffer_mapping mapping = {
         .buffer_page = bind->resourceOffset / KK_SPARSE_PAGE_B,
         .page_count = DIV_ROUND_UP(bind->size, KK_SPARSE_PAGE_B),
         .heap_page = bind->memoryOffset / KK_SPARSE_PAGE_B,
         .map = heap != NULL,
      };
      mtl_command_queue_update_buffer_mappings(
         queue->mtl_handle, buffer->metal.handle, heap, &mapping, 1u);
      unmaps |= heap == NULL;
   }

   return unmaps;
}

/* Splits a run of pages of the opaque address space into texture regions.
 * Within a level, pages follow tiles in X, Y, Z order like Metal assigns heap
 * pages to a region, so each region maps consecutive heap pages. */
static void
kk_sparse_opaque_to_regions(const struct kk_sparse_image_layout *sparse,
                            uint64_t page, uint64_t end, uint64_t heap_page,
                            bool map, struct util_dynarray *mappings)
{
   while (page < end) {
      const uint32_t layer = page / sparse->layer_pages;
      const uint64_t layer_page = page % sparse->layer_pages;
      uint32_t level = 0u;
      while (level < sparse->tail_level &&
             layer_page >= sparse->level_page[level + 1])
         level++;

      const uint64_t idx = layer_page - sparse->level_page[level];
      const uint64_t left = end - page;
      struct mtl_sparse_texture_mapping m = {
         .level = level,
         .slice = layer,
         .heap_page = heap_page,
         .map = map,
      };
      uint64_t count;
      if (level == sparse->tail_level) {
         /* Tail pages are addressed along X */
         count = MIN2(sparse->tail_pages - idx, left);
         m.origin = (struct mtl_origin){idx, 0u, 0u};
         m.size = (struct mtl_size){count, 1u, 1u};
      } else {
         const struct mtl_size tiles = sparse->level_tiles[level];
         const uint64_t x = idx % tiles.x;
         const uint64_t y = (idx / tiles.x) % tiles.y;
         const uint64_t z = idx / (tiles.x * tiles.y);
         if (x == 0u && left >= tiles.x) {
            const uint64_t rows = MIN2(left / tiles.x, tiles.y - y);
            count = rows * tiles.x;
            m.origin = (struct mtl_origin){0u, y, z};
            m.size = (struct mtl_size){tiles.x, rows, 1u};
         } else {
            count = MIN2(tiles.x - x, left);
            m.origin = (struct mtl_origin){x, y, z};
            m.size = (struct mtl_size){count, 1u, 1u};
         }
      }
      util_dynarray_append(mappings, m);
      page += count;
      heap_page += count;
   }
}

static void
kk_sparse_update_texture(struct kk_queue *queue, mtl_texture *texture,
                         mtl_heap *heap, struct util_dynarray *mappings)
{
   mtl_command_queue_update_texture_mappings(
      queue->mtl_handle, texture, heap, mappings->data,
      util_dynarray_num_elements(mappings, struct mtl_sparse_texture_mapping));
   util_dynarray_clear(mappings);
}

static bool
kk_sparse_bind_image_opaque(struct kk_queue *queue,
                            const VkSparseImageOpaqueMemoryBindInfo *info,
                            struct util_dynarray *mappings)
{
   VK_FROM_HANDLE(kk_image, image, info->image);
   struct kk_image_plane *plane = &image->planes[0];
   bool unmaps = false;

   for (uint32_t i = 0; i < info->bindCount; i++) {
      const VkSparseMemoryBind *bind = &info->pBinds[i];
      mtl_heap *heap = kk_sparse_heap(bind->memory);
      const uint64_t page = bind->resourceOffset / KK_SPARSE_PAGE_B;
      const uint64_t count = DIV_ROUND_UP(bind->size, KK_SPARSE_PAGE_B);
      kk_sparse_opaque_to_regions(&plane->sparse, page, page + count,
                                  bind->memoryOffset / KK_SPARSE_PAGE_B,
                                  heap != NULL, mappings);
      kk_sparse_update_texture(queue, plane->mtl_handle, heap, mappings);
      kk_sparse_set_resident(plane, page, count, heap != NULL);
      unmaps |= heap == NULL;
   }

   return unmaps;
}

static bool
kk_sparse_bind_image(struct kk_queue *queue,
                     const VkSparseImageMemoryBindInfo *info,
                     struct util_dynarray *mappings)
{
   VK_FROM_HANDLE(kk_image, image, info->image);
   struct kk_image_plane *plane = &image->planes[0];
   const struct mtl_size tile = plane->sparse.tile_px;
   bool unmaps = false;

   for (uint32_t i = 0; i < info->bindCount; i++) {
      const VkSparseImageMemoryBind *bind = &info->pBinds[i];
      mtl_heap *heap = kk_sparse_heap(bind->memory);
      const struct mtl_sparse_texture_mapping m = {
         .origin = {bind->offset.x / tile.x, bind->offset.y / tile.y,
                    bind->offset.z / tile.z},
         .size = {DIV_ROUND_UP(bind->extent.width, tile.x),
                  DIV_ROUND_UP(bind->extent.height, tile.y),
                  DIV_ROUND_UP(bind->extent.depth, tile.z)},
         .level = bind->subresource.mipLevel,
         .slice = bind->subresource.arrayLayer,
         .heap_page = bind->memoryOffset / KK_SPARSE_PAGE_B,
         .map = heap != NULL,
      };
      util_dynarray_append(mappings, m);
      kk_sparse_update_texture(queue, plane->mtl_handle, heap, mappings);
      kk_sparse_set_region_resident(plane, &m);
      unmaps |= heap == NULL;
   }

   return unmaps;
}

struct kk_sparse_upload {
   struct kk_image_plane *plane;
   struct kk_bo *bo;
};

struct kk_sparse_barrier {
   struct kk_device *dev;
   struct kk_queue *queue;
   mtl_command_allocator *allocator;
   mtl_command_buffer *cmd_buf;
   mtl_compute_encoder *encoder;
   /* Copies of the residency tables of the bound images, struct
    * kk_sparse_upload */
   struct util_dynarray uploads;
};

static void
kk_sparse_barrier_destroy(struct kk_sparse_barrier *barrier)
{
   util_dynarray_foreach(&barrier->uploads, struct kk_sparse_upload, upload)
      kk_destroy_bo(barrier->dev, upload->bo);
   util_dynarray_fini(&barrier->uploads);
   if (barrier->cmd_buf)
      mtl_release(barrier->cmd_buf);
   if (barrier->allocator)
      mtl_release(barrier->allocator);
   free(barrier);
}

static void
kk_sparse_barrier_done(struct mtl_feedback_data *data)
{
   struct kk_sparse_barrier *barrier = data->user_data;
   struct kk_queue *queue = barrier->queue;
   kk_sparse_barrier_destroy(barrier);

   mtx_lock(&queue->mutex);
   kk_queue_end_commit_locked(queue);
   mtx_unlock(&queue->mutex);
}

/* Allocates the copy of the residency table of each plane of the image, so
 * no allocation can fail once Metal got the mappings */
static VkResult
kk_sparse_alloc_uploads(struct kk_sparse_barrier *barrier,
                        struct kk_image *image)
{
   for (uint8_t p = 0; p < image->plane_count; p++) {
      struct kk_image_plane *plane = &image->planes[p];
      if (!plane->residency_table)
         continue;

      bool found = false;
      util_dynarray_foreach(&barrier->uploads, struct kk_sparse_upload, upload)
         found |= upload->plane == plane;
      if (found)
         continue;

      struct kk_sparse_upload upload = {.plane = plane};
      VkResult result =
         kk_alloc_bo(barrier->dev, &barrier->dev->vk.base,
                     plane->residency_table->size_B, 4u, &upload.bo);
      if (result != VK_SUCCESS)
         return result;
      util_dynarray_append(&barrier->uploads, upload);
   }
   return VK_SUCCESS;
}

VkResult
kk_queue_prepare_sparse(struct kk_queue *queue,
                        const struct vk_queue_submit *submit,
                        struct kk_sparse_barrier **barrier_out)
{
   *barrier_out = NULL;
   if (!submit->buffer_bind_count && !submit->image_opaque_bind_count &&
       !submit->image_bind_count)
      return VK_SUCCESS;

   struct kk_device *dev = kk_queue_device(queue);
   struct kk_sparse_barrier *barrier = calloc(1, sizeof(*barrier));
   if (!barrier)
      return vk_error(queue, VK_ERROR_OUT_OF_HOST_MEMORY);

   barrier->dev = dev;
   barrier->queue = queue;
   barrier->uploads = (struct util_dynarray)UTIL_DYNARRAY_INIT;

   VkResult result = VK_SUCCESS;
   for (uint32_t i = 0; i < submit->image_opaque_bind_count; i++) {
      VK_FROM_HANDLE(kk_image, image, submit->image_opaque_binds[i].image);
      if (result == VK_SUCCESS)
         result = kk_sparse_alloc_uploads(barrier, image);
   }
   for (uint32_t i = 0; i < submit->image_bind_count; i++) {
      VK_FROM_HANDLE(kk_image, image, submit->image_binds[i].image);
      if (result == VK_SUCCESS)
         result = kk_sparse_alloc_uploads(barrier, image);
   }

   barrier->allocator = mtl_new_command_allocator(dev->mtl_handle);
   barrier->cmd_buf = mtl_new_command_buffer(dev->mtl_handle);
   if (result == VK_SUCCESS && (!barrier->allocator || !barrier->cmd_buf))
      result = vk_error(queue, VK_ERROR_OUT_OF_HOST_MEMORY);

   /* Commands run when committed, so they can be recorded before Metal gets
    * the mappings */
   if (result == VK_SUCCESS) {
      mtl_begin_command_buffer(barrier->cmd_buf, barrier->allocator);
      barrier->encoder = mtl_new_compute_command_encoder(barrier->cmd_buf);
      if (!barrier->encoder) {
         mtl_end_command_buffer(barrier->cmd_buf);
         result = vk_error(queue, VK_ERROR_OUT_OF_HOST_MEMORY);
      }
   }

   if (result != VK_SUCCESS) {
      kk_sparse_barrier_destroy(barrier);
      return result;
   }

   *barrier_out = barrier;
   return VK_SUCCESS;
}

/* Metal applies mapping updates asynchronously. Work committed later, and
 * the queue's later event signals, only observe them once a command buffer
 * waited on the resource state stage. The same command buffer copies the
 * residency tables changed by the binds, so work still running before the
 * binds does not see them. */
static void
kk_queue_sparse_barrier(struct kk_queue *queue,
                        struct kk_sparse_barrier *barrier)
{
   struct kk_device *dev = kk_queue_device(queue);

   mtl_compute_encoder *encoder = barrier->encoder;
   barrier->encoder = NULL;
   mtl_barrier_after_queue_stages(encoder, MTL_STAGE_RESOURCE_STATE,
                                  MTL_STAGE_ALL);
   util_dynarray_foreach(&barrier->uploads, struct kk_sparse_upload, upload) {
      struct kk_image_plane *plane = upload->plane;
      const uint64_t size_B = plane->residency_table->size_B;
      memcpy(upload->bo->cpu, plane->residency_cpu, size_B);
      mtl_copy_from_buffer_to_buffer(encoder, upload->bo->map, 0u,
                                     plane->residency_table->map, 0u, size_B);
   }
   /* Later work, and the copies of later binds, see the tables */
   if (barrier->uploads.size)
      mtl_barrier_after_stages(encoder, MTL_STAGE_BLIT, MTL_STAGE_ALL);
   mtl_end_encoding(encoder);
   mtl_release(encoder);
   mtl_end_command_buffer(barrier->cmd_buf);

   /* The uploads must be resident */
   if (barrier->uploads.size)
      kk_device_make_resources_resident(dev);

   mtx_lock(&queue->mutex);
   queue->commits_in_flight++;
   mtx_unlock(&queue->mutex);

   mtl_commit_options *options = mtl_new_commit_options();
   mtl_commit_options_add_feedback_handler(options, kk_sparse_barrier_done,
                                           barrier);
   mtl_command_queue_commit(queue->mtl_handle, &barrier->cmd_buf, 1u, options);
   mtl_release(options);
}

void
kk_queue_bind_sparse(struct kk_queue *queue,
                     const struct vk_queue_submit *submit,
                     struct kk_sparse_barrier *barrier)
{
   if (!barrier)
      return;

   struct util_dynarray mappings = UTIL_DYNARRAY_INIT;
   bool unmaps = false;

   for (uint32_t i = 0; i < submit->buffer_bind_count; i++)
      unmaps |= kk_sparse_bind_buffer(queue, &submit->buffer_binds[i]);
   for (uint32_t i = 0; i < submit->image_opaque_bind_count; i++)
      unmaps |= kk_sparse_bind_image_opaque(
         queue, &submit->image_opaque_binds[i], &mappings);
   for (uint32_t i = 0; i < submit->image_bind_count; i++)
      unmaps |= kk_sparse_bind_image(queue, &submit->image_binds[i], &mappings);

   util_dynarray_fini(&mappings);

   if (unmaps) {
      const struct mtl_sparse_buffer_mapping flush = {
         .page_count = 1u,
         .map = true,
      };
      mtl_command_queue_update_buffer_mappings(
         queue->mtl_handle, queue->sparse_flush.buffer,
         queue->sparse_flush.heap, &flush, 1u);
   }

   kk_queue_sparse_barrier(queue, barrier);
}
