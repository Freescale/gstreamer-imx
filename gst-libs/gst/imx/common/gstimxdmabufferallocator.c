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

/**
 * SECTION:gstimxdmabufferallocator
 * @title: GstImxDmaBufferAllocator
 * @short_description: Interface for allocators that allocate ImxDmaBuffer instances
 * @see_also: #GstMemory, #GstPhysMemoryAllocator
 */
#include "config.h"

#include <gst/gst.h>
#include "gstimxdmabufferallocator.h"
#include "gstimxdmabufallocator.h"
#include "gstimxdefaultallocator.h"


GST_DEBUG_CATEGORY_STATIC(gst_imx_dma_buffer_allocator_debug);
#define GST_CAT_DEFAULT gst_imx_dma_buffer_allocator_debug


/**
 * gst_imx_is_imx_dma_buffer_memory:
 * @memory: a #GstMemory
 *
 * Returns: Whether the memory at @mem is backed by an ImxDmaBuffer instance
 */
gboolean gst_imx_is_imx_dma_buffer_memory(GstMemory *memory)
{
	return (memory != NULL)
	    && (memory->allocator != NULL)
	    && g_type_is_a(G_OBJECT_TYPE(memory->allocator), GST_TYPE_IMX_DMA_BUFFER_ALLOCATOR);
}


gboolean gst_imx_has_imx_dma_buffer_memory(GstBuffer *buffer)
{
	if (G_UNLIKELY((buffer == NULL) || (gst_buffer_n_memory(buffer) == 0)))
		return FALSE;

	return gst_imx_is_imx_dma_buffer_memory(gst_buffer_peek_memory(buffer, 0));
}


/**
 * gst_imx_get_dma_buffer_from_memory:
 * @memory: a #GstMemory
 *
 * Returns: Pointer to ImxDmaBuffer instance that backs this memory
 */
ImxDmaBuffer* gst_imx_get_dma_buffer_from_memory(GstMemory *memory)
{
	GstImxDmaBufferAllocatorInterface *iface;

	if (G_UNLIKELY(memory == NULL))
		return NULL;

	g_return_val_if_fail(GST_IS_IMX_DMA_BUFFER_ALLOCATOR(memory->allocator), NULL);

	iface = GST_IMX_DMA_BUFFER_ALLOCATOR_GET_INTERFACE(memory->allocator);
	g_return_val_if_fail(iface != NULL, NULL);
	g_return_val_if_fail(iface->get_dma_buffer != NULL, NULL);

	return iface->get_dma_buffer(GST_IMX_DMA_BUFFER_ALLOCATOR_CAST(memory->allocator), memory);
}


/**
 * gst_imx_get_dma_buffer_from_buffer:
 * @buffer: a #GstBuffer
 *
 * Convenience function that queries the first memory in the buffer
 * by calling gst_imx_get_dma_buffer_from_memory().
 *
 * Returns: Pointer to ImxDmaBuffer instance that backs this buffer's memory
 */
ImxDmaBuffer* gst_imx_get_dma_buffer_from_buffer(GstBuffer *buffer)
{
	if (G_UNLIKELY((buffer == NULL) || (gst_buffer_n_memory(buffer) == 0)))
		return NULL;

	return gst_imx_get_dma_buffer_from_memory(gst_buffer_peek_memory(buffer, 0));
}


/**
 * gst_imx_allocator_new:
 *
 * Creates a new allocator that allocates ImxDmaBuffer instances. Internally,
 * this chooses a DMA-BUF capable allocator like dma-heap or ION one is
 * enabled. Otherwise, it chooses the libimxdmabuffer default allocator.
 *
 * Returns: (transfer full) (nullable): Newly created allocator, or NULL in case of failure.
 */
GstAllocator* gst_imx_allocator_new(void)
{
#ifdef GST_DMABUF_ALLOCATOR_AVAILABLE
	return gst_imx_dmabuf_allocator_new();
#else
	return gst_imx_default_allocator_new();
#endif
}


GType gst_imx_dma_buffer_allocator_get_type(void)
{
	static volatile gsize imxdmabufferallocator_type = 0;

	if (g_once_init_enter(&imxdmabufferallocator_type))
	{
		GType _type;
		static GTypeInfo const imxdmabufferallocator_info =
		{
			sizeof(GstImxDmaBufferAllocatorInterface),
			NULL,
			NULL,
			NULL,
			NULL,
			NULL,
			0,
			0,
			NULL,
			NULL
		};

		_type = g_type_register_static(G_TYPE_INTERFACE, "GstImxDmaBufferAllocator", &imxdmabufferallocator_info, 0);

		GST_DEBUG_CATEGORY_INIT(gst_imx_dma_buffer_allocator_debug, "imxdmabufferallocator", GST_DEBUG_BOLD, "allocates i.MX DMA buffers");
		g_once_init_leave (&imxdmabufferallocator_type, _type);
	}

	return imxdmabufferallocator_type;
}
