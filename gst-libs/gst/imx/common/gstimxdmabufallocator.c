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
InternalImxDmaBuffer;


static void gst_imx_dmabuf_allocator_phys_mem_allocator_iface_init(gpointer iface, gpointer iface_data);
static guintptr gst_imx_dmabuf_allocator_get_phys_addr(GstPhysMemoryAllocator *allocator, GstMemory *mem);

static void gst_imx_dmabuf_allocator_dma_buffer_allocator_iface_init(gpointer iface, gpointer iface_data);
static ImxDmaBuffer* gst_imx_dmabuf_allocator_get_dma_buffer(GstImxDmaBufferAllocator *allocator, GstMemory *memory);


G_DEFINE_ABSTRACT_TYPE_WITH_CODE(
    GstImxDmaBufAllocator, gst_imx_dmabuf_allocator, GST_TYPE_DMABUF_ALLOCATOR,
    G_IMPLEMENT_INTERFACE(GST_TYPE_PHYS_MEMORY_ALLOCATOR,    gst_imx_dmabuf_allocator_phys_mem_allocator_iface_init)
    G_IMPLEMENT_INTERFACE(GST_TYPE_IMX_DMA_BUFFER_ALLOCATOR, gst_imx_dmabuf_allocator_dma_buffer_allocator_iface_init)
)

static void gst_imx_dmabuf_allocator_dispose(GObject *object);

static gboolean gst_imx_dmabuf_allocator_activate(GstImxDmaBufAllocator *imx_dmabuf_allocator);
static GstMemory* gst_imx_dmabuf_allocator_alloc_internal(GstImxDmaBufAllocator *imx_dmabuf_allocator, int dmabuf_fd, gsize size);

static GstMemory* gst_imx_dmabuf_allocator_gstalloc_alloc(GstAllocator *allocator, gsize size, GstAllocationParams *params);
static void gst_imx_dmabuf_allocator_gstalloc_free(GstAllocator* allocator, GstMemory *memory);
static GstMemory * gst_imx_dmabuf_allocator_copy(GstMemory *memory, gssize offset, gssize size);


/* Integration of libimxdmabuffer DMA-BUF allocators with GstDmaBufAllocator
 * is not straightforward. This is because the GstDmaBufAllocator closes the
 * DMA-BUF FDs on its own, and because both the GstAllocator/GstMemory and
 * the libimxdmabuffer allocator APIs have functions for (un)mapping buffers,
 * and both are in use (imx_dma_buffer_map() and friends may be used inside
 * libimxvpuapi for example).
 *
 * The solution is to create our own internal libimxdmabuffer allocator that
 * is never intended to be used from the outside. This allocator does not
 * actually have allocate/deallocate/destroy functionality, and only exists
 * so that we can (un)map ImxDmaBuffer instances properly over both APIs.
 * To that end, this allocator internally maps using gst_memory_map().
 *
 * This "dummy" allocator is internal_imxdmabuffer_allocator. It is set up
 * in gst_imx_dmabuf_allocator_init(). The function declarations below are
 * specific to this dummy allocator.
 *
 * This design also allows us to combine GstDmaBufAllocator, the DMA-BUF
 * allocator from libimxdmabuffer, and the GstPhysMemoryAllocator and
 * GstImxDmaBufferAllocator interfaces into one GStreamer allocator.
 */

static void gst_imx_dmabuf_allocator_imxdmabufalloc_destroy(ImxDmaBufferAllocator *allocator);

static ImxDmaBuffer* gst_imx_dmabuf_allocator_imxdmabufalloc_allocate(ImxDmaBufferAllocator *allocator, size_t size, size_t alignment, int *error);
static void gst_imx_dmabuf_allocator_imxdmabufalloc_deallocate(ImxDmaBufferAllocator *allocator, ImxDmaBuffer *buffer);

static uint8_t* gst_imx_dmabuf_allocator_imxdmabufalloc_map(ImxDmaBufferAllocator *allocator, ImxDmaBuffer *buffer, unsigned int flags, int *error);
static void gst_imx_dmabuf_allocator_imxdmabufalloc_unmap(ImxDmaBufferAllocator *allocator, ImxDmaBuffer *buffer);

static imx_physical_address_t gst_imx_dmabuf_allocator_imxdmabufalloc_get_physical_address(ImxDmaBufferAllocator *allocator, ImxDmaBuffer *buffer);
static int gst_imx_dmabuf_allocator_imxdmabufalloc_get_fd(ImxDmaBufferAllocator *allocator, ImxDmaBuffer *buffer);

static size_t gst_imx_dmabuf_allocator_imxdmabufalloc_get_size(ImxDmaBufferAllocator *allocator, ImxDmaBuffer *buffer);




/**** GstImxDmaBufAllocator internal function definitions ****/


static void gst_imx_dmabuf_allocator_class_init(GstImxDmaBufAllocatorClass *klass)
{
    GObjectClass *object_class;
    GstAllocatorClass *allocator_class;

    GST_DEBUG_CATEGORY_INIT(imx_dmabuf_allocator_debug, "imxdmabufallocator", 0, "physical memory allocator which allocates DMA-BUF memory");

    gst_imx_dmabuf_memory_internal_imxdmabuffer_quark = g_quark_from_static_string("gst-imxdmabuffer-dmabuf-memory");

    object_class = G_OBJECT_CLASS(klass);
    allocator_class = GST_ALLOCATOR_CLASS(klass);

    object_class->dispose = GST_DEBUG_FUNCPTR(gst_imx_dmabuf_allocator_dispose);
    allocator_class->alloc = GST_DEBUG_FUNCPTR(gst_imx_dmabuf_allocator_gstalloc_alloc);
    allocator_class->free = GST_DEBUG_FUNCPTR(gst_imx_dmabuf_allocator_gstalloc_free);

    klass->activate = NULL;
    klass->get_physical_address = NULL;
    klass->allocate_dmabuf = NULL;
}


static void gst_imx_dmabuf_allocator_init(GstImxDmaBufAllocator *imx_dmabuf_allocator)
{
    GstAllocator *allocator = GST_ALLOCATOR(imx_dmabuf_allocator);
    allocator->mem_type = GST_IMX_DMABUF_MEMORY_TYPE;
    allocator->mem_copy = GST_DEBUG_FUNCPTR(gst_imx_dmabuf_allocator_copy);

    imx_dmabuf_allocator->active = FALSE;

    imx_dmabuf_allocator->internal_imxdmabuffer_allocator.destroy              = gst_imx_dmabuf_allocator_imxdmabufalloc_destroy;
    imx_dmabuf_allocator->internal_imxdmabuffer_allocator.allocate             = gst_imx_dmabuf_allocator_imxdmabufalloc_allocate;
    imx_dmabuf_allocator->internal_imxdmabuffer_allocator.deallocate           = gst_imx_dmabuf_allocator_imxdmabufalloc_deallocate;
    imx_dmabuf_allocator->internal_imxdmabuffer_allocator.map                  = gst_imx_dmabuf_allocator_imxdmabufalloc_map;
    imx_dmabuf_allocator->internal_imxdmabuffer_allocator.unmap                = gst_imx_dmabuf_allocator_imxdmabufalloc_unmap;
    imx_dmabuf_allocator->internal_imxdmabuffer_allocator.get_physical_address = gst_imx_dmabuf_allocator_imxdmabufalloc_get_physical_address;
    imx_dmabuf_allocator->internal_imxdmabuffer_allocator.get_fd               = gst_imx_dmabuf_allocator_imxdmabufalloc_get_fd;
    imx_dmabuf_allocator->internal_imxdmabuffer_allocator.get_size             = gst_imx_dmabuf_allocator_imxdmabufalloc_get_size;

    GST_TRACE_OBJECT(imx_dmabuf_allocator, "new i.MX DMA-BUF GstAllocator %p", (gpointer)imx_dmabuf_allocator);
}


static void gst_imx_dmabuf_allocator_dispose(GObject *object)
{
    GstImxDmaBufAllocator *self = GST_IMX_DMABUF_ALLOCATOR(object);
    GST_TRACE_OBJECT(self, "finalizing i.MX DMA-BUF GstAllocator %p", (gpointer)self);
    G_OBJECT_CLASS(gst_imx_dmabuf_allocator_parent_class)->dispose(object);
}


static gboolean gst_imx_dmabuf_allocator_activate(GstImxDmaBufAllocator *imx_dmabuf_allocator)
{
    /* must be called with object lock held */

    GstImxDmaBufAllocatorClass *klass = GST_IMX_DMABUF_ALLOCATOR_CLASS(G_OBJECT_GET_CLASS(imx_dmabuf_allocator));

    g_assert(klass->activate != NULL);

    if (imx_dmabuf_allocator->active)
        return TRUE;

    if (!klass->activate(imx_dmabuf_allocator))
    {
        GST_ERROR_OBJECT(imx_dmabuf_allocator, "could not activate i.MX DMA-BUF allocator");
        return FALSE;
    }

    GST_DEBUG_OBJECT(imx_dmabuf_allocator, "i.MX DMA-BUF allocator activated");

    imx_dmabuf_allocator->active = TRUE;

    return TRUE;
}


static GstMemory* gst_imx_dmabuf_allocator_alloc_internal(GstImxDmaBufAllocator *imx_dmabuf_allocator, int dmabuf_fd, gsize size)
{
    imx_physical_address_t physical_address;
    InternalImxDmaBuffer *internal_imx_dma_buffer;
    GstMemory *memory = NULL;
    GstImxDmaBufAllocatorClass *klass = GST_IMX_DMABUF_ALLOCATOR_CLASS(G_OBJECT_GET_CLASS(imx_dmabuf_allocator));

    g_assert(klass->get_physical_address != NULL);

    /* must be called with object lock held */

    physical_address = klass->get_physical_address(imx_dmabuf_allocator, dmabuf_fd);
    if (physical_address == 0)
    {
        GST_ERROR_OBJECT(imx_dmabuf_allocator, "could not open get physical address from dmabuf FD %d", dmabuf_fd);
        goto finish;
    }
    GST_DEBUG_OBJECT(imx_dmabuf_allocator, "got physical address %" IMX_PHYSICAL_ADDRESS_FORMAT " from DMA-BUF buffer", physical_address);

    memory = gst_dmabuf_allocator_alloc(GST_ALLOCATOR_CAST(imx_dmabuf_allocator), dmabuf_fd, size);
    if (!memory)
    {
        GST_ERROR_OBJECT(imx_dmabuf_allocator, "could not allocate GstMemory with GstDmaBufAllocator");
        goto finish;
    }

    internal_imx_dma_buffer = g_malloc0(sizeof(InternalImxDmaBuffer));
    internal_imx_dma_buffer->parent.allocator = (ImxDmaBufferAllocator *)&(imx_dmabuf_allocator->internal_imxdmabuffer_allocator);
    internal_imx_dma_buffer->gstmemory = memory;
    internal_imx_dma_buffer->physical_address = physical_address;
    internal_imx_dma_buffer->dmabuf_fd = dmabuf_fd;
    internal_imx_dma_buffer->size = size;
    internal_imx_dma_buffer->mapping_refcount = 0;
    gst_mini_object_set_qdata(GST_MINI_OBJECT_CAST(memory), gst_imx_dmabuf_memory_internal_imxdmabuffer_quark, (gpointer)internal_imx_dma_buffer, g_free);

finish:
    return memory;
}




/**** GstPhysMemoryAllocatorInterface internal function definitions ****/

static void gst_imx_dmabuf_allocator_phys_mem_allocator_iface_init(gpointer iface, G_GNUC_UNUSED gpointer iface_data)
{
    GstPhysMemoryAllocatorInterface *phys_mem_allocator_iface = (GstPhysMemoryAllocatorInterface *)iface;
    phys_mem_allocator_iface->get_phys_addr = GST_DEBUG_FUNCPTR(gst_imx_dmabuf_allocator_get_phys_addr);
}


static guintptr gst_imx_dmabuf_allocator_get_phys_addr(GstPhysMemoryAllocator *allocator, GstMemory *mem)
{
    gpointer qdata;

    qdata = gst_mini_object_get_qdata(GST_MINI_OBJECT_CAST(mem), gst_imx_dmabuf_memory_internal_imxdmabuffer_quark);
    if (G_LIKELY(qdata == NULL))
    {
        GST_WARNING_OBJECT(allocator, "GstMemory object %p does not contain imxionbuffer qdata; returning 0 as physical address", (gpointer)mem);
        return 0;
    }

    return ((InternalImxDmaBuffer *)qdata)->physical_address + mem->offset;
}


static void gst_imx_dmabuf_allocator_dma_buffer_allocator_iface_init(gpointer iface, G_GNUC_UNUSED gpointer iface_data)
{
    GstImxDmaBufferAllocatorInterface *imx_dma_buffer_allocator_iface = (GstImxDmaBufferAllocatorInterface *)iface;
    imx_dma_buffer_allocator_iface->get_dma_buffer = GST_DEBUG_FUNCPTR(gst_imx_dmabuf_allocator_get_dma_buffer);
}




/**** GstImxDmaBufferAllocatorInterface internal function definitions ****/


static ImxDmaBuffer* gst_imx_dmabuf_allocator_get_dma_buffer(GstImxDmaBufferAllocator *allocator, GstMemory *memory)
{
    gpointer qdata;

    qdata = gst_mini_object_get_qdata(GST_MINI_OBJECT_CAST(memory), gst_imx_dmabuf_memory_internal_imxdmabuffer_quark);
    if (G_LIKELY(qdata == NULL))
    {
        GST_ERROR_OBJECT(allocator, "GstMemory object %p does not contain ImxDmaBufMemory qdata", (gpointer)memory);
        return NULL;
    }

    return ((ImxDmaBuffer *)qdata);
}




/**** GstAllocator internal function definitions ****/


static GstMemory* gst_imx_dmabuf_allocator_gstalloc_alloc(GstAllocator *allocator, gsize size, GstAllocationParams *params)
{
    int dmabuf_fd = -1;
    GstMemory *memory = NULL;
    GstImxDmaBufAllocator *self = GST_IMX_DMABUF_ALLOCATOR(allocator);
    gsize total_size = size + params->prefix + params->padding;
    size_t alignment;
    GstImxDmaBufAllocatorClass *klass = GST_IMX_DMABUF_ALLOCATOR_CLASS(G_OBJECT_GET_CLASS(self));

    g_assert(klass->allocate_dmabuf != NULL);

    GST_OBJECT_LOCK(self);

    if (!gst_imx_dmabuf_allocator_activate(self))
        goto finish;

    /* TODO: is this the correct way to calculate alignment?
    alignment = (params->align > 1) ? (params->align - 1) : 0; */
    alignment = params->align + 1;

    /* Perform the actual allocation. */
    dmabuf_fd = klass->allocate_dmabuf(self, total_size, alignment);
    if (dmabuf_fd < 0)
    {
        GST_ERROR_OBJECT(self, "could not allocate DMA-BUF buffer");
        goto finish;
    }
    GST_DEBUG_OBJECT(self, "allocated new DMA-BUF buffer;  FD: %d  total size: %" G_GSIZE_FORMAT "  alignment: %zu", dmabuf_fd, total_size, alignment);

    memory = gst_imx_dmabuf_allocator_alloc_internal(self, dmabuf_fd, total_size);

finish:
    GST_OBJECT_UNLOCK(self);
    return memory;
}


static void gst_imx_dmabuf_allocator_gstalloc_free(GstAllocator* allocator, GstMemory *memory)
{
    GST_DEBUG_OBJECT(allocator, "freeing DMA-BUF buffer with FD %d", gst_dmabuf_memory_get_fd(memory));
    GST_ALLOCATOR_CLASS(gst_imx_dmabuf_allocator_parent_class)->free(allocator, memory);
}


static GstMemory * gst_imx_dmabuf_allocator_copy(GstMemory *memory, gssize offset, gssize size)
{
    /* We explicitely do the copy here. GstAllocator has a fallback
     * mem_copy function that works in a very similar manner. But
     * we cannot rely on it, since the GstFdAllocator's
     * GST_ALLOCATOR_FLAG_CUSTOM_ALLOC flag is set, and we inherit
     * from that class indirectly. This causes the fallback mem_copy
     * to not use our DMA-BUF allocator, picking the sysmem allocator
     * instead. To make sure the copy is DMA-BUF backed, we perform
     * the copy manually. */

    int dmabuf_fd = -1;
    GstMemory *copy = NULL;
    GstMapInfo src_map_info;
    GstMapInfo dest_map_info;
    GstImxDmaBufAllocator *self = GST_IMX_DMABUF_ALLOCATOR_CAST(memory->allocator);
    GstImxDmaBufAllocatorClass *klass = GST_IMX_DMABUF_ALLOCATOR_CLASS(G_OBJECT_GET_CLASS(self));

    g_assert(klass->allocate_dmabuf != NULL);

    if (G_UNLIKELY(!gst_memory_map(memory, &src_map_info, GST_MAP_READ)))
    {
        GST_ERROR_OBJECT(memory, "could not map source memory %p for copy", (gpointer)memory);
        return NULL;
    }

    if (size == -1)
        size = (gssize)(src_map_info.size) > offset ? (src_map_info.size - offset) : 0;

    dmabuf_fd = klass->allocate_dmabuf(self, size, 1);
    if (G_UNLIKELY(dmabuf_fd < 0))
    {
        GST_ERROR_OBJECT(self, "could not allocate DMA-BUF buffer");
        goto error;
    }
    GST_DEBUG_OBJECT(self, "allocated new DMA-BUF buffer with FD %d for gstmemory copy", dmabuf_fd);

    copy = gst_imx_dmabuf_allocator_alloc_internal(self, dmabuf_fd, size);
    if (G_UNLIKELY(copy == NULL))
        goto error;

    if (G_UNLIKELY(!gst_memory_map(copy, &dest_map_info, GST_MAP_WRITE)))
    {
        GST_ERROR_OBJECT(memory, "could not map destination memory %p for copy", (gpointer)copy);
        goto error;
    }

    GST_LOG_OBJECT(
        self,
        "copying %" G_GSSIZE_FORMAT " byte(s) from gstmemory %p to gstmemory %p with offset %" G_GSSIZE_FORMAT,
        size,
        (gpointer)memory,
        (gpointer)copy,
        offset
    );

    memcpy(dest_map_info.data, src_map_info.data + offset, size);

    gst_memory_unmap(copy, &dest_map_info);


finish:
    gst_memory_unmap(memory, &src_map_info);
    return copy;

error:
    if (copy != NULL)
    {
        gst_memory_unref(copy);
        /* The copy handles ownership over dmabuf_fd, so by
         * unref'ing it, the DMA-BUF FD got closed as well. */
        dmabuf_fd = -1;
    }

    if (dmabuf_fd > 0)
    {
        close(dmabuf_fd);
        dmabuf_fd = -1;
    }

    goto finish;
}




/**** internal_imxdmabuffer_allocator internal function definitions ****/

static void gst_imx_dmabuf_allocator_imxdmabufalloc_destroy(G_GNUC_UNUSED ImxDmaBufferAllocator *allocator)
{
}


static ImxDmaBuffer* gst_imx_dmabuf_allocator_imxdmabufalloc_allocate(G_GNUC_UNUSED ImxDmaBufferAllocator *allocator, G_GNUC_UNUSED size_t size, G_GNUC_UNUSED size_t alignment, G_GNUC_UNUSED int *error)
{
    return NULL;
}


static void gst_imx_dmabuf_allocator_imxdmabufalloc_deallocate(G_GNUC_UNUSED ImxDmaBufferAllocator *allocator, G_GNUC_UNUSED ImxDmaBuffer *buffer)
{
}


static uint8_t* gst_imx_dmabuf_allocator_imxdmabufalloc_map(G_GNUC_UNUSED ImxDmaBufferAllocator *allocator, ImxDmaBuffer *buffer, unsigned int flags, G_GNUC_UNUSED int *error)
{
    InternalImxDmaBuffer *internal_imx_dma_buffer = (InternalImxDmaBuffer *)buffer;
    if (internal_imx_dma_buffer->mapping_refcount == 0)
    {
        GstMapFlags gstflags = 0;
        if (flags & IMX_DMA_BUFFER_MAPPING_FLAG_READ) gstflags |= GST_MAP_READ;
        if (flags & IMX_DMA_BUFFER_MAPPING_FLAG_WRITE) gstflags |= GST_MAP_WRITE;

        gst_memory_map(internal_imx_dma_buffer->gstmemory, &(internal_imx_dma_buffer->map_info), gstflags);
    }

    internal_imx_dma_buffer->mapping_refcount++;

    return internal_imx_dma_buffer->map_info.data;
}


static void gst_imx_dmabuf_allocator_imxdmabufalloc_unmap(G_GNUC_UNUSED ImxDmaBufferAllocator *allocator, ImxDmaBuffer *buffer)
{
    InternalImxDmaBuffer *internal_imx_dma_buffer = (InternalImxDmaBuffer *)buffer;

    if (internal_imx_dma_buffer->mapping_refcount == 0)
        return;

    internal_imx_dma_buffer->mapping_refcount--;
    if (internal_imx_dma_buffer->mapping_refcount != 0)
        return;

    gst_memory_unmap(internal_imx_dma_buffer->gstmemory, &(internal_imx_dma_buffer->map_info));
}


static imx_physical_address_t gst_imx_dmabuf_allocator_imxdmabufalloc_get_physical_address(G_GNUC_UNUSED ImxDmaBufferAllocator *allocator, ImxDmaBuffer *buffer)
{
    InternalImxDmaBuffer *internal_imx_dma_buffer = (InternalImxDmaBuffer *)buffer;
    return internal_imx_dma_buffer->physical_address;
}


static int gst_imx_dmabuf_allocator_imxdmabufalloc_get_fd(G_GNUC_UNUSED ImxDmaBufferAllocator *allocator, ImxDmaBuffer *buffer)
{
    InternalImxDmaBuffer *internal_imx_dma_buffer = (InternalImxDmaBuffer *)buffer;
    return internal_imx_dma_buffer->dmabuf_fd;
}


static size_t gst_imx_dmabuf_allocator_imxdmabufalloc_get_size(G_GNUC_UNUSED ImxDmaBufferAllocator *allocator, ImxDmaBuffer *buffer)
{
    InternalImxDmaBuffer *internal_imx_dma_buffer = (InternalImxDmaBuffer *)buffer;
    return internal_imx_dma_buffer->size;
}




/**** Public functions ****/


GstMemory* gst_imx_dmabuf_allocator_wrap_dmabuf(GstAllocator *allocator, int dmabuf_fd, gsize dmabuf_size)
{
    GstImxDmaBufAllocator *self;
    GstMemory *memory = NULL;

    g_assert(allocator != NULL);

    self = GST_IMX_DMABUF_ALLOCATOR(allocator);

    assert(dmabuf_fd > 0);
    assert(dmabuf_size > 0);

    GST_OBJECT_LOCK(self);
    gst_imx_dmabuf_allocator_activate(self);
    memory = gst_imx_dmabuf_allocator_alloc_internal(self, dmabuf_fd, dmabuf_size);
    GST_OBJECT_UNLOCK(self);

    return memory;
}


gboolean gst_imx_dmabuf_allocator_is_active(GstAllocator *allocator)
{
    GstImxDmaBufAllocator *self;
    g_assert(allocator != NULL);
    self = GST_IMX_DMABUF_ALLOCATOR(allocator);
    return self->active;
}


GstAllocator* gst_imx_dmabuf_allocator_new(void)
{
#if defined(WITH_GST_DMA_HEAP_ALLOCATOR)
    return gst_imx_dma_heap_allocator_new();
#elif defined(WITH_GST_ION_ALLOCATOR)
    return gst_imx_ion_allocator_new();
#else
    /* No DMA-BUF capable allocator enabled. In such a case, calling this is an error. */
    g_assert_not_reached();
    return NULL;
#endif
}
