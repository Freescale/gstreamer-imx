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


#ifndef GST_IMX_VPU_ALLOCATOR_H
#define GST_IMX_VPU_ALLOCATOR_H

#include "imxvpuapi/imxvpuapi.h"
#include "../common/phys_mem_allocator.h"


G_BEGIN_DECLS


typedef struct _GstImxVpuAllocator GstImxVpuAllocator;
typedef struct _GstImxVpuAllocatorClass GstImxVpuAllocatorClass;


#define GST_TYPE_IMX_VPU_ALLOCATOR                (gst_imx_vpu_allocator_get_type())
#define GST_IMX_VPU_ALLOCATOR(obj)                (G_TYPE_CHECK_INSTANCE_CAST((obj), GST_TYPE_IMX_VPU_ALLOCATOR, GstImxVpuAllocator))
#define GST_IMX_VPU_ALLOCATOR_CAST(obj)           ((GstImxVpuAllocator *)(obj))
#define GST_IMX_VPU_ALLOCATOR_CLASS(klass)        (G_TYPE_CHECK_CLASS_CAST((klass), GST_TYPE_IMX_VPU_ALLOCATOR, GstImxVpuAllocatorClass))
#define GST_IS_IMX_VPU_ALLOCATOR(obj)             (G_TYPE_CHECK_INSTANCE_TYPE((obj), GST_TYPE_IMX_VPU_ALLOCATOR))
#define GST_IS_IMX_VPU_ALLOCATOR_CLASS(klass)     (G_TYPE_CHECK_CLASS_TYPE((klass), GST_TYPE_IMX_VPU_ALLOCATOR))

#define GST_IMX_VPU_IMXVPUAPI_ALLOCATOR(klass)    (GST_IMX_VPU_ALLOCATOR(allocator)->imxvpuapi_allocator)

#define GST_IMX_VPU_MEM_IMXVPUAPI_DMA_BUFFER(obj) ((ImxVpuDMABuffer*)(((GstImxPhysMemory *)(obj))->internal))


struct _GstImxVpuAllocator
{
	GstImxPhysMemAllocator parent;
	ImxVpuDMABufferAllocator *imxvpuapi_allocator;
};


/**
 * GstImxVpuAllocatorClass:
 *
 * Physical memory allocator using the imxvpuapi allocator API.
 * The @gst_imx_vpu_allocator_new is used for creating instances
 * of this class.
 *
 * @parent_class:   The parent class structure
 */
struct _GstImxVpuAllocatorClass
{
	GstImxPhysMemAllocatorClass parent_class;
};


GType gst_imx_vpu_allocator_get_type(void);

/* Creates a new GstImxVpuAllocator instance.
 *
 * imxvpuapi_allocator is imxvpuapi allocator that shall be used internally.
 * This allocator is used by VPU elements to allocate DMA memory.
 * It must stay valid for as long as the GstImxVpuAllocator instance exists.
 * mem_type is an identifier for the type of the allocator. It can have any
 * form as long as it uniquely identifies the type of the allocator. For every
 * different type of imxvpuapi allocator there must be a differen mem_type.
 */
GstAllocator* gst_imx_vpu_allocator_new(ImxVpuDMABufferAllocator *imxvpuapi_allocator, gchar const *mem_type);

/* Returns an imxvpuapi DMA buffer from the GstBuffer, or NULL if no DMA
 * buffer information is present in the GstBuffer. The function looks at
 * the buffer's first GstMemory block. If no blocks exist in the buffer,
 * or if the first block wasn't allocated by a GstImxVpuAllocator.
 */
ImxVpuDMABuffer* gst_imx_vpu_get_dma_buffer_from(GstBuffer *buffer);


G_END_DECLS


#endif
