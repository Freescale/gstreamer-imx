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
 * SECTION:gstimxdefaultallocator
 * @title: GstImxDefaultAllocator
 * @short_description: ImxDmabuffer-backed allocator using the default libimxdmabuffer allocator
 * @see_also: #GstMemory, #GstPhysMemoryAllocator, #GstImxDmaBufferAllocator
 */
#include <string.h>
#include <gst/gst.h>
#include <gst/allocators/allocators.h>
#include <imxdmabuffer/imxdmabuffer.h>
#include "gstimxdmabufferallocator.h"
#include "gstimxdefaultallocator.h"


GST_DEBUG_CATEGORY_STATIC(imx_default_allocator_debug);
#define GST_CAT_DEFAULT imx_default_allocator_debug


#define GST_IMX_DEFAULT_MEMORY_TYPE "ImxDefaultDmaMemory"

static GQuark gst_imx_default_memory_imxbuffer_quark;


typedef struct _GstImxDefaultDmaMemory GstImxDefaultDmaMemory;


struct _GstImxDefaultDmaMemory
{
	GstMemory parent;
	ImxDmaBuffer *dmabuffer;
};


struct _GstImxDefaultAllocator
{
	GstAllocator parent;
	ImxDmaBufferAllocator *imxdmabuffer_allocator;
};


struct _GstImxDefaultAllocatorClass
{
	GstAllocatorClass parent_class;
};


static void gst_imx_default_allocator_phys_mem_allocator_iface_init(gpointer iface, gpointer iface_data);
guintptr gst_imx_default_allocator_get_phys_addr(GstPhysMemoryAllocator *allocator, GstMemory *memory);

static void gst_imx_default_allocator_dma_buffer_allocator_iface_init(gpointer iface, gpointer iface_data);
ImxDmaBuffer* gst_imx_default_allocator_get_dma_buffer(GstImxDmaBufferAllocator *allocator, GstMemory *memory);


G_DEFINE_TYPE_WITH_CODE(
	GstImxDefaultAllocator, gst_imx_default_allocator, GST_TYPE_ALLOCATOR,
	G_IMPLEMENT_INTERFACE(GST_TYPE_PHYS_MEMORY_ALLOCATOR,    gst_imx_default_allocator_phys_mem_allocator_iface_init)
	G_IMPLEMENT_INTERFACE(GST_TYPE_IMX_DMA_BUFFER_ALLOCATOR, gst_imx_default_allocator_dma_buffer_allocator_iface_init)
);

static void gst_imx_default_allocator_dispose(GObject *object);

static GstMemory* gst_imx_default_allocator_alloc(GstAllocator *allocator, gsize size, GstAllocationParams *params);
static void gst_imx_default_allocator_free(GstAllocator *allocator, GstMemory *memory);

static gpointer gst_imx_default_allocator_map(GstMemory *memory, GstMapInfo *info, gsize maxsize);
static void gst_imx_default_allocator_unmap(GstMemory *memory, GstMapInfo *info);
static GstMemory * gst_imx_default_allocator_copy(GstMemory *memory, gssize offset, gssize size);
static GstMemory * gst_imx_default_allocator_share(GstMemory *memory, gssize offset, gssize size);
static gboolean gst_imx_default_allocator_is_span(GstMemory *memory1, GstMemory *memory2, gsize *offset);




static void gst_imx_default_allocator_class_init(GstImxDefaultAllocatorClass *klass)
{
	GObjectClass *object_class;
	GstAllocatorClass *allocator_class;

	GST_DEBUG_CATEGORY_INIT(imx_default_allocator_debug, "imxdefaultallocator", 0, "physical memory allocator using the default libimxdmabuffer DMA buffer allocator");

	gst_imx_default_memory_imxbuffer_quark = g_quark_from_static_string("gst-imx-default-memory-imxbuffer");

	object_class = G_OBJECT_CLASS(klass);
	allocator_class = GST_ALLOCATOR_CLASS(klass);

	object_class->dispose = GST_DEBUG_FUNCPTR(gst_imx_default_allocator_dispose);
	allocator_class->alloc = GST_DEBUG_FUNCPTR(gst_imx_default_allocator_alloc);
	allocator_class->free = GST_DEBUG_FUNCPTR(gst_imx_default_allocator_free);
}


static void gst_imx_default_allocator_init(GstImxDefaultAllocator *imx_default_allocator)
{
	GstAllocator *allocator = GST_ALLOCATOR(imx_default_allocator);

	allocator->mem_type       = GST_IMX_DEFAULT_MEMORY_TYPE;
	allocator->mem_map_full   = GST_DEBUG_FUNCPTR(gst_imx_default_allocator_map);
	allocator->mem_unmap_full = GST_DEBUG_FUNCPTR(gst_imx_default_allocator_unmap);
	allocator->mem_copy       = GST_DEBUG_FUNCPTR(gst_imx_default_allocator_copy);
	allocator->mem_share      = GST_DEBUG_FUNCPTR(gst_imx_default_allocator_share);
	allocator->mem_is_span    = GST_DEBUG_FUNCPTR(gst_imx_default_allocator_is_span);
}


static void gst_imx_default_allocator_dispose(GObject *object)
{
	GstImxDefaultAllocator *imx_default_allocator = GST_IMX_DEFAULT_ALLOCATOR(object);

	if (imx_default_allocator->imxdmabuffer_allocator != NULL)
	{
		imx_dma_buffer_allocator_destroy(imx_default_allocator->imxdmabuffer_allocator);
		imx_default_allocator->imxdmabuffer_allocator = NULL;
	}

	G_OBJECT_CLASS(gst_imx_default_allocator_parent_class)->dispose(object);
}


static void gst_imx_default_allocator_phys_mem_allocator_iface_init(gpointer iface, gpointer G_GNUC_UNUSED iface_data)
{
	GstPhysMemoryAllocatorInterface *phys_mem_allocator_iface = (GstPhysMemoryAllocatorInterface *)iface;
	phys_mem_allocator_iface->get_phys_addr = GST_DEBUG_FUNCPTR(gst_imx_default_allocator_get_phys_addr);
}


guintptr gst_imx_default_allocator_get_phys_addr(G_GNUC_UNUSED GstPhysMemoryAllocator *allocator, GstMemory *memory)
{
	GstImxDefaultDmaMemory *dma_memory = (GstImxDefaultDmaMemory *)memory;
	return imx_dma_buffer_get_physical_address(dma_memory->dmabuffer) + memory->offset;
}


static void gst_imx_default_allocator_dma_buffer_allocator_iface_init(gpointer iface, gpointer G_GNUC_UNUSED iface_data)
{
	GstImxDmaBufferAllocatorInterface *imx_dma_buffer_allocator_iface = (GstImxDmaBufferAllocatorInterface *)iface;
	imx_dma_buffer_allocator_iface->get_dma_buffer = GST_DEBUG_FUNCPTR(gst_imx_default_allocator_get_dma_buffer);
}


ImxDmaBuffer* gst_imx_default_allocator_get_dma_buffer(G_GNUC_UNUSED GstImxDmaBufferAllocator *allocator, GstMemory *memory)
{
	GstImxDefaultDmaMemory *dma_memory = (GstImxDefaultDmaMemory *)memory;
	return dma_memory->dmabuffer;
}


static GstMemory* gst_imx_default_allocator_alloc(GstAllocator *allocator, gsize size, GstAllocationParams *params)
{
	int error;
	ImxDmaBuffer *dmabuffer;
	GstImxDefaultDmaMemory *imx_dma_memory;
	GstImxDefaultAllocator *imx_default_allocator = GST_IMX_DEFAULT_ALLOCATOR(allocator);

	g_assert(imx_default_allocator != NULL);

	dmabuffer = imx_dma_buffer_allocate(imx_default_allocator->imxdmabuffer_allocator, size + params->padding, params->align + 1, &error);
	if (dmabuffer == NULL)
	{
		GST_ERROR_OBJECT(imx_default_allocator, "could not allocate memory with default i.MX DMA allocator: %s (%d)", strerror(error), error);
		return NULL;
	}

	imx_dma_memory = g_slice_alloc0(sizeof(GstImxDefaultDmaMemory));
	gst_memory_init(GST_MEMORY_CAST(imx_dma_memory), params->flags | GST_MEMORY_FLAG_PHYSICALLY_CONTIGUOUS, allocator, NULL, size + params->padding, params->align, 0, size);
	imx_dma_memory->dmabuffer = dmabuffer;

	return GST_MEMORY_CAST(imx_dma_memory);
}


static void gst_imx_default_allocator_free(GstAllocator *allocator, GstMemory *memory)
{
	GstImxDefaultDmaMemory *imx_dma_memory = (GstImxDefaultDmaMemory *)memory;
	GstImxDefaultAllocator *imx_default_allocator = GST_IMX_DEFAULT_ALLOCATOR(allocator);

	g_assert(imx_default_allocator != NULL);
	g_assert(imx_dma_memory != NULL);
	g_assert(imx_dma_memory->dmabuffer != NULL);

	imx_dma_buffer_deallocate(imx_dma_memory->dmabuffer);

	g_slice_free1(sizeof(GstImxDefaultDmaMemory), imx_dma_memory);
}


static gpointer gst_imx_default_allocator_map(GstMemory *memory, GstMapInfo *info, G_GNUC_UNUSED gsize maxsize)
{
	GstImxDefaultDmaMemory *imx_dma_memory = (GstImxDefaultDmaMemory *)memory;
	unsigned int flags = 0;
	int error;
	uint8_t *mapped_virtual_address;

	if (info->flags & GST_MAP_READ)
		flags |= IMX_DMA_BUFFER_MAPPING_FLAG_READ;
	if (info->flags & GST_MAP_WRITE)
		flags |= IMX_DMA_BUFFER_MAPPING_FLAG_WRITE;

	mapped_virtual_address = imx_dma_buffer_map(imx_dma_memory->dmabuffer, flags, &error);
	if (mapped_virtual_address == NULL)
		GST_ERROR_OBJECT(memory->allocator, "could not map memory: %s (%d)", strerror(error), error);

	return mapped_virtual_address;
}


static void gst_imx_default_allocator_unmap(GstMemory *memory, G_GNUC_UNUSED GstMapInfo *info)
{
	GstImxDefaultDmaMemory *imx_dma_memory = (GstImxDefaultDmaMemory *)memory;
	imx_dma_buffer_unmap(imx_dma_memory->dmabuffer);
}


static GstMemory * gst_imx_default_allocator_copy(GstMemory *memory, gssize offset, gssize size)
{
	GstImxDefaultDmaMemory *imx_dma_memory = (GstImxDefaultDmaMemory *)memory;
	GstImxDefaultAllocator *imx_default_allocator = GST_IMX_DEFAULT_ALLOCATOR(memory->allocator);
	GstImxDefaultDmaMemory *new_imx_dma_memory = NULL;
	uint8_t *mapped_src_data = NULL, *mapped_dest_data = NULL;
	int error;

	if (size == -1)
	{
		size = imx_dma_buffer_get_size(imx_dma_memory->dmabuffer);
		size = (size > offset) ? (size - offset) : 0;
	}

	new_imx_dma_memory = g_slice_alloc0(sizeof(GstImxDefaultDmaMemory));
	if (G_UNLIKELY(new_imx_dma_memory == NULL))
	{
		GST_ERROR_OBJECT(imx_default_allocator, "could not allocate slice for GstImxDefaultDmaMemory copy");
		goto cleanup;
	}

	gst_memory_init(GST_MEMORY_CAST(new_imx_dma_memory), GST_MEMORY_FLAG_PHYSICALLY_CONTIGUOUS, memory->allocator, NULL, size, memory->align, 0, size);

	new_imx_dma_memory->dmabuffer = imx_dma_buffer_allocate(imx_default_allocator->imxdmabuffer_allocator, size, memory->align + 1, &error);
	if (G_UNLIKELY(new_imx_dma_memory->dmabuffer == NULL))
	{
		GST_ERROR_OBJECT(imx_default_allocator, "could not allocate DMA buffer for copy: %s (%d)", strerror(error), error);
		goto cleanup;
	}

	mapped_src_data = imx_dma_buffer_map(imx_dma_memory->dmabuffer, IMX_DMA_BUFFER_MAPPING_FLAG_READ, &error);
	if (mapped_src_data == NULL)
	{
		GST_ERROR_OBJECT(imx_default_allocator, "could not map source DMA buffer for copy: %s (%d)", strerror(error), error);
		goto cleanup;
	}

	mapped_dest_data = imx_dma_buffer_map(new_imx_dma_memory->dmabuffer, IMX_DMA_BUFFER_MAPPING_FLAG_WRITE, &error);
	if (mapped_dest_data == NULL)
	{
		GST_ERROR_OBJECT(imx_default_allocator, "could not map source DMA buffer for copy: %s (%d)", strerror(error), error);
		goto cleanup;
	}

	/* TODO: Is it perhaps possible to copy over DMA instead of by using the CPU? */
	memcpy(mapped_dest_data, mapped_src_data + offset, size);

finish:
	if (mapped_src_data != NULL)
		imx_dma_buffer_unmap(imx_dma_memory->dmabuffer);
	if (mapped_dest_data != NULL)
		imx_dma_buffer_unmap(new_imx_dma_memory->dmabuffer);

	return GST_MEMORY_CAST(new_imx_dma_memory);

cleanup:
	if (new_imx_dma_memory != NULL)
	{
		g_slice_free1(sizeof(GstImxDefaultDmaMemory), new_imx_dma_memory);
		new_imx_dma_memory = NULL;
	}

	goto finish;
}


static GstMemory * gst_imx_default_allocator_share(GstMemory *memory, gssize offset, gssize size)
{
	GstImxDefaultDmaMemory *imx_dma_memory = (GstImxDefaultDmaMemory *)memory;
	GstImxDefaultAllocator *imx_default_allocator = GST_IMX_DEFAULT_ALLOCATOR(memory->allocator);
	GstImxDefaultDmaMemory *new_imx_dma_memory = NULL;
	GstMemory *parent;

	if (size == -1)
	{
		size = imx_dma_buffer_get_size(imx_dma_memory->dmabuffer);
		size = (size > offset) ? (size - offset) : 0;
	}

	if ((parent = memory->parent) == NULL)
		parent = memory;

	new_imx_dma_memory = g_slice_alloc0(sizeof(GstImxDefaultDmaMemory));
	if (G_UNLIKELY(new_imx_dma_memory == NULL))
	{
		GST_ERROR_OBJECT(imx_default_allocator, "could not allocate slice for GstImxDefaultDmaMemory copy");
		goto cleanup;
	}

	gst_memory_init(GST_MEMORY_CAST(new_imx_dma_memory), GST_MINI_OBJECT_FLAGS(parent) | GST_MINI_OBJECT_FLAG_LOCK_READONLY | GST_MEMORY_FLAG_PHYSICALLY_CONTIGUOUS, memory->allocator, parent, memory->maxsize, memory->align, memory->offset + offset, size);

	new_imx_dma_memory->dmabuffer = imx_dma_memory->dmabuffer;

finish:
	return GST_MEMORY_CAST(new_imx_dma_memory);

cleanup:
	if (new_imx_dma_memory != NULL)
	{
		g_slice_free1(sizeof(GstImxDefaultDmaMemory), new_imx_dma_memory);
		new_imx_dma_memory = NULL;
	}

	goto finish;
}


static gboolean gst_imx_default_allocator_is_span(G_GNUC_UNUSED GstMemory *memory1, G_GNUC_UNUSED GstMemory *memory2, G_GNUC_UNUSED gsize *offset)
{
	/* We cannot reliably detect spans with physically contiguous memory blocks,
	 * since the whole notion of "span" is ambiguous with such memory. Two blocks
	 * may be spans (= they may be contiguous) in the physical address space but
	 * not in the virtual address space, and vice versa. */
	return FALSE;
}


/**
 * gst_imx_default_allocator_new:
 *
 * Creates a new #GstAllocator using the libimxdmabuffer default allocator.
 *
 * Returns: (transfer full) (nullable): Newly created allocator, or NULL in case of failure.
 */
GstAllocator* gst_imx_default_allocator_new(void)
{
	GstImxDefaultAllocator *imx_default_allocator;
	int error = 0;

	imx_default_allocator = GST_IMX_DEFAULT_ALLOCATOR_CAST(g_object_new(gst_imx_default_allocator_get_type(), NULL));
	imx_default_allocator->imxdmabuffer_allocator = imx_dma_buffer_allocator_new(&error);

	if (imx_default_allocator->imxdmabuffer_allocator == NULL)
	{
		GST_ERROR_OBJECT(imx_default_allocator, "could not create default i.MX DMA allocator: %s (%d)", strerror(error), error);
		gst_object_unref(GST_OBJECT(imx_default_allocator));
		return NULL;
	}

	/* Clear floating flag */
	gst_object_ref_sink(GST_OBJECT(imx_default_allocator));

	return GST_ALLOCATOR_CAST(imx_default_allocator);
}
