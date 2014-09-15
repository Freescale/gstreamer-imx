/* Allocation functions for physical memory
 * Copyright (C) 2013  Carlos Rafael Giani
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
#include <string.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <linux/ipu.h>
#include "../common/phys_mem_meta.h"
#include "device.h"


GST_DEBUG_CATEGORY_STATIC(imx_ipu_allocator_debug);
#define GST_CAT_DEFAULT imx_ipu_allocator_debug



static void gst_imx_ipu_allocator_finalize(GObject *object);

static gboolean gst_imx_ipu_alloc_phys_mem(GstImxPhysMemAllocator *allocator, GstImxPhysMemory *memory, gssize size);
static gboolean gst_imx_ipu_free_phys_mem(GstImxPhysMemAllocator *allocator, GstImxPhysMemory *memory);
static gpointer gst_imx_ipu_map_phys_mem(GstImxPhysMemAllocator *allocator, GstImxPhysMemory *memory, gssize size, GstMapFlags flags);
static void gst_imx_ipu_unmap_phys_mem(GstImxPhysMemAllocator *allocator, GstImxPhysMemory *memory);


G_DEFINE_TYPE(GstImxIpuAllocator, gst_imx_ipu_allocator, GST_TYPE_IMX_PHYS_MEM_ALLOCATOR)




GstAllocator* gst_imx_ipu_allocator_new(void)
{
	GstAllocator *allocator;
	allocator = g_object_new(gst_imx_ipu_allocator_get_type(), NULL);

	return allocator;
}


static gboolean gst_imx_ipu_alloc_phys_mem(GstImxPhysMemAllocator *allocator, GstImxPhysMemory *memory, gssize size)
{
	dma_addr_t m;
	int ret;

	m = (dma_addr_t)size;
	ret = ioctl(gst_imx_ipu_get_fd(), IPU_ALLOC, &m);
	memory->internal = NULL;
	if (ret < 0)
	{
		GST_ERROR_OBJECT(allocator, "could not allocate %u bytes of physical memory: %s", size, strerror(errno));
		memory->phys_addr = 0;
		return FALSE;
	}
	else
	{
		memory->phys_addr = m;
		GST_DEBUG_OBJECT(allocator, "allocated %u bytes of physical memory at address %" GST_IMX_PHYS_ADDR_FORMAT, size, memory->phys_addr);
		return TRUE;
	}
}


static gboolean gst_imx_ipu_free_phys_mem(GstImxPhysMemAllocator *allocator, GstImxPhysMemory *memory)
{
	dma_addr_t m;
	int ret;

	m = (dma_addr_t)(memory->phys_addr);
	ret = ioctl(gst_imx_ipu_get_fd(), IPU_FREE, &m);
	if (ret < 0)
	{
		GST_ERROR_OBJECT(allocator, "could not free physical memory at address %" GST_IMX_PHYS_ADDR_FORMAT ": %s", memory->phys_addr, strerror(errno));
		return FALSE;
	}
	else
	{
		GST_DEBUG_OBJECT(allocator, "freed physical memory at address %" GST_IMX_PHYS_ADDR_FORMAT, memory->phys_addr);
		return TRUE;
	}
}


static gpointer gst_imx_ipu_map_phys_mem(GstImxPhysMemAllocator *allocator, GstImxPhysMemory *memory, gssize size, G_GNUC_UNUSED GstMapFlags flags)
{
	GstImxPhysMemory *phys_mem = (GstImxPhysMemory *)memory;
	GstImxIpuAllocator *ipu_allocator = GST_IMX_IPU_ALLOCATOR(allocator);

	g_assert(phys_mem->mapped_virt_addr == NULL);

	/* Mapping with both read and write flags, since mapping is refcounted,
	 * and it is possible multiple mappings might select either read or write */
	phys_mem->mapped_virt_addr = mmap(0, size, PROT_READ | PROT_WRITE, MAP_SHARED, gst_imx_ipu_get_fd(), (dma_addr_t)(phys_mem->phys_addr));
	if (phys_mem->mapped_virt_addr == MAP_FAILED)
	{
		phys_mem->mapped_virt_addr = NULL;
		GST_ERROR_OBJECT(ipu_allocator, "memory-mapping the IPU framebuffer failed: %s", strerror(errno));
		return NULL;
	}

	GST_LOG_OBJECT(ipu_allocator, "mapped IPU physmem memory:  virt addr %p  phys addr %" GST_IMX_PHYS_ADDR_FORMAT, phys_mem->mapped_virt_addr, phys_mem->phys_addr);

	return phys_mem->mapped_virt_addr;
}


static void gst_imx_ipu_unmap_phys_mem(GstImxPhysMemAllocator *allocator, GstImxPhysMemory *memory)
{
	if (memory->mapped_virt_addr != NULL)
	{
		if (munmap(memory->mapped_virt_addr, memory->mem.maxsize) == -1)
			GST_ERROR_OBJECT(allocator, "unmapping memory-mapped IPU framebuffer failed: %s", strerror(errno));
		GST_LOG_OBJECT(allocator, "unmapped IPU physmem memory:  virt addr %p  phys addr %" GST_IMX_PHYS_ADDR_FORMAT, memory->mapped_virt_addr, memory->phys_addr);
		memory->mapped_virt_addr = NULL;
	}
}




static void gst_imx_ipu_allocator_class_init(GstImxIpuAllocatorClass *klass)
{
	GObjectClass *object_class = G_OBJECT_CLASS(klass);
	GstImxPhysMemAllocatorClass *parent_class = GST_IMX_PHYS_MEM_ALLOCATOR_CLASS(klass);

	object_class->finalize       = GST_DEBUG_FUNCPTR(gst_imx_ipu_allocator_finalize);
	parent_class->alloc_phys_mem = GST_DEBUG_FUNCPTR(gst_imx_ipu_alloc_phys_mem);
	parent_class->free_phys_mem  = GST_DEBUG_FUNCPTR(gst_imx_ipu_free_phys_mem);
	parent_class->map_phys_mem   = GST_DEBUG_FUNCPTR(gst_imx_ipu_map_phys_mem);
	parent_class->unmap_phys_mem = GST_DEBUG_FUNCPTR(gst_imx_ipu_unmap_phys_mem);

	GST_DEBUG_CATEGORY_INIT(imx_ipu_allocator_debug, "imxipuallocator", 0, "Freescale i.MX IPU physical memory/allocator");
}


static void gst_imx_ipu_allocator_init(GstImxIpuAllocator *allocator)
{
	GstAllocator *base = GST_ALLOCATOR(allocator);
	base->mem_type = GST_IMX_IPU_ALLOCATOR_MEM_TYPE;

	if (!gst_imx_ipu_open())
	{
		GST_ERROR_OBJECT(allocator, "could not open IPU device");
		return;
	}

	GST_INFO_OBJECT(allocator, "initialized IPU allocator");
}


static void gst_imx_ipu_allocator_finalize(GObject *object)
{
	GST_INFO_OBJECT(object, "shutting down IPU allocator");

	gst_imx_ipu_close();

	G_OBJECT_CLASS(gst_imx_ipu_allocator_parent_class)->finalize(object);
}

