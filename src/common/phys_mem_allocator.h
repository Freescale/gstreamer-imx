/* Common allocator for allocating physical memory
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


#ifndef GST_IMX_COMMON_PHYS_MEM_ALLOCATOR_H
#define GST_IMX_COMMON_PHYS_MEM_ALLOCATOR_H

#include <gst/gst.h>
#include <gst/gstallocator.h>

#include "phys_mem_addr.h"


G_BEGIN_DECLS


typedef struct _GstImxPhysMemAllocator GstImxPhysMemAllocator;
typedef struct _GstImxPhysMemAllocatorClass GstImxPhysMemAllocatorClass;
typedef struct _GstImxPhysMemory GstImxPhysMemory;


#define GST_TYPE_IMX_PHYS_MEM_ALLOCATOR             (gst_imx_phys_mem_allocator_get_type())
#define GST_IMX_PHYS_MEM_ALLOCATOR(obj)             (G_TYPE_CHECK_INSTANCE_CAST((obj), GST_TYPE_IMX_PHYS_MEM_ALLOCATOR, GstImxPhysMemAllocator))
#define GST_IMX_PHYS_MEM_ALLOCATOR_CLASS(klass)     (G_TYPE_CHECK_CLASS_CAST((klass), GST_TYPE_IMX_PHYS_MEM_ALLOCATOR, GstImxPhysMemAllocatorClass))
#define GST_IS_IMX_PHYS_MEM_ALLOCATOR(obj)          (G_TYPE_CHECK_INSTANCE_TYPE((obj), GST_TYPE_IMX_PHYS_MEM_ALLOCATOR))
#define GST_IS_IMX_PHYS_MEM_ALLOCATOR_CLASS(klass)  (G_TYPE_CHECK_CLASS_TYPE((klass), GST_TYPE_IMX_PHYS_MEM_ALLOCATOR))


struct _GstImxPhysMemAllocator
{
	GstAllocator parent;
};


struct _GstImxPhysMemAllocatorClass
{
	GstAllocatorClass parent_class;

	gboolean (*alloc_phys_mem)(GstImxPhysMemAllocator *allocator, GstImxPhysMemory *memory, gssize size);
	gboolean (*free_phys_mem)(GstImxPhysMemAllocator *allocator, GstImxPhysMemory *memory);
	gpointer (*map_phys_mem)(GstImxPhysMemAllocator *allocator, GstImxPhysMemory *memory, gssize size, GstMapFlags flags);
	void (*unmap_phys_mem)(GstImxPhysMemAllocator *allocator, GstImxPhysMemory *memory);
};


struct _GstImxPhysMemory
{
	GstMemory mem;

	gpointer mapped_virt_addr;
	gst_imx_phys_addr_t phys_addr;

	GstMapFlags mapping_flags;

	/* Counter to ensure the memory block isn't (un)mapped
	 * more often than necessary */
	long mapping_refcount;

	/* pointer for any additional internal data an allocator may define
	 * not for outside use; allocators do not have to use it */
	gpointer internal;
};


GType gst_imx_phys_mem_allocator_get_type(void);

guintptr gst_imx_phys_memory_get_phys_addr(GstMemory *mem);
gboolean gst_imx_is_phys_memory(GstMemory *mem);


G_END_DECLS


#endif
