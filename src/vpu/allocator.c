/* GStreamer allocator class using the imxvpuapi DMA buffer allocator interface
 * Copyright (C) 2015  Carlos Rafael Giani
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


#include "allocator.h"
#include "device.h"


GST_DEBUG_CATEGORY_STATIC(imx_vpu_allocator_debug);
#define GST_CAT_DEFAULT imx_vpu_allocator_debug



static void gst_imx_vpu_allocator_finalize(GObject *object);

static gboolean gst_imx_vpu_alloc_phys_mem(GstImxPhysMemAllocator *allocator, GstImxPhysMemory *memory, gssize size);
static gboolean gst_imx_vpu_free_phys_mem(GstImxPhysMemAllocator *allocator, GstImxPhysMemory *memory);
static gpointer gst_imx_vpu_map_phys_mem(GstImxPhysMemAllocator *allocator, GstImxPhysMemory *memory, gssize size, GstMapFlags flags);
static void gst_imx_vpu_unmap_phys_mem(GstImxPhysMemAllocator *allocator, GstImxPhysMemory *memory);


G_DEFINE_TYPE(GstImxVpuAllocator, gst_imx_vpu_allocator, GST_TYPE_IMX_PHYS_MEM_ALLOCATOR)




static void gst_imx_vpu_allocator_class_init(GstImxVpuAllocatorClass *klass)
{
	GObjectClass *object_class = G_OBJECT_CLASS(klass);
	GstImxPhysMemAllocatorClass *parent_class = GST_IMX_PHYS_MEM_ALLOCATOR_CLASS(klass);

	object_class->finalize       = GST_DEBUG_FUNCPTR(gst_imx_vpu_allocator_finalize);
	parent_class->alloc_phys_mem = GST_DEBUG_FUNCPTR(gst_imx_vpu_alloc_phys_mem);
	parent_class->free_phys_mem  = GST_DEBUG_FUNCPTR(gst_imx_vpu_free_phys_mem);
	parent_class->map_phys_mem   = GST_DEBUG_FUNCPTR(gst_imx_vpu_map_phys_mem);
	parent_class->unmap_phys_mem = GST_DEBUG_FUNCPTR(gst_imx_vpu_unmap_phys_mem);

	GST_DEBUG_CATEGORY_INIT(imx_vpu_allocator_debug, "imxvpuallocator", 0, "Freescale i.MX VPU DMA buffer allocator");
}


static void gst_imx_vpu_allocator_init(GstImxVpuAllocator *allocator)
{
	/* mem type is initialized in the _new() function */
	GST_INFO_OBJECT(allocator, "initializing IMX VPU decoder allocator");
}


static void gst_imx_vpu_allocator_finalize(GObject *object)
{
	GST_INFO_OBJECT(object, "shutting down IMX VPU decoder allocator");
	G_OBJECT_CLASS(gst_imx_vpu_allocator_parent_class)->finalize(object);
}




GstAllocator* gst_imx_vpu_allocator_new(ImxVpuDMABufferAllocator *imxvpuapi_allocator, gchar const *mem_type)
{
	GstAllocator *allocator = g_object_new(gst_imx_vpu_allocator_get_type(), NULL);
	allocator->mem_type = mem_type;
	GST_IMX_VPU_IMXVPUAPI_ALLOCATOR(allocator) = imxvpuapi_allocator;
	return allocator;
}


ImxVpuDMABuffer* gst_imx_vpu_get_dma_buffer_from(GstBuffer *buffer)
{
	GstMemory *memory = gst_buffer_peek_memory(buffer, 0);
	if ((memory == NULL) || !GST_IS_IMX_VPU_ALLOCATOR(memory->allocator))
		return NULL;

	return (ImxVpuDMABuffer *)(((GstImxPhysMemory *)memory)->internal);
}


static gboolean gst_imx_vpu_alloc_phys_mem(GstImxPhysMemAllocator *allocator, GstImxPhysMemory *memory, gssize size)
{
	ImxVpuDMABuffer *dma_buffer = imx_vpu_dma_buffer_allocate(GST_IMX_VPU_IMXVPUAPI_ALLOCATOR(allocator), size, 1, 0);
	if (dma_buffer == NULL)
		return FALSE;

	/* Load the VPU decoder to make sure the default allocator works. Loading the VPU
	 * makes use of an internal reference counter, so multiple load calls are okay.
	 * gst_imx_vpu_decoder_load() is a thread safe wrapper around imx_vpu_dec_load(). */
	if (!gst_imx_vpu_decoder_load())
		return FALSE;


	memory->internal = dma_buffer;
	memory->mem.size = imx_vpu_dma_buffer_get_size(dma_buffer);
	memory->phys_addr = imx_vpu_dma_buffer_get_physical_address(dma_buffer);
	memory->mapped_virt_addr = NULL;

	if (memory->phys_addr == 0)
	{
		GST_ERROR_OBJECT(allocator, "could not get physical address for DMA buffer");
		imx_vpu_dma_buffer_deallocate(dma_buffer);
		return FALSE;
	}

	return TRUE;
}


static gboolean gst_imx_vpu_free_phys_mem(G_GNUC_UNUSED GstImxPhysMemAllocator *allocator, GstImxPhysMemory *memory)
{
	ImxVpuDMABuffer *dma_buffer = (ImxVpuDMABuffer *)(memory->internal);
	g_assert(dma_buffer != NULL);

	imx_vpu_dma_buffer_deallocate(dma_buffer);

	/* Unload the decoder when freeing memory. Together with the load function call in
	 * gst_imx_vpu_alloc_phys_mem(), this makes sure the allocator still works even if
	 * the actual decoder has been shut down already. (The allocator needs to have the
	 * internal VPU decoder loaded in order to work.)
	 * gst_imx_vpu_decoder_unload() is a thread safe wrapper around imx_vpu_dec_unload(). */
	gst_imx_vpu_decoder_unload();

	return TRUE;
}


static gpointer gst_imx_vpu_map_phys_mem(G_GNUC_UNUSED GstImxPhysMemAllocator *allocator, GstImxPhysMemory *memory, G_GNUC_UNUSED gssize size, GstMapFlags flags)
{
	unsigned int internal_flags;
	ImxVpuDMABuffer *dma_buffer = (ImxVpuDMABuffer *)(memory->internal);

	internal_flags = 0;
	internal_flags |= (flags & GST_MAP_READ) ? IMX_VPU_MAPPING_FLAG_READ : 0;
	internal_flags |= (flags & GST_MAP_WRITE) ? IMX_VPU_MAPPING_FLAG_WRITE : 0;

	memory->mapped_virt_addr = imx_vpu_dma_buffer_map(dma_buffer, internal_flags);
	return memory->mapped_virt_addr;
}


static void gst_imx_vpu_unmap_phys_mem(G_GNUC_UNUSED GstImxPhysMemAllocator *allocator, GstImxPhysMemory *memory)
{
	ImxVpuDMABuffer *dma_buffer = (ImxVpuDMABuffer *)(memory->internal);
	imx_vpu_dma_buffer_unmap(dma_buffer);
}
