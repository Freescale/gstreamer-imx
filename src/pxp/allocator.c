/* PxP allocation functions for physical memory
 * Copyright (C) 2014  Carlos Rafael Giani
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


#include <string.h>
#include <errno.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <pxp_lib.h>
#include "allocator.h"
#include "device.h"


GST_DEBUG_CATEGORY_STATIC(imx_pxp_allocator_debug);
#define GST_CAT_DEFAULT imx_pxp_allocator_debug



static void gst_imx_pxp_allocator_finalize(GObject *object);

static gboolean gst_imx_pxp_alloc_phys_mem(GstImxPhysMemAllocator *allocator, GstImxPhysMemory *memory, gssize size);
static gboolean gst_imx_pxp_free_phys_mem(GstImxPhysMemAllocator *allocator, GstImxPhysMemory *memory);
static void gst_imx_pxp_cache_op(GstImxPhysMemAllocator *allocator, GstImxPhysMemory *memory, int cache_mode);
static gpointer gst_imx_pxp_map_phys_mem(GstImxPhysMemAllocator *allocator, GstImxPhysMemory *phys_mem, gssize size, GstMapFlags flags);
static void gst_imx_pxp_unmap_phys_mem(GstImxPhysMemAllocator *allocator, GstImxPhysMemory *phys_mem);


G_DEFINE_TYPE(GstImxPxPAllocator, gst_imx_pxp_allocator, GST_TYPE_IMX_PHYS_MEM_ALLOCATOR)




GstAllocator* gst_imx_pxp_allocator_new(void)
{
	GstAllocator *allocator;
	allocator = g_object_new(gst_imx_pxp_allocator_get_type(), NULL);

	return allocator;
}


static gboolean gst_imx_pxp_alloc_phys_mem(GstImxPhysMemAllocator *allocator, GstImxPhysMemory *memory, gssize size)
{
	/* allocate cacheable physically contiguous memory block */

	int ret;
	struct pxp_mem_desc *mem_desc = g_slice_alloc0(sizeof(struct pxp_mem_desc));

	mem_desc->size = size;
	mem_desc->mtype = MEMORY_TYPE_CACHED;

	ret = ioctl(gst_imx_pxp_get_fd(), PXP_IOC_GET_PHYMEM, mem_desc);

	if (ret != 0)
	{
		GST_ERROR_OBJECT(allocator, "could not allocate %u bytes of physical memory: %s", size, strerror(errno));
		g_slice_free1(size, mem_desc);
		return FALSE;
	}
	else
	{
		memory->phys_addr = (gst_imx_phys_addr_t)(mem_desc->phys_addr);
		memory->internal = mem_desc;

		GST_INFO_OBJECT(allocator, "allocated %u bytes of physical memory, paddr %" GST_IMX_PHYS_ADDR_FORMAT, size, memory->phys_addr);

		return TRUE;
	}
}


static gboolean gst_imx_pxp_free_phys_mem(GstImxPhysMemAllocator *allocator, GstImxPhysMemory *memory)
{
	struct pxp_mem_desc *mem_desc;

	g_assert(memory->internal != NULL);

	mem_desc = (struct pxp_mem_desc *)(memory->internal);

	if (ioctl(gst_imx_pxp_get_fd(), PXP_IOC_PUT_PHYMEM, mem_desc) == 0)
	{
		GST_INFO_OBJECT(allocator, "freed %u bytes of physical memory, paddr %" GST_IMX_PHYS_ADDR_FORMAT, memory->mem.size, memory->phys_addr);
		return TRUE;
	}
	else
	{
		GST_ERROR_OBJECT(allocator, "could not free %u bytes of physical memory, paddr %" GST_IMX_PHYS_ADDR_FORMAT ": %s", memory->mem.size, memory->phys_addr, strerror(errno));
		return FALSE;
	}
}


static void gst_imx_pxp_cache_op(GstImxPhysMemAllocator *allocator, GstImxPhysMemory *memory, int cache_mode)
{
	gchar const *desc;
	struct pxp_mem_flush flush;
	struct pxp_mem_desc *mem_desc = (struct pxp_mem_desc *)(memory->internal);

	switch (cache_mode)
	{
		case CACHE_CLEAN:      desc = "cleaning"; break;
		case CACHE_FLUSH:      desc = "flushing"; break;
		case CACHE_INVALIDATE: desc = "invalidating"; break;
		default: g_assert(0);
	}

	flush.type = cache_mode;
	flush.handle = mem_desc->handle;

	if (ioctl(gst_imx_pxp_get_fd(), PXP_IOC_FLUSH_PHYMEM, &flush) == 0)
	{
		GST_LOG_OBJECT(allocator, "%s cacheable memory, paddr %" GST_IMX_PHYS_ADDR_FORMAT, desc, memory->phys_addr);
	}
	else
	{
		GST_ERROR_OBJECT(allocator, "%s cacheable memory failed, paddr %" GST_IMX_PHYS_ADDR_FORMAT, desc, memory->phys_addr);
	}
}


static gpointer gst_imx_pxp_map_phys_mem(GstImxPhysMemAllocator *allocator, GstImxPhysMemory *phys_mem, G_GNUC_UNUSED gssize size, GstMapFlags flags)
{
	int prot = 0;
	GstImxPxPAllocator *pxp_allocator = GST_IMX_PXP_ALLOCATOR(allocator);

	g_assert(phys_mem->mapped_virt_addr == NULL);

	/* As explained in gst_imx_phys_mem_allocator_map(), the flags are guaranteed to
	 * be the same when a memory block is mapped multiple times, so the value of
	 * "flags" will be identical if map() is called two times, for example. */

	if (flags & GST_MAP_READ)
		prot |= PROT_READ;
	if (flags & GST_MAP_WRITE)
		prot |= PROT_WRITE;

	/* invalidate cache in map() if the MAP_READ flag is set
	 * to ensure any data that is read from the mapped memory
	 * is up to date */
	if (flags & GST_MAP_READ)
		gst_imx_pxp_cache_op(allocator, phys_mem, CACHE_INVALIDATE);

	phys_mem->mapped_virt_addr = mmap(0, size, prot, MAP_SHARED, gst_imx_pxp_get_fd(), (dma_addr_t)(phys_mem->phys_addr));
	if (phys_mem->mapped_virt_addr == MAP_FAILED)
	{
		phys_mem->mapped_virt_addr = NULL;
		GST_ERROR_OBJECT(pxp_allocator, "memory-mapping the PxP framebuffer failed: %s", strerror(errno));
		return NULL;
	}

	GST_LOG_OBJECT(pxp_allocator, "mapped PxP physmem memory:  virt addr %p  phys addr %" GST_IMX_PHYS_ADDR_FORMAT, phys_mem->mapped_virt_addr, phys_mem->phys_addr);

	return phys_mem->mapped_virt_addr;
}


static void gst_imx_pxp_unmap_phys_mem(GstImxPhysMemAllocator *allocator, GstImxPhysMemory *memory)
{
	if (memory->mapped_virt_addr != NULL)
	{
		if (munmap(memory->mapped_virt_addr, memory->mem.maxsize) == -1)
			GST_ERROR_OBJECT(allocator, "unmapping memory-mapped PxP framebuffer failed: %s", strerror(errno));
		GST_LOG_OBJECT(allocator, "unmapped PxP physmem memory:  virt addr %p  phys addr %" GST_IMX_PHYS_ADDR_FORMAT, memory->mapped_virt_addr, memory->phys_addr);
		memory->mapped_virt_addr = NULL;

		/* clean cache in map() if the MAP_WRITE flag is set
		 * to ensure the cache gets filled with up to date data */
		if (memory->mapping_flags & GST_MAP_WRITE)
			gst_imx_pxp_cache_op(allocator, memory, CACHE_CLEAN);
	}
}




static void gst_imx_pxp_allocator_class_init(GstImxPxPAllocatorClass *klass)
{
	GObjectClass *object_class = G_OBJECT_CLASS(klass);
	GstImxPhysMemAllocatorClass *parent_class = GST_IMX_PHYS_MEM_ALLOCATOR_CLASS(klass);

	object_class->finalize       = GST_DEBUG_FUNCPTR(gst_imx_pxp_allocator_finalize);
	parent_class->alloc_phys_mem = GST_DEBUG_FUNCPTR(gst_imx_pxp_alloc_phys_mem);
	parent_class->free_phys_mem  = GST_DEBUG_FUNCPTR(gst_imx_pxp_free_phys_mem);
	parent_class->map_phys_mem   = GST_DEBUG_FUNCPTR(gst_imx_pxp_map_phys_mem);
	parent_class->unmap_phys_mem = GST_DEBUG_FUNCPTR(gst_imx_pxp_unmap_phys_mem);

	GST_DEBUG_CATEGORY_INIT(imx_pxp_allocator_debug, "imxpxpallocator", 0, "Freescale i.MX PxP physical memory/allocator");
}


static void gst_imx_pxp_allocator_init(GstImxPxPAllocator *allocator)
{
	GstAllocator *base = GST_ALLOCATOR(allocator);
	base->mem_type = GST_IMX_PXP_ALLOCATOR_MEM_TYPE;

	if (!gst_imx_pxp_open())
	{
		GST_ERROR_OBJECT(allocator, "could not open PxP device");
		return;
	}
}


static void gst_imx_pxp_allocator_finalize(GObject *object)
{
	GST_DEBUG_OBJECT(object, "shutting down IMX PxP allocator");

	gst_imx_pxp_close();

	G_OBJECT_CLASS(gst_imx_pxp_allocator_parent_class)->finalize(object);
}
