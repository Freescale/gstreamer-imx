/* gstreamer-imx: GStreamer plugins for the i.MX SoCs
 * Copyright (C) 2019  Carlos Rafael Giani
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

#ifndef GST_IMX_DMA_BUFFER_ALLOCATOR_H
#define GST_IMX_DMA_BUFFER_ALLOCATOR_H

#include <gst/gst.h>
#include <imxdmabuffer/imxdmabuffer.h>


G_BEGIN_DECLS


#define GST_TYPE_IMX_DMA_BUFFER_ALLOCATOR               (gst_imx_dma_buffer_allocator_get_type())
#define GST_IMX_DMA_BUFFER_ALLOCATOR(obj)               (G_TYPE_CHECK_INSTANCE_CAST((obj), GST_TYPE_IMX_DMA_BUFFER_ALLOCATOR, GstImxDmaBufferAllocator))
#define GST_IMX_DMA_BUFFER_ALLOCATOR_CAST(obj)          ((GstImxDmaBufferAllocator *)(obj))
#define GST_IS_IMX_DMA_BUFFER_ALLOCATOR(obj)            (G_TYPE_CHECK_INSTANCE_TYPE((obj), GST_TYPE_IMX_DMA_BUFFER_ALLOCATOR))
#define GST_IMX_DMA_BUFFER_ALLOCATOR_GET_INTERFACE(obj) (G_TYPE_INSTANCE_GET_INTERFACE((obj), GST_TYPE_IMX_DMA_BUFFER_ALLOCATOR, GstImxDmaBufferAllocatorInterface))


/* Extra GstMemory map flag to underlying libimxdmabuffer allocators
 * to disable automatic cache sync. Needed if the allocated buffers
 * will be manually synced with imx_dma_buffer_start_sync_session()
 * and imx_dma_buffer_stop_sync_session(). */
#define GST_MAP_FLAG_IMX_MANUAL_SYNC (GST_MAP_FLAG_LAST + 0)


typedef struct _GstImxDmaBufferAllocator GstImxDmaBufferAllocator;
typedef struct _GstImxDmaBufferAllocatorInterface GstImxDmaBufferAllocatorInterface;


/**
 * GstImxDmaBufferAllocatorInterface:
 * @parent parent interface type.
 * @get_dma_buffer: virtual method to get an ImxDmaBuffer out of a GstMemory that was allocated by a DMA buffer allocator
 *
 * #GstImxDmaBufferAllocator interface.
 */
struct _GstImxDmaBufferAllocatorInterface
{
	GTypeInterface parent;

	/* methods */
	ImxDmaBuffer* (*get_dma_buffer)(GstImxDmaBufferAllocator *allocator, GstMemory *memory);

	/*< private >*/
	gpointer _gst_reserved[GST_PADDING];
};


GType gst_imx_dma_buffer_allocator_get_type(void);

gboolean gst_imx_is_imx_dma_buffer_memory(GstMemory *memory);
gboolean gst_imx_has_imx_dma_buffer_memory(GstBuffer *buffer);

ImxDmaBuffer* gst_imx_get_dma_buffer_from_memory(GstMemory *memory);
ImxDmaBuffer* gst_imx_get_dma_buffer_from_buffer(GstBuffer *buffer);

GstAllocator* gst_imx_allocator_new(void);


G_END_DECLS


#endif /* GST_IMX_DMA_BUFFER_ALLOCATOR_H */
