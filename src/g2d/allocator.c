/* G2D allocation functions for physical memory
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


#include <g2d.h>
#include "allocator.h"


GST_DEBUG_CATEGORY_STATIC(imx_g2d_allocator_debug);
#define GST_CAT_DEFAULT imx_g2d_allocator_debug



static void gst_imx_g2d_allocator_finalize(GObject *object);

static gboolean gst_imx_g2d_alloc_phys_mem(GstImxPhysMemAllocator *allocator, GstImxPhysMemory *memory, gssize size);
static gboolean gst_imx_g2d_free_phys_mem(GstImxPhysMemAllocator *allocator, GstImxPhysMemory *memory);
static gpointer gst_imx_g2d_map_phys_mem(GstImxPhysMemAllocator *allocator, GstImxPhysMemory *memory, gssize size, GstMapFlags flags);
static void gst_imx_g2d_unmap_phys_mem(GstImxPhysMemAllocator *allocator, GstImxPhysMemory *memory);


G_DEFINE_TYPE(GstImxG2DAllocator, gst_imx_g2d_allocator, GST_TYPE_IMX_PHYS_MEM_ALLOCATOR)




GstAllocator* gst_imx_g2d_allocator_new(void)
{
	GstAllocator *allocator;
	allocator = g_object_new(gst_imx_g2d_allocator_get_type(), NULL);

	return allocator;
}


static gboolean gst_imx_g2d_alloc_phys_mem(GstImxPhysMemAllocator *allocator, GstImxPhysMemory *memory, gssize size)
{
	/* allocate cacheable physically contiguous memory block */
	struct g2d_buf *buf = g2d_alloc(size, 1);

	if (buf == NULL)
	{
		GST_ERROR_OBJECT(allocator, "could not allocate %u bytes of physical memory", size);
		return FALSE;
	}
	else
	{
		memory->mapped_virt_addr = buf->buf_vaddr;
		memory->phys_addr = (guintptr)(buf->buf_paddr);
		memory->internal = buf;

		GST_INFO_OBJECT(allocator, "allocated %u bytes of physical memory, vaddr %p paddr %" GST_IMX_PHYS_ADDR_FORMAT, size, memory->mapped_virt_addr, memory->phys_addr);

		return TRUE;
	}
}


static gboolean gst_imx_g2d_free_phys_mem(GstImxPhysMemAllocator *allocator, GstImxPhysMemory *memory)
{
	struct g2d_buf *buf;

	g_assert(memory->internal != NULL);

	buf = (struct g2d_buf *)(memory->internal);

	if (g2d_free(buf) == 0)
	{
		GST_INFO_OBJECT(allocator, "freed %u bytes of physical memory, vaddr %p paddr %" GST_IMX_PHYS_ADDR_FORMAT, memory->mem.size, memory->mapped_virt_addr, memory->phys_addr);
		return TRUE;
	}
	else
	{
		GST_ERROR_OBJECT(allocator, "could not free %u bytes of physical memory, vaddr %p paddr %" GST_IMX_PHYS_ADDR_FORMAT, memory->mem.size, memory->mapped_virt_addr, memory->phys_addr);
		return FALSE;
	}
}


static gpointer gst_imx_g2d_map_phys_mem(G_GNUC_UNUSED GstImxPhysMemAllocator *allocator, GstImxPhysMemory *memory, G_GNUC_UNUSED gssize size, GstMapFlags flags)
{
	struct g2d_buf *buf = (struct g2d_buf *)(memory->internal);

	/* invalidate cache in map() if the MAP_READ flag is set
	 * to ensure any data that is read from the mapped memory
	 * is up to date */
	if (flags & GST_MAP_READ)
	{
		if (g2d_cache_op(buf, G2D_CACHE_INVALIDATE) == 0)
		{
			GST_LOG_OBJECT(allocator, "invalidating cacheable memory, vaddr %p paddr %" GST_IMX_PHYS_ADDR_FORMAT, memory->mapped_virt_addr, memory->phys_addr);
		}
		else
		{
			GST_ERROR_OBJECT(allocator, "invalidating cacheable memory failed, vaddr %p paddr %" GST_IMX_PHYS_ADDR_FORMAT, memory->mapped_virt_addr, memory->phys_addr);
		}
	}

	return memory->mapped_virt_addr;
}


static void gst_imx_g2d_unmap_phys_mem(G_GNUC_UNUSED GstImxPhysMemAllocator *allocator, GstImxPhysMemory *memory)
{
	struct g2d_buf *buf = (struct g2d_buf *)(memory->internal);

	/* clean cache in map() if the MAP_WRITE flag is set
	 * to ensure the cache gets filled with up to date data */
	if (memory->mapping_flags & GST_MAP_WRITE)
	{
		if (g2d_cache_op(buf, G2D_CACHE_CLEAN) == 0)
		{
			GST_LOG_OBJECT(allocator, "cleaned cacheable memory, vaddr %p paddr %" GST_IMX_PHYS_ADDR_FORMAT, memory->mapped_virt_addr, memory->phys_addr);
		}
		else
		{
			GST_ERROR_OBJECT(allocator, "cleaning cacheable memory failed, vaddr %p paddr %" GST_IMX_PHYS_ADDR_FORMAT, memory->mapped_virt_addr, memory->phys_addr);
		}
	}
}




static void gst_imx_g2d_allocator_class_init(GstImxG2DAllocatorClass *klass)
{
	GObjectClass *object_class = G_OBJECT_CLASS(klass);
	GstImxPhysMemAllocatorClass *parent_class = GST_IMX_PHYS_MEM_ALLOCATOR_CLASS(klass);

	object_class->finalize       = GST_DEBUG_FUNCPTR(gst_imx_g2d_allocator_finalize);
	parent_class->alloc_phys_mem = GST_DEBUG_FUNCPTR(gst_imx_g2d_alloc_phys_mem);
	parent_class->free_phys_mem  = GST_DEBUG_FUNCPTR(gst_imx_g2d_free_phys_mem);
	parent_class->map_phys_mem   = GST_DEBUG_FUNCPTR(gst_imx_g2d_map_phys_mem);
	parent_class->unmap_phys_mem = GST_DEBUG_FUNCPTR(gst_imx_g2d_unmap_phys_mem);

	GST_DEBUG_CATEGORY_INIT(imx_g2d_allocator_debug, "imxg2dallocator", 0, "Freescale i.MX G2D physical memory/allocator");
}


static void gst_imx_g2d_allocator_init(GstImxG2DAllocator *allocator)
{
	GstAllocator *base = GST_ALLOCATOR(allocator);
	base->mem_type = GST_IMX_G2D_ALLOCATOR_MEM_TYPE;
}


static void gst_imx_g2d_allocator_finalize(GObject *object)
{
	GST_DEBUG_OBJECT(object, "shutting down IMX G2D allocator");

	G_OBJECT_CLASS(gst_imx_g2d_allocator_parent_class)->finalize(object);
}
