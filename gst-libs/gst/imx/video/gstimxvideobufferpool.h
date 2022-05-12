/* gstreamer-imx: GStreamer plugins for the i.MX SoCs
 * Copyright (C) 2022  Carlos Rafael Giani
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

#ifndef GST_IMX_VIDEO_BUFFER_POOL_H
#define GST_IMX_VIDEO_BUFFER_POOL_H

#include <gst/gst.h>
#include <gst/video/video.h>


G_BEGIN_DECLS


#define GST_TYPE_IMX_VIDEO_BUFFER_POOL             (gst_imx_video_buffer_pool_get_type())
#define GST_IMX_VIDEO_BUFFER_POOL(obj)             (G_TYPE_CHECK_INSTANCE_CAST((obj), GST_TYPE_IMX_VIDEO_BUFFER_POOL, GstImxVideoBufferPool))
#define GST_IMX_VIDEO_BUFFER_POOL_CLASS(klass)     (G_TYPE_CHECK_CLASS_CAST((klass), GST_TYPE_IMX_VIDEO_BUFFER_POOL, GstImxVideoBufferPoolClass))
#define GST_IMX_VIDEO_BUFFER_POOL_GET_CLASS(obj)   (G_TYPE_INSTANCE_GET_CLASS((obj), GST_TYPE_IMX_VIDEO_BUFFER_POOL, GstImxVideoBufferPoolClass))
#define GST_IMX_VIDEO_BUFFER_POOL_CAST(obj)        ((GstImxVideoBufferPool *)(obj))
#define GST_IS_IMX_VIDEO_BUFFER_POOL(obj)          (G_TYPE_CHECK_INSTANCE_TYPE((obj), GST_TYPE_IMX_VIDEO_BUFFER_POOL))
#define GST_IS_IMX_VIDEO_BUFFER_POOL_CLASS(klass)  (G_TYPE_CHECK_CLASS_TYPE((klass), GST_TYPE_IMX_VIDEO_BUFFER_POOL))


/**
 * GstImxVideoBufferPool:
 *
 * Creates internal buffer pools depending on the downstream videometa support and the output video info.
 *
 * If downstream does not support video metas, and if a GStreamer element produces
 * buffers with frames that use stride / plane offset values that aren't tightly packed,
 * then output frames have to be copied internally into a layout that is tightly packed.
 *
 * "Tightly packed" means that all of these apply:
 *
 * 1. The stride size equals width * bytes_per_pixel (that is, there are no padding pixels/bytes to the right)
 * 2. In case of planar formats, the plane offsets are such that there are no padding bytes between planes
 *
 * If this is the case, then stride and plane offset values can be directly derived from
 * the video/x-raw caps. If they aren't, then the caps are not enough - the video meta
 * data is then necessary. But if downstream can't handle video metas, it therefore can't
 * handle these frames directly. Therefore, they need to be transferred into a form that
 * _is_ tightly packed before pushing the frames downstream.
 *
 * To that end, this object exists. It creates two internal buffer pools, one based on a
 * GstVideoInfo structure (which can have non-tightly-packed stride and plane offset values),
 * the other based on caps from an allocation query. The former allocates DMA buffers,
 * and is called the "internal DMA buffer pool". The latter is called the "output video
 * buffer pool", and allocates buffers using normal system memory.
 *
 * If downstream can handle video meta, or if the stride / plane offset values are tightly
 * packed, then both buffer pools are the same (that is, there's _one_ pool inside, one
 * that allocates DMA buffers). That's because in such a case, frame copies are unnecessary,
 * so a separate pool for output buffers is not needed.
 *
 * The GstImxVideoBufferPool is created in the decide_allocation vmethods of the elements
 * (or in the allocation query handler in case the element is not based on a subclass that
 * has such a vmethod). The output video buffer pool is added to that query, while the
 * internal DMA buffer pool isn't.
 *
 * When the element's subclass then acquires a buffer from the pool that was added to the
 * query (that is - the output video buffer pool in our case), callers need to pass it to
 * gst_imx_video_buffer_pool_acquire_intermediate_buffer(). This acquires an "intermediate
 * buffer" from the internal DMA buffer pool.  The element then needs to use that intermediate
 * buffer as the target for its blitting operations.
 *
 * Once the blitter is done, gst_imx_video_buffer_pool_transfer_to_output_buffer() needs
 * to be called to transfer over the rendered pixels into a form that is suitable for
 * downstream. The element can then push the final buffer downstream.
 *
 * Should both pools be the same, then nothing is acquired / transferred, since
 * the buffer acquired by the subclass and the intermediate buffer are one and the same;
 * gst_imx_video_buffer_pool_acquire_intermediate_buffer() then just refs the output_buffer
 * and sets (*intermediate_buffer) to output_buffer, and the intermedia_buffer is unref'd in
 * gst_imx_video_buffer_pool_transfer_to_output_buffer(). That way, these two functions
 * effectively become no-ops.
 */
typedef struct _GstImxVideoBufferPool GstImxVideoBufferPool;
typedef struct _GstImxVideoBufferPoolClass GstImxVideoBufferPoolClass;


GType gst_imx_video_buffer_pool_get_type(void);

/**
 * gst_imx_video_buffer_pool_new:
 * @imx_dma_buffer_allocator: ImxDmaBuffer allocator to use for allocating gstbuffers.
 * @query: Allocation query to parse.
 * @intermediate_video_info: Video info of the intermediate frames.
 *
 * Returns: (transfer floating) A new video buffer pool.
 */
GstImxVideoBufferPool* gst_imx_video_buffer_pool_new(
	GstAllocator *imx_dma_buffer_allocator,
	GstQuery *query,
	GstVideoInfo const *intermediate_video_info
);

GstBufferPool* gst_imx_video_buffer_pool_get_internal_dma_buffer_pool(GstImxVideoBufferPool *imx_video_buffer_pool);
GstBufferPool* gst_imx_video_buffer_pool_get_output_video_buffer_pool(GstImxVideoBufferPool *imx_video_buffer_pool);

GstFlowReturn gst_imx_video_buffer_pool_acquire_intermediate_buffer(GstImxVideoBufferPool *imx_video_buffer_pool, GstBuffer *output_buffer, GstBuffer **intermediate_buffer);
gboolean gst_imx_video_buffer_pool_transfer_to_output_buffer(GstImxVideoBufferPool *imx_video_buffer_pool, GstBuffer *intermediate_buffer, GstBuffer *output_buffer);

gboolean gst_imx_video_buffer_pool_are_both_pools_same(GstImxVideoBufferPool *imx_video_buffer_pool);
gboolean gst_imx_video_buffer_pool_video_meta_supported(GstImxVideoBufferPool *imx_video_buffer_pool);


G_END_DECLS


#endif /* GST_IMX_VIDEO_BUFFER_POOL_H */
