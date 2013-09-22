/* Common physical memory buffer pool
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


#ifndef GST_FSL_PHYS_MEM_BUFFER_POOL_H
#define GST_FSL_PHYS_MEM_BUFFER_POOL_H


#include <gst/gst.h>
#include <gst/video/video.h>
#include <gst/video/gstvideometa.h>
#include <gst/video/gstvideopool.h>


G_BEGIN_DECLS


typedef struct _GstFslPhysMemBufferPool GstFslPhysMemBufferPool;
typedef struct _GstFslPhysMemBufferPoolClass GstFslPhysMemBufferPoolClass;


#define GST_TYPE_FSL_PHYS_MEM_BUFFER_POOL             (gst_fsl_phys_mem_buffer_pool_get_type())
#define GST_FSL_PHYS_MEM_BUFFER_POOL(obj)             (G_TYPE_CHECK_INSTANCE_CAST((obj), GST_TYPE_FSL_PHYS_MEM_BUFFER_POOL, GstFslPhysMemBufferPool))
#define GST_FSL_PHYS_MEM_BUFFER_POOL_CLASS(klass)     (G_TYPE_CHECK_CLASS_CAST((klass), GST_TYPE_FSL_PHYS_MEM_BUFFER_POOL, GstFslPhysMemBufferPoolClass))


struct _GstFslPhysMemBufferPool
{
	GstBufferPool bufferpool;

	GstAllocator *allocator;
	GstVideoInfo video_info;
	gboolean add_video_meta;
	gboolean read_only;
};


struct _GstFslPhysMemBufferPoolClass
{
	GstBufferPoolClass parent_class;
};


GType gst_fsl_phys_mem_buffer_pool_get_type(void);
GstBufferPool *gst_fsl_phys_mem_buffer_pool_new(gboolean read_only);


G_END_DECLS


#endif

