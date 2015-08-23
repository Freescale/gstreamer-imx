/* GStreamer buffer pool for VPU-based decoding
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


#ifndef GST_IMX_VPU_DECODER_FRAMEBUFFER_POOL_H
#define GST_IMX_VPU_DECODER_FRAMEBUFFER_POOL_H

#include <gst/gst.h>
#include <gst/video/video.h>
#include <gst/video/gstvideometa.h>
#include <gst/video/gstvideopool.h>
#include "decoder_context.h"


G_BEGIN_DECLS


typedef struct _GstImxVpuDecoderFramebufferPool GstImxVpuDecoderFramebufferPool;
typedef struct _GstImxVpuDecoderFramebufferPoolClass GstImxVpuDecoderFramebufferPoolClass;


#define GST_TYPE_IMX_VPU_DECODER_FRAMEBUFFER_POOL             (gst_imx_vpu_decoder_framebuffer_pool_get_type())
#define GST_IMX_VPU_DECODER_FRAMEBUFFER_POOL(obj)             (G_TYPE_CHECK_INSTANCE_CAST((obj), GST_TYPE_IMX_VPU_DECODER_FRAMEBUFFER_POOL, GstImxVpuDecoderFramebufferPool))
#define GST_IMX_VPU_DECODER_FRAMEBUFFER_POOL_CLASS(klass)     (G_TYPE_CHECK_CLASS_CAST((klass), GST_TYPE_IMX_VPU_DECODER_FRAMEBUFFER_POOL, GstImxVpuDecoderFramebufferPoolClass))


#define GST_BUFFER_POOL_OPTION_IMX_VPU_DECODER_FRAMEBUFFER "GstBufferPoolOptionImxVpuDecoderFramebuffer"


struct _GstImxVpuDecoderFramebufferPool
{
	GstBufferPool bufferpool;

	GstImxVpuDecoderContext *decoder_context;
	GstVideoInfo video_info;
	gboolean add_videometa;
};


struct _GstImxVpuDecoderFramebufferPoolClass
{
	GstBufferPoolClass parent_class;
};


GType gst_imx_vpu_decoder_framebuffer_pool_get_type(void);

/* Returns a buffer pool, associated with the given decoder context.
 *
 * This call refs decoder_context, and unrefs it again when the buffer
 * pool gets disposed of.
 * The returned buffer pool is a floating reference.
 */
GstBufferPool *gst_imx_vpu_decoder_framebuffer_pool_new(GstImxVpuDecoderContext *decoder_context);


G_END_DECLS


#endif
