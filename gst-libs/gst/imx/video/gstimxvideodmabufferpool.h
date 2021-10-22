/* gstreamer-imx: GStreamer plugins for the i.MX SoCs
 * Copyright (C) 2021  Carlos Rafael Giani
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Library General Public
 * License as published by the Free Software Foundation; either
 * version 2 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Library General Public License for more details.
 *
 * You should have received a copy of the GNU Library General Public
 * License along with this library; if not, write to the Free
 * Software Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
 */

#ifndef GST_IMX_VIDEO_DMA_BUFFER_POOL_H
#define GST_IMX_VIDEO_DMA_BUFFER_POOL_H

#include <gst/gst.h>
#include <gst/video/video.h>


G_BEGIN_DECLS


#define GST_TYPE_IMX_VIDEO_DMA_BUFFER_POOL             (gst_imx_video_dma_buffer_pool_get_type())
#define GST_IMX_VIDEO_DMA_BUFFER_POOL(obj)             (G_TYPE_CHECK_INSTANCE_CAST((obj), GST_TYPE_IMX_VIDEO_DMA_BUFFER_POOL, GstImxVideoDmaBufferPool))
#define GST_IMX_VIDEO_DMA_BUFFER_POOL_CLASS(klass)     (G_TYPE_CHECK_CLASS_CAST((klass), GST_TYPE_IMX_VIDEO_DMA_BUFFER_POOL, GstImxVideoDmaBufferPoolClass))
#define GST_IMX_VIDEO_DMA_BUFFER_POOL_GET_CLASS(klass) (G_TYPE_INSTANCE_GET_CLASS((obj), GST_TYPE_IMX_VIDEO_DMA_BUFFER_POOL, GstImxVideoDmaBufferPoolClass))
#define GST_IMX_VIDEO_DMA_BUFFER_POOL_CAST(obj)        ((GstImxVideoDmaBufferPool *)(obj))
#define GST_IS_IMX_VIDEO_DMA_BUFFER_POOL(obj)          (G_TYPE_CHECK_INSTANCE_TYPE((obj), GST_TYPE_IMX_VIDEO_DMA_BUFFER_POOL))
#define GST_IS_IMX_VIDEO_DMA_BUFFER_POOL_CLASS(klass)  (G_TYPE_CHECK_CLASS_TYPE((klass), GST_TYPE_IMX_VIDEO_DMA_BUFFER_POOL))


/**
 * GstImxVideoDmaBufferPool:
 *
 * Special buffer pool designed for use with elements that produce
 * buffers backed with DMA memory and allocated based on the specification
 * from a GstVideoInfo instance. The allocated GstBuffer instances
 * may be single-memory (= all video frame planes in one GstMemory)
 * or multi-memory (= one GstMemory per plane). The plane sizes may
 * be specified manually (useful when a driver / API requires certain
 * plane sizes) or calculated out of the GstVideoInfo.
 *
 * Importantly, the regular buffer pool configuration that is set
 * gst_buffer_pool_set_config is not used, since the GstVideoInfo
 * already supplies all the necessary information.
 */

typedef struct _GstImxVideoDmaBufferPool GstImxVideoDmaBufferPool;
typedef struct _GstImxVideoDmaBufferPoolClass GstImxVideoDmaBufferPoolClass;


GType gst_imx_video_dma_buffer_pool_get_type(void);


/**
 * gst_imx_video_dma_buffer_pool_new:
 * @imx_dma_buffer_allocator: ImxDmaBuffer allocator to use for
 *     allocating GstMemory blocks.
 * @video_info: GstVideoInfo to use for allocating buffers.
 * @create_multi_memory_buffers: True if the allocated GstBuffer
 *     shall contain one GstMemory per plane.
 * @plane_sizes If non-NULL, this contains the sizes for each
 *     plane, in bytes. If NULL, the sizes are calculated
 *     out of the video_info.
 *
 * Creates a new GstImxVideoDmaBufferPool instance.
 *
 * The created buffer pool comes already activated and configured.
 * Buffers are allocated according to the video info. Their size
 * is defined by the video_info's size field, or by the total sum
 * of the plane_sizes if that sum exceeds the video_info size.
 */
GstBufferPool* gst_imx_video_dma_buffer_pool_new(
	GstAllocator *imx_dma_buffer_allocator,
	GstVideoInfo *video_info,
	gboolean create_multi_memory_buffers,
	gsize *plane_sizes
);

GstVideoInfo const * gst_imx_video_dma_buffer_pool_get_video_info(GstBufferPool *imx_video_dma_buffer_pool);
gboolean gst_imx_video_dma_buffer_pool_creates_multi_memory_buffers(GstBufferPool *imx_video_dma_buffer_pool);

gsize gst_imx_video_dma_buffer_pool_get_plane_offset(GstBufferPool *imx_video_dma_buffer_pool, gint plane_index);
gsize gst_imx_video_dma_buffer_pool_get_plane_size(GstBufferPool *imx_video_dma_buffer_pool, gint plane_index);


G_END_DECLS


#endif /* GST_IMX_VIDEO_DMA_BUFFER_POOL_H */
