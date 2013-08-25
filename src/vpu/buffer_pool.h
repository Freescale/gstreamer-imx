/* GStreamer buffer pool for wrapped VPU framebuffers
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


#ifndef GST_FSL_VPU_BUFFER_POOL_H
#define GST_FSL_VPU_BUFFER_POOL_H

#include <gst/gst.h>
#include <gst/video/video.h>
#include <gst/video/gstvideometa.h>
#include <gst/video/gstvideopool.h>
#include "framebuffers.h"


G_BEGIN_DECLS


typedef struct _GstFslVpuBufferPool GstFslVpuBufferPool;
typedef struct _GstFslVpuBufferPoolClass GstFslVpuBufferPoolClass;
typedef struct _GstFslVpuFrameBufferExt GstFslVpuFrameBufferExt;


#define GST_TYPE_FSL_VPU_BUFFER_POOL             (gst_fsl_vpu_buffer_pool_get_type())
#define GST_FSL_VPU_BUFFER_POOL(obj)             (G_TYPE_CHECK_INSTANCE_CAST((obj), GST_TYPE_FSL_VPU_BUFFER_POOL, GstFslVpuBufferPool))
#define GST_FSL_VPU_BUFFER_POOL_CLASS(klass)     (G_TYPE_CHECK_CLASS_CAST((klass), GST_TYPE_FSL_VPU_BUFFER_POOL, GstFslVpuBufferPoolClass))


#define GST_BUFFER_POOL_OPTION_FSL_VPU_FRAMEBUFFER "GstBufferPoolOptionFslVpuFramebuffer"


struct _GstFslVpuBufferPool
{
	GstBufferPool bufferpool;

	GstFslVpuFramebuffers *framebuffers;
	GstVideoInfo video_info;
	gboolean add_videometa;
};


struct _GstFslVpuBufferPoolClass
{
	GstBufferPoolClass parent_class;
};


G_END_DECLS


GType gst_fsl_vpu_buffer_pool_get_type(void);

GstBufferPool *gst_fsl_vpu_buffer_pool_new(GstFslVpuFramebuffers *framebuffers);
void gst_fsl_vpu_buffer_pool_set_framebuffers(GstBufferPool *pool, GstFslVpuFramebuffers *framebuffers);

gboolean gst_fsl_vpu_set_buffer_contents(GstBuffer *buffer, GstFslVpuFramebuffers *framebuffers, VpuFrameBuffer *framebuffer, gboolean heap_mode);
void gst_fsl_vpu_mark_buf_as_not_displayed(GstBuffer *buffer);


#endif

