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

/**
 * SECTION:gstimxionallocator
 * @title: GstImxDmaBufAllocator
 * @short_description: Base class for DMA-BUF backed allocators using libimxdmabuffer
 * @see_also: #GstMemory, #GstPhysMemoryAllocator, #GstImxDmaBufferAllocator
 */
#include "config.h"

#include <assert.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <gst/gst.h>
#include <gst/allocators/allocators.h>
#include <imxdmabuffer/imxdmabuffer.h>
#include "gstimxdmabufferallocator.h"
#include "gstimxdmabufallocator.h"

#include "gstimxdmaheapallocator.h"
#include "gstimxionallocator.h"


GST_DEBUG_CATEGORY_STATIC(imx_dmabuf_allocator_debug);
#define GST_CAT_DEFAULT imx_dmabuf_allocator_debug


#define GST_IMX_DMABUF_MEMORY_TYPE "ImxDmaBufMemory"


/* We store the ImxDmaBuffer (or rather, a derived type called InternalImxDmaBuffer)
 * as a qdata in the GstMemory. */
static GQuark gst_imx_dmabuf_memory_internal_imxdmabuffer_quark;


static void gst_imx_dmabuf_allocator_phys_mem_allocator_iface_init(gpointer iface, gpointer iface_data);
static guintptr gst_imx_dmabuf_allocator_get_phys_addr(GstPhysMemoryAllocator *allocator, GstMemory *mem);

static void gst_imx_dmabuf_allocator_dma_buffer_allocator_iface_init(gpointer iface, gpointer iface_data);
static ImxDmaBuffer* gst_imx_dmabuf_allocator_get_dma_buffer(GstImxDmaBufferAllocator *allocator, GstMemory *memory);


struct _GstImxDmaBufAllocatorPrivate
{
	gboolean active;
};


G_DEFINE_ABSTRACT_TYPE_WITH_CODE(
	GstImxDmaBufAllocator, gst_imx_dmabuf_allocator, GST_TYPE_DMABUF_ALLOCATOR,
	G_IMPLEMENT_INTERFACE(GST_TYPE_PHYS_MEMORY_ALLOCATOR, gst_imx_dmabuf_allocator_phys_mem_allocator_iface_init)
	G_IMPLEMENT_INTERFACE(GST_TYPE_IMX_DMA_BUFFER_ALLOCATOR, gst_imx_dmabuf_allocator_dma_buffer_allocator_iface_init)
	G_ADD_PRIVATE(GstImxDmaBufAllocator)
)

static void gst_imx_dmabuf_allocator_dispose(GObject *object);

static GstMemory* gst_imx_dmabuf_allocator_alloc(GstAllocator *allocator, gsize size, GstAllocationParams *params);
static void gst_imx_dmabuf_allocator_free(GstAllocator* allocator, GstMemory *memory);

static gboolean gst_imx_dmabuf_allocator_activate(GstImxDmaBufAllocator *imx_dmabuf_allocator);

static GstMemory * gst_imx_dmabuf_allocator_mem_copy(GstMemory *memory, gssize offset, gssize size);
static gboolean gst_imx_dmabuf_allocator_mem_is_span(GstMemory *memory1, GstMemory *memory2, gsize *offset);
static gpointer gst_imx_dmabuf_allocator_mem_map_full(GstMemory *memory, GstMapInfo *info, gsize maxsize);
static void gst_imx_dmabuf_allocator_mem_unmap_full(GstMemory *memory, GstMapInfo *info);


static void gst_imx_dmabuf_allocator_class_init(GstImxDmaBufAllocatorClass *klass)
{
	GObjectClass *object_class;
	GstAllocatorClass *allocator_class;

	GST_DEBUG_CATEGORY_INIT(imx_dmabuf_allocator_debug, "imxdmabufallocator", 0, "physical memory allocator which allocates DMA-BUF memory");

	gst_imx_dmabuf_memory_internal_imxdmabuffer_quark = g_quark_from_static_string("gst-imxdmabuffer-dmabuf-memory");

	object_class = G_OBJECT_CLASS(klass);
	allocator_class = GST_ALLOCATOR_CLASS(klass);

	object_class->dispose = GST_DEBUG_FUNCPTR(gst_imx_dmabuf_allocator_dispose);
	allocator_class->alloc = GST_DEBUG_FUNCPTR(gst_imx_dmabuf_allocator_alloc);
	allocator_class->free = GST_DEBUG_FUNCPTR(gst_imx_dmabuf_allocator_free);

	klass->activate = NULL;
	klass->get_allocator = NULL;
}


static void gst_imx_dmabuf_allocator_init(GstImxDmaBufAllocator *imx_dmabuf_allocator)
{
	GstAllocator *allocator = GST_ALLOCATOR(imx_dmabuf_allocator);

	imx_dmabuf_allocator->priv = gst_imx_dmabuf_allocator_get_instance_private(imx_dmabuf_allocator);
	imx_dmabuf_allocator->priv->active = FALSE;

	allocator->mem_type = GST_IMX_DMABUF_MEMORY_TYPE;
	allocator->mem_copy = GST_DEBUG_FUNCPTR(gst_imx_dmabuf_allocator_mem_copy);
	allocator->mem_is_span = GST_DEBUG_FUNCPTR(gst_imx_dmabuf_allocator_mem_is_span);
	allocator->mem_map_full = GST_DEBUG_FUNCPTR(gst_imx_dmabuf_allocator_mem_map_full);
	allocator->mem_unmap_full = GST_DEBUG_FUNCPTR(gst_imx_dmabuf_allocator_mem_unmap_full);

	GST_TRACE_OBJECT(imx_dmabuf_allocator, "new i.MX DMA-BUF GstAllocator %p", (gpointer)imx_dmabuf_allocator);
}


static void gst_imx_dmabuf_allocator_dispose(GObject *object)
{
	GstImxDmaBufAllocator *self = GST_IMX_DMABUF_ALLOCATOR(object);
	GST_TRACE_OBJECT(self, "finalizing i.MX DMA-BUF GstAllocator %p", (gpointer)self);
	G_OBJECT_CLASS(gst_imx_dmabuf_allocator_parent_class)->dispose(object);
}


static void gst_imx_dmabuf_allocator_phys_mem_allocator_iface_init(gpointer iface, G_GNUC_UNUSED gpointer iface_data)
{
	GstPhysMemoryAllocatorInterface *phys_mem_allocator_iface = (GstPhysMemoryAllocatorInterface *)iface;
	phys_mem_allocator_iface->get_phys_addr = GST_DEBUG_FUNCPTR(gst_imx_dmabuf_allocator_get_phys_addr);
}


static guintptr gst_imx_dmabuf_allocator_get_phys_addr(GstPhysMemoryAllocator *allocator, GstMemory *mem)
{
	gpointer qdata;

	qdata = gst_mini_object_get_qdata(GST_MINI_OBJECT_CAST(mem), gst_imx_dmabuf_memory_internal_imxdmabuffer_quark);
	if (G_UNLIKELY(qdata == NULL))
	{
		GST_WARNING_OBJECT(allocator, "GstMemory object %p does not contain imxionbuffer qdata; returning 0 as physical address", (gpointer)mem);
		return 0;
	}

	return imx_dma_buffer_get_physical_address((ImxDmaBuffer *)qdata) + mem->offset;
}


static void gst_imx_dmabuf_allocator_dma_buffer_allocator_iface_init(gpointer iface, G_GNUC_UNUSED gpointer iface_data)
{
	GstImxDmaBufferAllocatorInterface *imx_dma_buffer_allocator_iface = (GstImxDmaBufferAllocatorInterface *)iface;
	imx_dma_buffer_allocator_iface->get_dma_buffer = GST_DEBUG_FUNCPTR(gst_imx_dmabuf_allocator_get_dma_buffer);
}


static ImxDmaBuffer* get_dma_buffer_from_memory(GstMemory *memory)
{
	gpointer qdata;
	qdata = gst_mini_object_get_qdata(GST_MINI_OBJECT_CAST(memory), gst_imx_dmabuf_memory_internal_imxdmabuffer_quark);
	return ((ImxDmaBuffer *)qdata);
}


static ImxDmaBuffer* gst_imx_dmabuf_allocator_get_dma_buffer(GstImxDmaBufferAllocator *allocator, GstMemory *memory)
{
	ImxDmaBuffer *imx_dma_buffer = get_dma_buffer_from_memory(memory);
	if (G_UNLIKELY(imx_dma_buffer == NULL))
	{
		GST_ERROR_OBJECT(allocator, "GstMemory object %p does not contain ImxDmaBufMemory qdata", (gpointer)memory);
		return NULL;
	}
	return imx_dma_buffer;
}


static GstMemory* gst_imx_dmabuf_allocator_alloc(GstAllocator *allocator, gsize size, GstAllocationParams *params)
{
	GstImxDmaBufAllocator *self = GST_IMX_DMABUF_ALLOCATOR(allocator);
	GstImxDmaBufAllocatorClass *klass = GST_IMX_DMABUF_ALLOCATOR_CLASS(G_OBJECT_GET_CLASS(self));
	gsize total_size = size + params->prefix + params->padding;
	GstMemory *memory = NULL;
	size_t alignment;
	int error;
	int dmabuf_fd = -1;
	ImxDmaBuffer *imx_dma_buffer = NULL;
	ImxDmaBufferAllocator *imxdmabuffer_allocator;
	gboolean qdata_set = FALSE;

	g_assert(klass->get_allocator != NULL);

	GST_OBJECT_LOCK(self);

	if (!gst_imx_dmabuf_allocator_activate(self))
		goto error;

	imxdmabuffer_allocator = klass->get_allocator(self);
	alignment = params->align + 1;

	/* Perform the actual allocation. */
	imx_dma_buffer = imx_dma_buffer_allocate(imxdmabuffer_allocator, total_size, alignment, &error);
	if (imx_dma_buffer == NULL)
	{
		GST_ERROR_OBJECT(self, "could not allocate DMA-BUF buffer: %s (%d)", strerror(error), error);
		goto error;
	}
	dmabuf_fd = imx_dma_buffer_get_fd(imx_dma_buffer);
	g_assert(dmabuf_fd > 0);

	/* Use GST_FD_MEMORY_FLAG_DONT_CLOSE since
	 * libimxdmabuffer takes care of closing the FD. */
	memory = gst_fd_allocator_alloc(allocator, dmabuf_fd, total_size, GST_FD_MEMORY_FLAG_DONT_CLOSE);
	if (memory == NULL)
	{
		GST_ERROR_OBJECT(self, "could not allocate GstMemory with GstDmaBufAllocator");
		goto error;
	}

	gst_mini_object_set_qdata(
		GST_MINI_OBJECT_CAST(memory),
		gst_imx_dmabuf_memory_internal_imxdmabuffer_quark,
		(gpointer)imx_dma_buffer,
		(GDestroyNotify)imx_dma_buffer_deallocate
	);
	qdata_set = TRUE;

	GST_DEBUG_OBJECT(
		self,
		"allocated new DMA-BUF buffer;  FD: %d  imxdmabuffer: %p  total size: %" G_GSIZE_FORMAT "  alignment: %zu  gstmemory: %p",
		dmabuf_fd,
		(gpointer)imx_dma_buffer,
		total_size,
		alignment,
		(gpointer)memory
	);

finish:
	GST_OBJECT_UNLOCK(self);
	return memory;

error:
	if (!qdata_set && (imx_dma_buffer != NULL))
		imx_dma_buffer_deallocate(imx_dma_buffer);

	goto finish;
}


static void gst_imx_dmabuf_allocator_free(GstAllocator* allocator, GstMemory *memory)
{
	int fd = gst_dmabuf_memory_get_fd(memory);

	/* We only log the free() call here. The DMA-BUF FD is closed by
	 * the imx_dma_buffer_deallocate() call that was passed to the
	 * gst_mini_object_set_qdata() function. */
	GST_ALLOCATOR_CLASS(gst_imx_dmabuf_allocator_parent_class)->free(allocator, memory);
	GST_DEBUG_OBJECT(allocator, "freed DMA-BUF buffer %p with FD %d", (gpointer)memory, fd);
}


static gboolean gst_imx_dmabuf_allocator_activate(GstImxDmaBufAllocator *imx_dmabuf_allocator)
{
	/* must be called with object lock held */

	GstImxDmaBufAllocatorClass *klass = GST_IMX_DMABUF_ALLOCATOR_CLASS(G_OBJECT_GET_CLASS(imx_dmabuf_allocator));

	g_assert(klass->activate != NULL);

	if (imx_dmabuf_allocator->priv->active)
		return TRUE;

	if (!klass->activate(imx_dmabuf_allocator))
	{
		GST_ERROR_OBJECT(imx_dmabuf_allocator, "could not activate i.MX DMA-BUF allocator");
		return FALSE;
	}

	GST_DEBUG_OBJECT(imx_dmabuf_allocator, "i.MX DMA-BUF allocator activated");

	imx_dmabuf_allocator->priv->active = TRUE;

	return TRUE;
}


static GstMemory * gst_imx_dmabuf_allocator_mem_copy(GstMemory *original_memory, gssize offset, gssize size)
{
	GstImxDmaBufAllocator *imx_dmabuf_allocator = GST_IMX_DMABUF_ALLOCATOR(original_memory->allocator);
	ImxDmaBuffer *orig_imx_dma_buffer, *copy_imx_dma_buffer;
	GstMemory *copy_memory = NULL;
	uint8_t *mapped_src_data = NULL, *mapped_dest_data = NULL;
	int error;
	GstAllocationParams copy_params = {
		.flags = 0,
		.align = original_memory->align,
		.prefix = 0,
		.padding = 0
	};

	orig_imx_dma_buffer = get_dma_buffer_from_memory(original_memory);

	if (size == -1)
	{
		size = imx_dma_buffer_get_size(orig_imx_dma_buffer);
		size = (size > offset) ? (size - offset) : 0;
	}

	copy_memory = gst_imx_dmabuf_allocator_alloc(original_memory->allocator, size, &copy_params);
	if (G_UNLIKELY(copy_memory == NULL))
	{
		GST_ERROR_OBJECT(imx_dmabuf_allocator, "could not allocate gstmemory for copy gstmemory");
		goto error;
	}

	copy_imx_dma_buffer = get_dma_buffer_from_memory(copy_memory);

	mapped_src_data = imx_dma_buffer_map(orig_imx_dma_buffer, IMX_DMA_BUFFER_MAPPING_FLAG_READ, &error);
	if (mapped_src_data == NULL)
	{
		GST_ERROR_OBJECT(imx_dmabuf_allocator, "could not map original DMA buffer: %s (%d)", strerror(error), error);
		goto error;
	}

	mapped_dest_data = imx_dma_buffer_map(copy_imx_dma_buffer, IMX_DMA_BUFFER_MAPPING_FLAG_WRITE, &error);
	if (mapped_dest_data == NULL)
	{
		GST_ERROR_OBJECT(imx_dmabuf_allocator, "could not map new DMA buffer: %s (%d)", strerror(error), error);
		goto error;
	}

	/* TODO: Is it perhaps possible to copy over DMA instead of by using the CPU? */
	memcpy(mapped_dest_data, mapped_src_data + offset, size);

finish:
	if (mapped_src_data != NULL)
		imx_dma_buffer_unmap(orig_imx_dma_buffer);
	if (mapped_dest_data != NULL)
		imx_dma_buffer_unmap(copy_imx_dma_buffer);

	return copy_memory;

error:
	if (copy_memory != NULL)
		gst_memory_unref(copy_memory);

	goto finish;
}


static gboolean gst_imx_dmabuf_allocator_mem_is_span(G_GNUC_UNUSED GstMemory *memory1, G_GNUC_UNUSED GstMemory *memory2, G_GNUC_UNUSED gsize *offset)
{
	/* We cannot reliably detect spans with physically contiguous memory blocks,
	 * since the whole notion of "span" is ambiguous with such memory. Two blocks
	 * may be spans (= they may be contiguous) in the physical address space but
	 * not in the virtual address space, and vice versa. */
	return FALSE;
}


static gpointer gst_imx_dmabuf_allocator_mem_map_full(GstMemory *memory, GstMapInfo *info, G_GNUC_UNUSED gsize maxsize)
{
	ImxDmaBuffer *imx_dma_buffer;
	unsigned int flags = 0;
	uint8_t *mapped_virtual_address;
	int error;

	imx_dma_buffer = get_dma_buffer_from_memory(memory);
	g_assert(imx_dma_buffer != NULL);

	flags |= (info->flags & GST_MAP_READ) ? IMX_DMA_BUFFER_MAPPING_FLAG_READ : 0;
	flags |= (info->flags & GST_MAP_WRITE) ? IMX_DMA_BUFFER_MAPPING_FLAG_WRITE : 0;
	flags |= (info->flags & GST_MAP_FLAG_IMX_MANUAL_SYNC) ? IMX_DMA_BUFFER_MAPPING_FLAG_MANUAL_SYNC : 0;

	mapped_virtual_address = imx_dma_buffer_map(imx_dma_buffer, flags, &error);
	if (G_UNLIKELY(mapped_virtual_address == NULL))
	{
		GST_ERROR_OBJECT(
			memory->allocator,
			"could not map imxdmabuffer %p with FD %d: %s (%d)",
			(gpointer)imx_dma_buffer,
			imx_dma_buffer_get_fd(imx_dma_buffer),
			strerror(error), error
		);
		goto finish;
	}

	GST_LOG_OBJECT(
		memory->allocator,
		"mapped imxdmabuffer %p with FD %d, mapped virtual address: %p",
		(gpointer)imx_dma_buffer,
		imx_dma_buffer_get_fd(imx_dma_buffer),
		(gpointer)mapped_virtual_address
	);

finish:
	return mapped_virtual_address;
}


static void gst_imx_dmabuf_allocator_mem_unmap_full(GstMemory *memory, G_GNUC_UNUSED GstMapInfo *info)
{
	ImxDmaBuffer *imx_dma_buffer;

	imx_dma_buffer = get_dma_buffer_from_memory(memory);
	g_assert(imx_dma_buffer != NULL);

	GST_LOG_OBJECT(
		memory->allocator,
		"unmapped imxdmabuffer %p with FD %d",
		(gpointer)imx_dma_buffer,
		imx_dma_buffer_get_fd(imx_dma_buffer)
	);

	imx_dma_buffer_unmap(imx_dma_buffer);
}




/**** Public functions ****/


GstMemory* gst_imx_dmabuf_allocator_wrap_dmabuf(GstAllocator *allocator, int dmabuf_fd, gsize dmabuf_size)
{
	GstImxDmaBufAllocator *self = GST_IMX_DMABUF_ALLOCATOR(allocator);
	GstImxDmaBufAllocatorClass *klass = GST_IMX_DMABUF_ALLOCATOR_CLASS(G_OBJECT_GET_CLASS(self));
	imx_physical_address_t physical_address;
	GstMemory *memory = NULL;
	ImxWrappedDmaBuffer *wrapped_dma_buffer = NULL;

	g_assert(dmabuf_fd > 0);
	g_assert(dmabuf_size > 0);
	g_assert(klass->get_physical_address != NULL);

	GST_OBJECT_LOCK(self);

	physical_address = klass->get_physical_address(self, dmabuf_fd);
	if (physical_address == 0)
	{
		GST_ERROR_OBJECT(self, "could not open get physical address from dmabuf FD %d", dmabuf_fd);
		goto error;
	}
	GST_DEBUG_OBJECT(self, "got physical address %" IMX_PHYSICAL_ADDRESS_FORMAT " from DMA-BUF buffer", physical_address);

	if (!gst_imx_dmabuf_allocator_activate(self))
		goto error;

	wrapped_dma_buffer = g_malloc(sizeof(ImxWrappedDmaBuffer));
	imx_dma_buffer_init_wrapped_buffer(wrapped_dma_buffer);
	wrapped_dma_buffer->fd = dmabuf_fd;
	wrapped_dma_buffer->size = dmabuf_size;
	wrapped_dma_buffer->physical_address = physical_address;

	/* Use GST_FD_MEMORY_FLAG_DONT_CLOSE since
	 * libimxdmabuffer takes care of closing the FD. */
	memory = gst_fd_allocator_alloc(allocator, dmabuf_fd, dmabuf_size, GST_FD_MEMORY_FLAG_DONT_CLOSE);
	if (memory == NULL)
	{
		GST_ERROR_OBJECT(self, "could not allocate GstMemory with GstDmaBufAllocator");
		goto error;
	}

	gst_mini_object_set_qdata(
		GST_MINI_OBJECT_CAST(memory),
		gst_imx_dmabuf_memory_internal_imxdmabuffer_quark,
		(gpointer)wrapped_dma_buffer,
		g_free
	);

	GST_DEBUG_OBJECT(
		self,
		"wrapped existing DMA-BUF into an imxdmabuffer:  DMA-BUF FD: %d  imxdmabuffer: %p  DMA-BUF size: %" G_GSIZE_FORMAT "  gstmemory: %p",
		dmabuf_fd,
		(gpointer)wrapped_dma_buffer,
		dmabuf_size,
		(gpointer)memory
	);

finish:
	GST_OBJECT_UNLOCK(self);
	return memory;

error:
	g_free(wrapped_dma_buffer);

	goto finish;
}


gboolean gst_imx_dmabuf_allocator_is_active(GstAllocator *allocator)
{
	GstImxDmaBufAllocator *self;
	gboolean active;

	g_assert(allocator != NULL);
	self = GST_IMX_DMABUF_ALLOCATOR(allocator);

	GST_OBJECT_LOCK(self);
	active = self->priv->active;
	GST_OBJECT_UNLOCK(self);

	return active;
}


GstAllocator* gst_imx_dmabuf_allocator_new(void)
{
#if defined(WITH_GST_DMA_HEAP_ALLOCATOR)
	if (g_strcmp0(g_getenv("GSTREAMER_IMX_DISABLE_DMA_HEAP_ALLOCATOR"), "1") != 0)
		return gst_imx_dma_heap_allocator_new();
#endif

#if defined(WITH_GST_ION_ALLOCATOR)
	if (g_strcmp0(g_getenv("GSTREAMER_IMX_DISABLE_ION_ALLOCATOR"), "1") != 0)
		return gst_imx_ion_allocator_new();
#endif

	/* No DMA-BUF capable allocator enabled. In such a case, calling this is an error. */
	g_assert_not_reached();
	return NULL;
}
