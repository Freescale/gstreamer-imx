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


GST_DEBUG_CATEGORY_STATIC(fslipuallocator_debug);
#define GST_CAT_DEFAULT fslipuallocator_debug



static void gst_fsl_ipu_allocator_finalize(GObject *object);

static GstMemory* gst_fsl_ipu_allocator_alloc(GstAllocator *allocator, gsize size, GstAllocationParams *params);
static void gst_fsl_ipu_allocator_free(GstAllocator *allocator, GstMemory *memory);
static gpointer gst_fsl_ipu_allocator_map(GstMemory *mem, gsize maxsize, GstMapFlags flags);
static void gst_fsl_ipu_allocator_unmap(GstMemory *mem);
static GstMemory* gst_fsl_ipu_allocator_copy(GstMemory *mem, gssize offset, gssize size);
static GstMemory* gst_fsl_ipu_allocator_share(GstMemory *mem, gssize offset, gssize size);
static gboolean gst_fsl_ipu_allocator_is_span(GstMemory *mem1, GstMemory *mem2, gsize *offset);


G_DEFINE_TYPE(GstFslIpuAllocator, gst_fsl_ipu_allocator, GST_TYPE_ALLOCATOR)


static void setup_debug_category(void)
{
	static gsize initialized = 0;

	if (g_once_init_enter(&initialized))
	{
		GST_DEBUG_CATEGORY_INIT(fslipuallocator_debug, "fslipuallocator", 0, "Freescale IPU physical memory/allocator");
		g_once_init_leave(&initialized, 1);
	}
}


GstAllocator* gst_fsl_ipu_allocator_new(int ipu_fd)
{
	GstAllocator *allocator;
	allocator = g_object_new(gst_fsl_ipu_allocator_get_type(), NULL);

	GST_FSL_IPU_ALLOCATOR(allocator)->fd = ipu_fd;

	return allocator;
}


gpointer gst_fsl_ipu_alloc_phys_mem(int ipu_fd, gsize size)
{
	dma_addr_t m;
	int ret;

	setup_debug_category();

	m = (dma_addr_t)size;
	ret = ioctl(ipu_fd, IPU_ALLOC, &m);
	if (ret < 0)
	{
		GST_ERROR("could not allocate %u bytes of physical memory: %s", size, strerror(errno));
		return NULL;
	}
	else
	{
		gpointer mem = (gpointer)m;
		GST_DEBUG("allocated %u bytes of physical memory at address %p", size, mem);
		return mem;
	}
}


gboolean gst_fsl_ipu_free_phys_mem(int ipu_fd, gpointer mem)
{
	dma_addr_t m;
	int ret;

	setup_debug_category();

	m = (dma_addr_t)mem;
	ret = ioctl(ipu_fd, IPU_FREE, &(m));
	if (ret < 0)
	{
		GST_ERROR("could not free physical memory at address %p: %s", mem, strerror(errno));
		return FALSE;
	}
	else
	{
		GST_DEBUG("freed physical memory at address %p", mem);
		return TRUE;
	}
}


static void gst_fsl_ipu_allocator_class_init(GstFslIpuAllocatorClass *klass)
{
	GObjectClass *object_class = G_OBJECT_CLASS(klass);
	GstAllocatorClass *parent_class = GST_ALLOCATOR_CLASS(klass);

	object_class->finalize = GST_DEBUG_FUNCPTR(gst_fsl_ipu_allocator_finalize);
	parent_class->alloc    = GST_DEBUG_FUNCPTR(gst_fsl_ipu_allocator_alloc);
	parent_class->free     = GST_DEBUG_FUNCPTR(gst_fsl_ipu_allocator_free);

	setup_debug_category();
}


static void gst_fsl_ipu_allocator_init(GstFslIpuAllocator *allocator)
{
	GstAllocator *parent = GST_ALLOCATOR(allocator);

	parent->mem_type    = GST_FSL_IPU_ALLOCATOR_MEM_TYPE;
	parent->mem_map     = GST_DEBUG_FUNCPTR(gst_fsl_ipu_allocator_map);
	parent->mem_unmap   = GST_DEBUG_FUNCPTR(gst_fsl_ipu_allocator_unmap);
	parent->mem_copy    = GST_DEBUG_FUNCPTR(gst_fsl_ipu_allocator_copy);
	parent->mem_share   = GST_DEBUG_FUNCPTR(gst_fsl_ipu_allocator_share);
	parent->mem_is_span = GST_DEBUG_FUNCPTR(gst_fsl_ipu_allocator_is_span);
}


static void gst_fsl_ipu_allocator_finalize(GObject *object)
{
	GST_DEBUG_OBJECT(object, "shutting down FSL IPU allocator");
	G_OBJECT_CLASS(gst_fsl_ipu_allocator_parent_class)->finalize(object);
}


static GstFslIpuMemory* gst_fsl_ipu_mem_new_internal(GstFslIpuAllocator *fsl_ipu_alloc, GstMemory *parent, gsize maxsize, GstMemoryFlags flags, gsize align, gsize offset, gsize size)
{
	GstFslIpuMemory *fsl_ipu_mem;
	fsl_ipu_mem = g_slice_alloc(sizeof(GstFslIpuMemory));
	fsl_ipu_mem->phys_addr = NULL;

	gst_memory_init(GST_MEMORY_CAST(fsl_ipu_mem), flags, GST_ALLOCATOR_CAST(fsl_ipu_alloc), parent, maxsize, align, offset, size);

	return fsl_ipu_mem;
}


static GstFslIpuMemory* gst_fsl_ipu_alloc_internal(GstAllocator *allocator, GstMemory *parent, gsize maxsize, GstMemoryFlags flags, gsize align, gsize offset, gsize size)
{
	GstFslIpuAllocator *fsl_ipu_alloc;
	GstFslIpuMemory *fsl_ipu_mem;

	fsl_ipu_alloc = GST_FSL_IPU_ALLOCATOR(allocator);

	GST_DEBUG_OBJECT(
		allocator,
		"allocating block with maxsize: %u, align: %u, offset: %u, size: %u",
		maxsize,
		align,
		offset,
		size
	);

	fsl_ipu_mem = gst_fsl_ipu_mem_new_internal(fsl_ipu_alloc, parent, maxsize, flags, align, offset, size);
	fsl_ipu_mem->phys_addr = gst_fsl_ipu_alloc_phys_mem(fsl_ipu_alloc->fd, maxsize);
	if (fsl_ipu_mem->phys_addr == NULL)
	{
		g_slice_free1(sizeof(GstFslIpuMemory), fsl_ipu_mem);
		return NULL;
	}

	if ((offset > 0) && (flags & GST_MEMORY_FLAG_ZERO_PREFIXED))
	{
		gpointer ptr = gst_fsl_ipu_allocator_map((GstMemory *)fsl_ipu_mem, maxsize, GST_MAP_WRITE);
		memset(ptr, 0, offset);
		gst_fsl_ipu_allocator_unmap((GstMemory *)fsl_ipu_mem);
	}

	return fsl_ipu_mem;
}


static GstMemory* gst_fsl_ipu_allocator_alloc(GstAllocator *allocator, gsize size, GstAllocationParams *params)
{
	gsize maxsize;
	GstFslIpuMemory *fsl_ipu_mem;

	maxsize = size + params->prefix + params->padding;
	fsl_ipu_mem = gst_fsl_ipu_alloc_internal(allocator, NULL, maxsize, params->flags, params->align, params->prefix, size);

	return (GstMemory *)fsl_ipu_mem;
}


static void gst_fsl_ipu_allocator_free(GstAllocator *allocator, GstMemory *memory)
{
	GstFslIpuMemory *fsl_ipu_mem = (GstFslIpuMemory *)memory;
	GstFslIpuAllocator *fsl_ipu_alloc = GST_FSL_IPU_ALLOCATOR(allocator);
	gst_fsl_ipu_free_phys_mem(fsl_ipu_alloc->fd, fsl_ipu_mem->phys_addr);
}


static gpointer gst_fsl_ipu_allocator_map(GstMemory *mem, gsize maxsize, GstMapFlags flags)
{
	int prot = 0;
	GstFslIpuMemory *fsl_ipu_mem = (GstFslIpuMemory *)mem;
	GstFslIpuAllocator *fsl_ipu_alloc = GST_FSL_IPU_ALLOCATOR(mem->allocator);

	if (flags & GST_MAP_READ)
		prot |= PROT_READ;
	if (flags & GST_MAP_WRITE)
		prot |= PROT_WRITE;

	fsl_ipu_mem->mapped_virt_addr = mmap(0, maxsize, prot, MAP_SHARED, fsl_ipu_alloc->fd, (dma_addr_t)(fsl_ipu_mem->phys_addr));

	return fsl_ipu_mem->mapped_virt_addr;
}


static void gst_fsl_ipu_allocator_unmap(GstMemory *mem)
{
	GstFslIpuMemory *fsl_ipu_mem = (GstFslIpuMemory *)mem;
	munmap(fsl_ipu_mem->mapped_virt_addr, mem->maxsize);
}


static GstMemory* gst_fsl_ipu_allocator_copy(GstMemory *mem, gssize offset, gssize size)
{
	GstFslIpuMemory *copy;

	if (size == -1)
		size = ((gssize)(mem->size) > offset) ? (mem->size - offset) : 0;

	copy = gst_fsl_ipu_alloc_internal(mem->allocator, NULL, mem->maxsize, 0, mem->align, mem->offset + offset, size);

	{
		gpointer srcptr, destptr;
		
		srcptr = gst_fsl_ipu_allocator_map(mem, mem->maxsize, GST_MAP_READ);
		destptr = gst_fsl_ipu_allocator_map((GstMemory *)copy, mem->maxsize, GST_MAP_WRITE);

		memcpy(destptr, srcptr, mem->maxsize);

		gst_fsl_ipu_allocator_unmap((GstMemory *)copy);
		gst_fsl_ipu_allocator_unmap(mem);
	}

	GST_DEBUG_OBJECT(
		mem->allocator,
		"copied block; offset: %d, size: %d; source block maxsize: %u, align: %u, offset: %u, size: %u",
		offset,
		size,
		mem->maxsize,
		mem->align,
		mem->offset,
		mem->size
	);

	return (GstMemory *)copy;
}


static GstMemory* gst_fsl_ipu_allocator_share(GstMemory *mem, gssize offset, gssize size)
{
	GstFslIpuMemory *fsl_ipu_mem;
	GstFslIpuMemory *sub;
	GstMemory *parent;

	fsl_ipu_mem = (GstFslIpuMemory *)mem;

	if (size == -1)
		size = ((gssize)(fsl_ipu_mem->mem.size) > offset) ? (fsl_ipu_mem->mem.size - offset) : 0;

	if ((parent = fsl_ipu_mem->mem.parent) == NULL)
		parent = (GstMemory *)mem;

	sub = gst_fsl_ipu_mem_new_internal(
		GST_FSL_IPU_ALLOCATOR(fsl_ipu_mem->mem.allocator),
		parent,
		fsl_ipu_mem->mem.maxsize,
		GST_MINI_OBJECT_FLAGS(parent) | GST_MINI_OBJECT_FLAG_LOCK_READONLY,
		fsl_ipu_mem->mem.align,
		fsl_ipu_mem->mem.offset + offset,
		size
	);
	sub->phys_addr = fsl_ipu_mem->phys_addr;

	GST_DEBUG_OBJECT(
		mem->allocator,
		"shared block; offset: %d, size: %d; source block maxsize: %u, align: %u, offset: %u, size: %u",
		offset,
		size,
		mem->maxsize,
		mem->align,
		mem->offset,
		mem->size
	);

	return (GstMemory *)sub;
}


static gboolean gst_fsl_ipu_allocator_is_span(G_GNUC_UNUSED GstMemory *mem1, G_GNUC_UNUSED GstMemory *mem2, G_GNUC_UNUSED gsize *offset)
{
	return FALSE;
}

