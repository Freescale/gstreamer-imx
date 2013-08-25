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


#ifndef VPU_BUFFERPOOL_H
#define VPU_BUFFERPOOL_H

#include <gst/gst.h>
#include <gst/video/video.h>
#include <gst/video/gstvideometa.h>
#include <gst/video/gstvideopool.h>
#include "vpu_framebuffers.h"


G_BEGIN_DECLS


typedef struct _GstFslVpuBufferMeta GstFslVpuBufferMeta;
typedef struct _GstFslPhysMemMeta GstFslPhysMemMeta;
typedef struct _GstFslVpuBufferPool GstFslVpuBufferPool;
typedef struct _GstFslVpuBufferPoolClass GstFslVpuBufferPoolClass;
typedef struct _GstFslVpuFrameBufferExt GstFslVpuFrameBufferExt;


#define GST_TYPE_FSL_VPU_BUFFER_POOL             (gst_fsl_vpu_buffer_pool_get_type())
#define GST_FSL_VPU_BUFFER_POOL(obj)             (G_TYPE_CHECK_INSTANCE_CAST((obj), GST_TYPE_FSL_VPU_BUFFER_POOL, GstFslVpuBufferPool))
#define GST_FSL_VPU_BUFFER_POOL_CLASS(klass)     (G_TYPE_CHECK_CLASS_CAST((klass), GST_TYPE_FSL_VPU_BUFFER_POOL, GstFslVpuBufferPoolClass))

#define GST_FSL_VPU_BUFFER_META_GET(buffer)      ((GstFslVpuBufferMeta *)gst_buffer_get_meta((buffer), gst_fsl_vpu_buffer_meta_api_get_type()))
#define GST_FSL_VPU_BUFFER_META_ADD(buffer)      (gst_buffer_add_meta((buffer), gst_fsl_vpu_buffer_meta_get_info(), NULL))
#define GST_FSL_VPU_BUFFER_META_DEL(buffer)      (gst_buffer_remove_meta((buffer), gst_buffer_get_meta((buffer), gst_fsl_vpu_buffer_meta_api_get_type())))


#define GST_FSL_PHYS_MEM_META_GET(buffer)      ((GstFslPhysMemMeta *)gst_buffer_get_meta((buffer), gst_fsl_phys_mem_meta_api_get_type()))
#define GST_FSL_PHYS_MEM_META_ADD(buffer)      (gst_buffer_add_meta((buffer), gst_fsl_phys_mem_meta_get_info(), NULL))
#define GST_FSL_PHYS_MEM_META_DEL(buffer)      (gst_buffer_remove_meta((buffer), gst_buffer_get_meta((buffer), gst_fsl_phys_mem_meta_api_get_type())))


#define GST_BUFFER_POOL_OPTION_FSL_VPU_FRAMEBUFFER "GstBufferPoolOptionFslVpuFramebuffer"


struct _GstFslVpuBufferMeta
{
	GstMeta meta;

	VpuFrameBuffer *framebuffer;
	gboolean not_displayed_yet;
};


struct _GstFslPhysMemMeta
{
	GstMeta meta;

	gpointer virt_addr, phys_addr;
	gsize padding;
};


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


GType gst_fsl_vpu_buffer_meta_api_get_type(void);
GstMetaInfo const * gst_fsl_vpu_buffer_meta_get_info(void);

GType gst_fsl_phys_mem_meta_api_get_type(void);
GstMetaInfo const * gst_fsl_phys_mem_meta_get_info(void);

GType gst_fsl_vpu_buffer_pool_get_type(void);

GstBufferPool *gst_fsl_vpu_buffer_pool_new(GstFslVpuFramebuffers *framebuffers);
void gst_fsl_vpu_buffer_pool_set_framebuffers(GstBufferPool *pool, GstFslVpuFramebuffers *framebuffers);

gboolean gst_fsl_vpu_set_buffer_contents(GstBuffer *buffer, GstFslVpuFramebuffers *framebuffers, VpuFrameBuffer *framebuffer, gboolean heap_mode);
void gst_fsl_vpu_mark_buf_as_not_displayed(GstBuffer *buffer);


#endif

