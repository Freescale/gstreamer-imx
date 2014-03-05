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


#ifndef GST_IMX_VPU_FB_BUFFER_POOL_H
#define GST_IMX_VPU_FB_BUFFER_POOL_H

#include <gst/gst.h>
#include <gst/video/video.h>
#include <gst/video/gstvideometa.h>
#include <gst/video/gstvideopool.h>
#include "framebuffers.h"


G_BEGIN_DECLS


typedef struct _GstImxVpuFbBufferPool GstImxVpuFbBufferPool;
typedef struct _GstImxVpuFbBufferPoolClass GstImxVpuFbBufferPoolClass;


#define GST_TYPE_IMX_VPU_FB_BUFFER_POOL             (gst_imx_vpu_fb_buffer_pool_get_type())
#define GST_IMX_VPU_FB_BUFFER_POOL(obj)             (G_TYPE_CHECK_INSTANCE_CAST((obj), GST_TYPE_IMX_VPU_FB_BUFFER_POOL, GstImxVpuFbBufferPool))
#define GST_IMX_VPU_FB_BUFFER_POOL_CLASS(klass)     (G_TYPE_CHECK_CLASS_CAST((klass), GST_TYPE_IMX_VPU_FB_BUFFER_POOL, GstImxVpuFbBufferPoolClass))


#define GST_BUFFER_POOL_OPTION_IMX_VPU_FRAMEBUFFER "GstBufferPoolOptionImxVpuFramebuffer"


struct _GstImxVpuFbBufferPool
{
	GstBufferPool bufferpool;

	GstImxVpuFramebuffers *framebuffers;
	GstVideoInfo video_info;
	gboolean add_videometa;
};


struct _GstImxVpuFbBufferPoolClass
{
	GstBufferPoolClass parent_class;
};


G_END_DECLS


GType gst_imx_vpu_fb_buffer_pool_get_type(void);

GstBufferPool *gst_imx_vpu_fb_buffer_pool_new(GstImxVpuFramebuffers *framebuffers);
void gst_imx_vpu_fb_buffer_pool_set_framebuffers(GstBufferPool *pool, GstImxVpuFramebuffers *framebuffers);

gboolean gst_imx_vpu_set_buffer_contents(GstBuffer *buffer, GstImxVpuFramebuffers *framebuffers, VpuFrameBuffer *framebuffer);
void gst_imx_vpu_mark_buf_as_not_displayed(GstBuffer *buffer);


#endif

