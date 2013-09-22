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


#ifndef GST_FSL_COMMON_PHYS_MEM_ALLOCATOR_H
#define GST_FSL_COMMON_PHYS_MEM_ALLOCATOR_H

#include <gst/gst.h>
#include <gst/gstallocator.h>


G_BEGIN_DECLS


typedef struct _GstFslPhysMemAllocator GstFslPhysMemAllocator;
typedef struct _GstFslPhysMemAllocatorClass GstFslPhysMemAllocatorClass;
typedef struct _GstFslPhysMemory GstFslPhysMemory;


#define GST_TYPE_FSL_PHYS_MEM_ALLOCATOR             (gst_fsl_phys_mem_allocator_get_type())
#define GST_FSL_PHYS_MEM_ALLOCATOR(obj)             (G_TYPE_CHECK_INSTANCE_CAST((obj), GST_TYPE_FSL_PHYS_MEM_ALLOCATOR, GstFslPhysMemAllocator))
#define GST_FSL_PHYS_MEM_ALLOCATOR_CLASS(klass)     (G_TYPE_CHECK_CLASS_CAST((klass), GST_TYPE_FSL_PHYS_MEM_ALLOCATOR, GstFslPhysMemAllocatorClass))
#define GST_IS_FSL_PHYS_MEM_ALLOCATOR(obj)          (G_TYPE_CHECK_INSTANCE_TYPE((obj), GST_TYPE_FSL_PHYS_MEM_ALLOCATOR))
#define GST_IS_FSL_PHYS_MEM_ALLOCATOR_CLASS(klass)  (G_TYPE_CHECK_CLASS_TYPE((klass), GST_TYPE_FSL_PHYS_MEM_ALLOCATOR))


struct _GstFslPhysMemAllocator
{
	GstAllocator parent;
};


struct _GstFslPhysMemAllocatorClass
{
	GstAllocatorClass parent_class;

	gboolean (*alloc_phys_mem)(GstFslPhysMemAllocator *allocator, GstFslPhysMemory *memory, gssize size);
	gboolean (*free_phys_mem)(GstFslPhysMemAllocator *allocator, GstFslPhysMemory *memory);
	gpointer (*map_phys_mem)(GstFslPhysMemAllocator *allocator, GstFslPhysMemory *memory, gssize size, GstMapFlags flags);
	void (*unmap_phys_mem)(GstFslPhysMemAllocator *allocator, GstFslPhysMemory *memory);
};


struct _GstFslPhysMemory
{
	GstMemory mem;

	gpointer mapped_virt_addr;
	guintptr phys_addr;
	guintptr cpu_addr;
};


GType gst_fsl_phys_mem_allocator_get_type(void);

guintptr gst_fsl_phys_memory_get_phys_addr(GstMemory *mem);
guintptr gst_fsl_phys_memory_get_cpu_addr(GstMemory *mem);
gboolean gst_fsl_is_phys_memory(GstMemory *mem);


G_END_DECLS


#endif

