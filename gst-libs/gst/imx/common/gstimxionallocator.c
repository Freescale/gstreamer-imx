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
 * SECTION:gstimxionallocator
 * @title: GstImxIonAllocator
 * @short_description: ImxDmabuffer-backed allocator using the ION libimxdmabuffer allocator
 * @see_also: #GstMemory, #GstPhysMemoryAllocator, #GstImxDmaBufferAllocator
 */
#include <assert.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <gst/gst.h>
#include <gst/allocators/allocators.h>
#include <imxdmabuffer/imxdmabuffer.h>
#include <imxdmabuffer/imxdmabuffer_ion_allocator.h>
#include "gstimxdmabufferallocator.h"
#include "gstimxionallocator.h"


GST_DEBUG_CATEGORY_STATIC(imx_ion_allocator_debug);
#define GST_CAT_DEFAULT imx_ion_allocator_debug


enum
{
	PROP_0,
	PROP_EXTERNAL_ION_FD,
	PROP_ION_HEAP_ID_MASK,
	PROP_ION_HEAP_FLAGS
};


#define DEFAULT_EXTERNAL_ION_FD   IMX_DMA_BUFFER_ION_ALLOCATOR_DEFAULT_ION_FD
#define DEFAULT_ION_HEAP_ID_MASK  IMX_DMA_BUFFER_ION_ALLOCATOR_DEFAULT_HEAP_ID_MASK
#define DEFAULT_ION_HEAP_FLAGS    IMX_DMA_BUFFER_ION_ALLOCATOR_DEFAULT_HEAP_FLAGS

#define GST_IMX_ION_MEMORY_TYPE "ImxIonDmaMemory"


/* We store the ImxDmaBuffer (or rather, a derived type called ImxDmaBufferIonBuffer)
 * as a qdata in the GstMemory. */
static GQuark gst_imx_ion_memory_imxionbuffer_quark;


/* ImxDmaBuffer derivative to be able to fulfill all requirements of the
 * GstDmaBufAllocator as well as those of the GstPhysMemoryAllocatorInterface
 * and GstImxDmaBufferAllocatorInterface. */
typedef struct
{
	ImxDmaBuffer parent;
	GstMemory *gstmemory;
	imx_physical_address_t physical_address;
	int dmabuf_fd;
	size_t size;

	GstMapInfo map_info;
	int mapping_refcount;
}
ImxDmaBufferIonBuffer;


typedef struct
{
	ImxDmaBufferAllocator parent;
}
ImxDmaBufferIonBufferAllocator;


struct _GstImxIonAllocator
{
	GstDmaBufAllocator parent;

	ImxDmaBufferIonBufferAllocator imxdmabuffer_allocator;

	/* The /dev/ion FD. This can be a FD from an internal open() call in
	 * gst_imx_ion_allocator_gstalloc_alloc(), or it may be an FD that
	 * was supplied over the GObject "external-ion-fd" property. In the
	 * latter case, it is important to know that this FD is not internal
	 * to avoid calling close() on it. */
	gboolean ion_fd_is_internal;
	int ion_fd;

	/* If TRUE, then the allocator is now in use. This happens once
	 * the first GstMemory is allocated with it. Once the allocator is
	 * active, setting its GObject properties is no longer allowed.
	 * This prevents complicated and unrealistic corner cases from
	 * happening, like replacing the FD during operation. */
	gboolean active;

	/* Bitmask for selecting ION heaps during allocations. See the
	 * libimxdmabuffer ION allocator documentation for more. */
	guint ion_heap_id_mask;
	/* Flags to pass to the ION heap during allocations. See the
	 * libimxdmabuffer ION allocator documentation for more. */
	guint ion_heap_flags;
};


struct _GstImxIonAllocatorClass
{
	GstDmaBufAllocatorClass parent_class;
};


static void gst_imx_ion_allocator_phys_mem_allocator_iface_init(gpointer iface, gpointer iface_data);
static guintptr gst_imx_ion_allocator_get_phys_addr(GstPhysMemoryAllocator *allocator, GstMemory *mem);

static void gst_imx_ion_allocator_dma_buffer_allocator_iface_init(gpointer iface, gpointer iface_data);
static ImxDmaBuffer* gst_imx_ion_allocator_get_dma_buffer(GstImxDmaBufferAllocator *allocator, GstMemory *memory);


G_DEFINE_TYPE_WITH_CODE(
	GstImxIonAllocator, gst_imx_ion_allocator, GST_TYPE_DMABUF_ALLOCATOR,
	G_IMPLEMENT_INTERFACE(GST_TYPE_PHYS_MEMORY_ALLOCATOR,    gst_imx_ion_allocator_phys_mem_allocator_iface_init)
	G_IMPLEMENT_INTERFACE(GST_TYPE_IMX_DMA_BUFFER_ALLOCATOR, gst_imx_ion_allocator_dma_buffer_allocator_iface_init)
)

static void gst_imx_ion_allocator_dispose(GObject *object);
static void gst_imx_ion_allocator_set_property(GObject *object, guint prop_id, GValue const *value, GParamSpec *pspec);
static void gst_imx_ion_allocator_get_property(GObject *object, guint prop_id, GValue *value, GParamSpec *pspec);

static GstMemory* gst_imx_ion_allocator_gstalloc_alloc(GstAllocator *allocator, gsize size, GstAllocationParams *params);


/* Integration of the libimxdmabuffer ION allocator with GstDmaBufAllocator
 * is not straightforward. This is because the GstDmaBufAllocator closes the
 * DMA-BUF FDs on its own, and because both the GstAllocator/GstMemory and
 * the libimxdmabuffer allocator APIs have functions for (un)mapping buffers,
 * and both are in use (imx_dma_buffer_map() and friends may be used inside
 * libimxvpuapi for example).
 *
 * The solution is to create our own internal libimxdmabuffer allocator that
 * is never intended to be used from the outside. This allocator odes not
 * actually have allocate/deallocate/destroy functionality, and only exists
 * so that we can (un)map ImxDmaBuffer instances properly over both APIs.
 * To that end, this allocator internally maps using gst_memory_map().
 *
 * This "dummy" allocator is ImxDmaBufferIonBufferAllocator. It is set up
 * in gst_imx_ion_allocator_init(). The function declarations below are
 * specific to this dummy allocator.
 *
 * This design also allows us to combine GstDmaBufAllocator, the ION
 * allocator from libimxdmabuffer, and the GstPhysMemoryAllocator and
 * GstImxDmaBufferAllocator interfaces into one GStreamer allocator.
 */

static void gst_imx_ion_allocator_imxdmabufalloc_destroy(ImxDmaBufferAllocator *allocator);

static ImxDmaBuffer* gst_imx_ion_allocator_imxdmabufalloc_allocate(ImxDmaBufferAllocator *allocator, size_t size, size_t alignment, int *error);
static void gst_imx_ion_allocator_imxdmabufalloc_deallocate(ImxDmaBufferAllocator *allocator, ImxDmaBuffer *buffer);

static uint8_t* gst_imx_ion_allocator_imxdmabufalloc_map(ImxDmaBufferAllocator *allocator, ImxDmaBuffer *buffer, unsigned int flags, int *error);
static void gst_imx_ion_allocator_imxdmabufalloc_unmap(ImxDmaBufferAllocator *allocator, ImxDmaBuffer *buffer);

static imx_physical_address_t gst_imx_ion_allocator_imxdmabufalloc_get_physical_address(ImxDmaBufferAllocator *allocator, ImxDmaBuffer *buffer);
static int gst_imx_ion_allocator_imxdmabufalloc_get_fd(ImxDmaBufferAllocator *allocator, ImxDmaBuffer *buffer);

static size_t gst_imx_ion_allocator_imxdmabufalloc_get_size(ImxDmaBufferAllocator *allocator, ImxDmaBuffer *buffer);




/**** GstImxIonAllocator internal function definitions ****/

static void gst_imx_ion_allocator_class_init(GstImxIonAllocatorClass *klass)
{
	GObjectClass *object_class;
	GstAllocatorClass *allocator_class;

	GST_DEBUG_CATEGORY_INIT(imx_ion_allocator_debug, "imxionallocator", 0, "physical memory allocator based on ION and DMA-BUF");

	gst_imx_ion_memory_imxionbuffer_quark = g_quark_from_static_string("gst-imx-ion-memory-imxionbuffer");

	object_class = G_OBJECT_CLASS(klass);
	allocator_class = GST_ALLOCATOR_CLASS(klass);

	object_class->dispose = GST_DEBUG_FUNCPTR(gst_imx_ion_allocator_dispose);
	object_class->set_property = GST_DEBUG_FUNCPTR(gst_imx_ion_allocator_set_property);
	object_class->get_property = GST_DEBUG_FUNCPTR(gst_imx_ion_allocator_get_property);
	allocator_class->alloc = GST_DEBUG_FUNCPTR(gst_imx_ion_allocator_gstalloc_alloc);

	g_object_class_install_property(
		object_class,
		PROP_EXTERNAL_ION_FD,
		g_param_spec_int(
			"external-ion-fd",
			"External ION FD",
			"External, already existing ION file descriptor to use (-1 = internally open /dev/ion and get an FD for it)",
			-1, G_MAXINT,
			DEFAULT_EXTERNAL_ION_FD,
			G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS
		)
	);
	g_object_class_install_property(
		object_class,
		PROP_ION_HEAP_ID_MASK,
		g_param_spec_uint(
			"ion-heap-id-mask",
			"ION heap ID mask",
			"Mask of ION heap IDs to allocate from",
			0, G_MAXUINT,
			DEFAULT_ION_HEAP_ID_MASK,
			G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS
		)
	);
	g_object_class_install_property(
		object_class,
		PROP_ION_HEAP_FLAGS,
		g_param_spec_uint(
			"ion-heap-flags",
			"ION heap flags",
			"Flags to pass to the ION heap (0 = automatically query for a heap that allocates via the DMA API; requires i.MX kernel 4.14.34 or newer)",
			0, G_MAXUINT,
			DEFAULT_ION_HEAP_FLAGS,
			G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS
		)
	);
}


static void gst_imx_ion_allocator_init(GstImxIonAllocator *imx_ion_allocator)
{
	GstAllocator *allocator = GST_ALLOCATOR(imx_ion_allocator);
	allocator->mem_type = GST_IMX_ION_MEMORY_TYPE;

	imx_ion_allocator->ion_fd_is_internal = FALSE;
	imx_ion_allocator->ion_fd = -1;

	imx_ion_allocator->active = FALSE;

	imx_ion_allocator->ion_heap_id_mask = DEFAULT_ION_HEAP_ID_MASK;
	imx_ion_allocator->ion_heap_flags = DEFAULT_ION_HEAP_FLAGS;

	imx_ion_allocator->imxdmabuffer_allocator.parent.destroy              = gst_imx_ion_allocator_imxdmabufalloc_destroy;
	imx_ion_allocator->imxdmabuffer_allocator.parent.allocate             = gst_imx_ion_allocator_imxdmabufalloc_allocate;
	imx_ion_allocator->imxdmabuffer_allocator.parent.deallocate           = gst_imx_ion_allocator_imxdmabufalloc_deallocate;
	imx_ion_allocator->imxdmabuffer_allocator.parent.map                  = gst_imx_ion_allocator_imxdmabufalloc_map;
	imx_ion_allocator->imxdmabuffer_allocator.parent.unmap                = gst_imx_ion_allocator_imxdmabufalloc_unmap;
	imx_ion_allocator->imxdmabuffer_allocator.parent.get_physical_address = gst_imx_ion_allocator_imxdmabufalloc_get_physical_address;
	imx_ion_allocator->imxdmabuffer_allocator.parent.get_fd               = gst_imx_ion_allocator_imxdmabufalloc_get_fd;
	imx_ion_allocator->imxdmabuffer_allocator.parent.get_size             = gst_imx_ion_allocator_imxdmabufalloc_get_size;

	GST_TRACE_OBJECT(imx_ion_allocator, "new ION GstAllocator %p", (gpointer)imx_ion_allocator);
}


static void gst_imx_ion_allocator_dispose(GObject *object)
{
	GstImxIonAllocator *imx_ion_allocator = GST_IMX_ION_ALLOCATOR(object);

	GST_TRACE_OBJECT(imx_ion_allocator, "finalizing ION GstAllocator %p", (gpointer)imx_ion_allocator);

	if ((imx_ion_allocator->ion_fd >= 0) && imx_ion_allocator->ion_fd_is_internal)
	{
		close(imx_ion_allocator->ion_fd);
		imx_ion_allocator->ion_fd = -1;
	}

	G_OBJECT_CLASS(gst_imx_ion_allocator_parent_class)->dispose(object);
}


static void gst_imx_ion_allocator_set_property(GObject *object, guint prop_id, GValue const *value, GParamSpec *pspec)
{
	GstImxIonAllocator *imx_ion_allocator = GST_IMX_ION_ALLOCATOR(object);

	GST_OBJECT_LOCK(object);
	if (imx_ion_allocator->active)
	{
		GST_OBJECT_UNLOCK(object);
		GST_ERROR_OBJECT(imx_ion_allocator, "cannot set property; allocator already active");
		return;
	}

	switch (prop_id)
	{
		case PROP_EXTERNAL_ION_FD:
		{
			imx_ion_allocator->ion_fd = g_value_get_int(value);
			imx_ion_allocator->ion_fd_is_internal = (imx_ion_allocator->ion_fd < 0);
			GST_DEBUG_OBJECT(imx_ion_allocator, "set ION FD to %d", imx_ion_allocator->ion_fd);
			GST_OBJECT_UNLOCK(object);
			break;
		}

		case PROP_ION_HEAP_ID_MASK:
			imx_ion_allocator->ion_heap_id_mask = g_value_get_uint(value);
			GST_OBJECT_UNLOCK(object);
			break;

		case PROP_ION_HEAP_FLAGS:
			imx_ion_allocator->ion_heap_flags = g_value_get_uint(value);
			GST_OBJECT_UNLOCK(object);
			break;

		default:
			GST_OBJECT_UNLOCK(object);
			G_OBJECT_WARN_INVALID_PROPERTY_ID(object, prop_id, pspec);
			break;
	}
}


static void gst_imx_ion_allocator_get_property(GObject *object, guint prop_id, GValue *value, GParamSpec *pspec)
{
	GstImxIonAllocator *imx_ion_allocator = GST_IMX_ION_ALLOCATOR(object);

	switch (prop_id)
	{
		case PROP_EXTERNAL_ION_FD:
			GST_OBJECT_LOCK(object);
			g_value_set_int(value, imx_ion_allocator->ion_fd);
			GST_OBJECT_UNLOCK(object);
			break;

		case PROP_ION_HEAP_ID_MASK:
			GST_OBJECT_LOCK(object);
			g_value_set_uint(value, imx_ion_allocator->ion_heap_id_mask);
			GST_OBJECT_UNLOCK(object);
			break;

		case PROP_ION_HEAP_FLAGS:
			GST_OBJECT_LOCK(object);
			g_value_set_uint(value, imx_ion_allocator->ion_heap_flags);
			GST_OBJECT_UNLOCK(object);
			break;

		default:
			G_OBJECT_WARN_INVALID_PROPERTY_ID(object, prop_id, pspec);
			break;
	}
}




/**** GstPhysMemoryAllocatorInterface internal function definitions ****/

static void gst_imx_ion_allocator_phys_mem_allocator_iface_init(gpointer iface, G_GNUC_UNUSED gpointer iface_data)
{
	GstPhysMemoryAllocatorInterface *phys_mem_allocator_iface = (GstPhysMemoryAllocatorInterface *)iface;
	phys_mem_allocator_iface->get_phys_addr = GST_DEBUG_FUNCPTR(gst_imx_ion_allocator_get_phys_addr);
}


static guintptr gst_imx_ion_allocator_get_phys_addr(GstPhysMemoryAllocator *allocator, GstMemory *mem)
{
	gpointer qdata;

	qdata = gst_mini_object_get_qdata(GST_MINI_OBJECT_CAST(mem), gst_imx_ion_memory_imxionbuffer_quark);
	if (G_LIKELY(qdata == NULL))
	{
		GST_WARNING_OBJECT(allocator, "GstMemory object %p does not contain imxionbuffer qdata; returning 0 as physical address", (gpointer)mem);
		return 0;
	}

	return ((ImxDmaBufferIonBuffer *)qdata)->physical_address + mem->offset;
}


static void gst_imx_ion_allocator_dma_buffer_allocator_iface_init(gpointer iface, G_GNUC_UNUSED gpointer iface_data)
{
	GstImxDmaBufferAllocatorInterface *imx_dma_buffer_allocator_iface = (GstImxDmaBufferAllocatorInterface *)iface;
	imx_dma_buffer_allocator_iface->get_dma_buffer = GST_DEBUG_FUNCPTR(gst_imx_ion_allocator_get_dma_buffer);
}




/**** GstImxDmaBufferAllocatorInterface internal function definitions ****/

static ImxDmaBuffer* gst_imx_ion_allocator_get_dma_buffer(GstImxDmaBufferAllocator *allocator, GstMemory *memory)
{
	gpointer qdata;

	qdata = gst_mini_object_get_qdata(GST_MINI_OBJECT_CAST(memory), gst_imx_ion_memory_imxionbuffer_quark);
	if (G_LIKELY(qdata == NULL))
	{
		GST_ERROR_OBJECT(allocator, "GstMemory object %p does not contain imxionbuffer qdata", (gpointer)memory);
		return NULL;
	}

	return ((ImxDmaBuffer *)qdata);
}




/**** GstAllocator internal function definitions ****/

static GstMemory* gst_imx_ion_allocator_gstalloc_alloc(GstAllocator *allocator, gsize size, GstAllocationParams *params)
{
	int dmabuf_fd = -1;
	int error = 0;
	GstMemory *memory = NULL;
	imx_physical_address_t physical_address;
	ImxDmaBufferIonBuffer *imx_ion_buffer;
	GstImxIonAllocator *imx_ion_allocator = GST_IMX_ION_ALLOCATOR(allocator);
	gsize total_size = size + params->prefix + params->padding;
	size_t alignment;

	assert(imx_ion_allocator != NULL);

	GST_OBJECT_LOCK(imx_ion_allocator);

	if (imx_ion_allocator->ion_fd < 0)
	{
		imx_ion_allocator->ion_fd = open("/dev/ion", O_RDONLY);
		if (imx_ion_allocator->ion_fd < 0)
		{
			GST_ERROR_OBJECT(imx_ion_allocator, "could not open ION allocator device node: %s (%d)", strerror(errno), errno);
			goto finish;
		}

		GST_DEBUG_OBJECT(imx_ion_allocator, "opened ION device node, FD: %d", imx_ion_allocator->ion_fd);
	}

	/* We opened the FD, or at least are now using it. Mark
	 * the allocator as active to prevent other FDs from
	 * being set via GObject properties in set_property(). */
	imx_ion_allocator->active = TRUE;

	/* TODO: is this the correct way to calculate alignment?
	alignment = (params->align > 1) ? (params->align - 1) : 0; */
	alignment = params->align + 1;

	/* Perform the actual allocation. */
	dmabuf_fd = imx_dma_buffer_ion_allocate_dmabuf(imx_ion_allocator->ion_fd, total_size, alignment, imx_ion_allocator->ion_heap_id_mask, imx_ion_allocator->ion_heap_flags, &error);
	if (dmabuf_fd < 0)
	{
		GST_ERROR_OBJECT(imx_ion_allocator, "could not open ION allocator device node: %s (%d)", strerror(error), error);
		goto finish;
	}
	GST_DEBUG_OBJECT(imx_ion_allocator, "allocated new DMA-BUF buffer with FD %d", dmabuf_fd);

	/* Now that we've got the buffer, retrieve its physical address. */
	physical_address = imx_dma_buffer_ion_get_physical_address_from_dmabuf_fd(imx_ion_allocator->ion_fd, dmabuf_fd, &error);
	if (physical_address == 0)
	{
		close(dmabuf_fd);
		GST_ERROR_OBJECT(imx_ion_allocator, "could not open get physical address from dmabuf FD: %s (%d)", strerror(error), error);
		goto finish;
	}
	GST_DEBUG_OBJECT(imx_ion_allocator, "got physical address %" IMX_PHYSICAL_ADDRESS_FORMAT " from DMA-BUF buffer", physical_address);

	memory = gst_dmabuf_allocator_alloc(allocator, dmabuf_fd, total_size);
	if (!memory)
	{
		close(dmabuf_fd);
		GST_ERROR_OBJECT(imx_ion_allocator, "could not allocate GstMemory with GstDmaBufAllocator");
		goto finish;
	}

	imx_ion_buffer = g_malloc0(sizeof(ImxDmaBufferIonBuffer));
	imx_ion_buffer->parent.allocator = (ImxDmaBufferAllocator *)&(imx_ion_allocator->imxdmabuffer_allocator);
	imx_ion_buffer->gstmemory = memory;
	imx_ion_buffer->physical_address = physical_address;
	imx_ion_buffer->dmabuf_fd = dmabuf_fd;
	imx_ion_buffer->size = total_size;
	imx_ion_buffer->mapping_refcount = 0;
	gst_mini_object_set_qdata(GST_MINI_OBJECT_CAST(memory), gst_imx_ion_memory_imxionbuffer_quark, (gpointer)imx_ion_buffer, g_free);

finish:
	GST_OBJECT_UNLOCK(imx_ion_allocator);
	return memory;
}




/**** ImxDmaBufferIonBufferAllocator internal function definitions ****/

static void gst_imx_ion_allocator_imxdmabufalloc_destroy(G_GNUC_UNUSED ImxDmaBufferAllocator *allocator)
{
}


static ImxDmaBuffer* gst_imx_ion_allocator_imxdmabufalloc_allocate(G_GNUC_UNUSED ImxDmaBufferAllocator *allocator, G_GNUC_UNUSED size_t size, G_GNUC_UNUSED size_t alignment, G_GNUC_UNUSED int *error)
{
	return NULL;
}


static void gst_imx_ion_allocator_imxdmabufalloc_deallocate(G_GNUC_UNUSED ImxDmaBufferAllocator *allocator, G_GNUC_UNUSED ImxDmaBuffer *buffer)
{
}


static uint8_t* gst_imx_ion_allocator_imxdmabufalloc_map(G_GNUC_UNUSED ImxDmaBufferAllocator *allocator, ImxDmaBuffer *buffer, unsigned int flags, G_GNUC_UNUSED int *error)
{
	ImxDmaBufferIonBuffer *imx_ion_buffer = (ImxDmaBufferIonBuffer *)buffer;
	if (imx_ion_buffer->mapping_refcount == 0)
	{
		GstMapFlags gstflags = 0;
		if (flags & IMX_DMA_BUFFER_MAPPING_FLAG_READ) gstflags |= GST_MAP_READ;
		if (flags & IMX_DMA_BUFFER_MAPPING_FLAG_WRITE) gstflags |= GST_MAP_WRITE;

		gst_memory_map(imx_ion_buffer->gstmemory, &(imx_ion_buffer->map_info), gstflags);
	}

	imx_ion_buffer->mapping_refcount++;

	return imx_ion_buffer->map_info.data;
}


static void gst_imx_ion_allocator_imxdmabufalloc_unmap(G_GNUC_UNUSED ImxDmaBufferAllocator *allocator, ImxDmaBuffer *buffer)
{
	ImxDmaBufferIonBuffer *imx_ion_buffer = (ImxDmaBufferIonBuffer *)buffer;

	if (imx_ion_buffer->mapping_refcount == 0)
		return;

	imx_ion_buffer->mapping_refcount--;
	if (imx_ion_buffer->mapping_refcount != 0)
		return;

	gst_memory_unmap(imx_ion_buffer->gstmemory, &(imx_ion_buffer->map_info));
}


static imx_physical_address_t gst_imx_ion_allocator_imxdmabufalloc_get_physical_address(G_GNUC_UNUSED ImxDmaBufferAllocator *allocator, ImxDmaBuffer *buffer)
{
	ImxDmaBufferIonBuffer *imx_ion_buffer = (ImxDmaBufferIonBuffer *)buffer;
	return imx_ion_buffer->physical_address;
}


static int gst_imx_ion_allocator_imxdmabufalloc_get_fd(G_GNUC_UNUSED ImxDmaBufferAllocator *allocator, ImxDmaBuffer *buffer)
{
	ImxDmaBufferIonBuffer *imx_ion_buffer = (ImxDmaBufferIonBuffer *)buffer;
	return imx_ion_buffer->dmabuf_fd;
}


static size_t gst_imx_ion_allocator_imxdmabufalloc_get_size(G_GNUC_UNUSED ImxDmaBufferAllocator *allocator, ImxDmaBuffer *buffer)
{
	ImxDmaBufferIonBuffer *imx_ion_buffer = (ImxDmaBufferIonBuffer *)buffer;
	return imx_ion_buffer->size;
}




/**** Public functions ****/

/**
 * gst_imx_ion_allocator_new:
 *
 * Creates a new #GstAllocator using the libimxdmabuffer ION allocator.
 *
 * Returns: (transfer full) (nullable): Newly created allocator, or NULL in case of failure.
 */
GstAllocator* gst_imx_ion_allocator_new(void)
{
	GstAllocator *imx_ion_allocator = GST_ALLOCATOR_CAST(g_object_new(gst_imx_ion_allocator_get_type(), NULL));

	GST_DEBUG_OBJECT(imx_ion_allocator, "created new ION i.MX DMA allocator %s", GST_OBJECT_NAME(imx_ion_allocator));

	/* Clear floating flag */
	gst_object_ref_sink(GST_OBJECT(imx_ion_allocator));

	return imx_ion_allocator;
}
