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


#include <string.h>
#include "imxvpuapi/imxvpuapi.h"
#include "../common/phys_mem_meta.h"
#include "decoder_framebuffer_pool.h"
#include "vpu_framebuffer_meta.h"


GST_DEBUG_CATEGORY_STATIC(imx_vpu_decoder_framebuffer_pool_debug);
#define GST_CAT_DEFAULT imx_vpu_decoder_framebuffer_pool_debug


static void gst_imx_vpu_decoder_framebuffer_pool_dispose(GObject *object);
static const gchar ** gst_imx_vpu_decoder_framebuffer_pool_get_options(GstBufferPool *pool);
static gboolean gst_imx_vpu_decoder_framebuffer_pool_set_config(GstBufferPool *pool, GstStructure *config);
static GstFlowReturn gst_imx_vpu_decoder_framebuffer_pool_alloc_buffer(GstBufferPool *pool, GstBuffer **buffer, GstBufferPoolAcquireParams *params);
static void gst_imx_vpu_decoder_framebuffer_pool_release_buffer(GstBufferPool *pool, GstBuffer *buffer);


G_DEFINE_TYPE(GstImxVpuDecoderFramebufferPool, gst_imx_vpu_decoder_framebuffer_pool, GST_TYPE_BUFFER_POOL)




static void gst_imx_vpu_decoder_framebuffer_pool_class_init(GstImxVpuDecoderFramebufferPoolClass *klass)
{
	GObjectClass *object_class;
	GstBufferPoolClass *parent_class;
	
	object_class = G_OBJECT_CLASS(klass);
	parent_class = GST_BUFFER_POOL_CLASS(klass);

	GST_DEBUG_CATEGORY_INIT(imx_vpu_decoder_framebuffer_pool_debug, "imxvpudecframebufferpool", 0, "Freescale i.MX VPU decoder framebuffer pool");

	object_class->dispose        = GST_DEBUG_FUNCPTR(gst_imx_vpu_decoder_framebuffer_pool_dispose);
	parent_class->get_options    = GST_DEBUG_FUNCPTR(gst_imx_vpu_decoder_framebuffer_pool_get_options);
	parent_class->set_config     = GST_DEBUG_FUNCPTR(gst_imx_vpu_decoder_framebuffer_pool_set_config);
	parent_class->alloc_buffer   = GST_DEBUG_FUNCPTR(gst_imx_vpu_decoder_framebuffer_pool_alloc_buffer);
	parent_class->release_buffer = GST_DEBUG_FUNCPTR(gst_imx_vpu_decoder_framebuffer_pool_release_buffer);
}


static void gst_imx_vpu_decoder_framebuffer_pool_init(GstImxVpuDecoderFramebufferPool *pool)
{
	pool->decoder_context = NULL;
	pool->add_videometa = FALSE;

	GST_INFO_OBJECT(pool, "initializing VPU buffer pool");
}


GstBufferPool *gst_imx_vpu_decoder_framebuffer_pool_new(GstImxVpuDecoderContext *decoder_context)
{
	GstImxVpuDecoderFramebufferPool *vpu_pool;
	vpu_pool = g_object_new(gst_imx_vpu_decoder_framebuffer_pool_get_type(), NULL);

	gst_object_ref(decoder_context);

	if (vpu_pool->decoder_context != NULL)
		gst_object_unref(vpu_pool->decoder_context);

	vpu_pool->decoder_context = decoder_context;

	return GST_BUFFER_POOL_CAST(vpu_pool);
}


static void gst_imx_vpu_decoder_framebuffer_pool_dispose(GObject *object)
{
	GstImxVpuDecoderFramebufferPool *vpu_pool = GST_IMX_VPU_DECODER_FRAMEBUFFER_POOL(object);

	if (vpu_pool->decoder_context != NULL)
	{
		gst_object_unref(vpu_pool->decoder_context);
		vpu_pool->decoder_context = NULL;
	}

	GST_TRACE_OBJECT(vpu_pool, "shutting down buffer pool");

	G_OBJECT_CLASS(gst_imx_vpu_decoder_framebuffer_pool_parent_class)->dispose(object);
}


static const gchar ** gst_imx_vpu_decoder_framebuffer_pool_get_options(G_GNUC_UNUSED GstBufferPool *pool)
{
	static const gchar *options[] =
	{
		GST_BUFFER_POOL_OPTION_VIDEO_META,
		GST_BUFFER_POOL_OPTION_IMX_VPU_DECODER_FRAMEBUFFER,
		NULL
	};

	return options;
}


static gboolean gst_imx_vpu_decoder_framebuffer_pool_set_config(GstBufferPool *pool, GstStructure *config)
{
	GstImxVpuDecoderFramebufferPool *vpu_pool;
	GstVideoInfo info;
	GstCaps *caps;
	gsize size;
	guint min, max;
	ImxVpuFramebufferSizes *fb_sizes;

	vpu_pool = GST_IMX_VPU_DECODER_FRAMEBUFFER_POOL(pool);

	if (!gst_buffer_pool_config_get_params(config, &caps, &size, &min, &max))
	{
		GST_ERROR_OBJECT(pool, "pool configuration invalid");
		return FALSE;
	}

	if (caps == NULL)
	{
		GST_ERROR_OBJECT(pool, "configuration contains no caps");
		return FALSE;
	}

	if (!gst_video_info_from_caps(&info, caps))
	{
		GST_ERROR_OBJECT(pool, "caps cannot be parsed for video info");
		return FALSE;
	}

	vpu_pool->video_info = info;

	fb_sizes = &(vpu_pool->decoder_context->framebuffer_array->framebuffer_sizes);

	/* Setup the stride sizes according to the framebuffer sizes.
	 * The framebuffer_sizes struct can contain different stride values, depending
	 * on the needs of the VPU. */
	vpu_pool->video_info.stride[0] = fb_sizes->y_stride;
	vpu_pool->video_info.stride[1] = fb_sizes->cbcr_stride;
	vpu_pool->video_info.stride[2] = fb_sizes->cbcr_stride;
	/* Setup the stride sizes according to the framebuffer offsets. */
	vpu_pool->video_info.offset[0] = 0;
	vpu_pool->video_info.offset[1] = fb_sizes->y_size;
	vpu_pool->video_info.offset[2] = fb_sizes->y_size + fb_sizes->cbcr_size;
	vpu_pool->video_info.size = fb_sizes->total_size;

	vpu_pool->add_videometa = gst_buffer_pool_config_has_option(config, GST_BUFFER_POOL_OPTION_VIDEO_META);

	return GST_BUFFER_POOL_CLASS(gst_imx_vpu_decoder_framebuffer_pool_parent_class)->set_config(pool, config);
}


static GstFlowReturn gst_imx_vpu_decoder_framebuffer_pool_alloc_buffer(GstBufferPool *pool, GstBuffer **buffer, G_GNUC_UNUSED GstBufferPoolAcquireParams *params)
{
	GstBuffer *buf;
	GstImxVpuDecoderFramebufferPool *vpu_pool;
	GstVideoInfo *info;

	vpu_pool = GST_IMX_VPU_DECODER_FRAMEBUFFER_POOL(pool);

	info = &(vpu_pool->video_info);

	buf = gst_buffer_new();
	if (buf == NULL)
	{
		GST_ERROR_OBJECT(pool, "could not create new buffer");
		return GST_FLOW_ERROR;
	}

	GST_IMX_VPU_FRAMEBUFFER_META_ADD(buf);
	GST_IMX_PHYS_MEM_META_ADD(buf);

	if (vpu_pool->add_videometa)
	{
		gst_buffer_add_video_meta_full(
			buf,
			GST_VIDEO_FRAME_FLAG_NONE,
			GST_VIDEO_INFO_FORMAT(info),
			GST_VIDEO_INFO_WIDTH(info), GST_VIDEO_INFO_HEIGHT(info),
			GST_VIDEO_INFO_N_PLANES(info),
			info->offset,
			info->stride
		);
	}

	*buffer = buf;

	return GST_FLOW_OK;
}


static void gst_imx_vpu_decoder_framebuffer_pool_release_buffer(GstBufferPool *pool, GstBuffer *buffer)
{
	GstImxVpuFramebufferMeta *vpu_meta;
	GstImxVpuDecoderFramebufferPool *vpu_pool = GST_IMX_VPU_DECODER_FRAMEBUFFER_POOL(pool);

	/* Here, the imxvpuapi framebuffer contained within the GstBuffer is
	 * marked as displayed, which returns the framebuffer to the VPU's pool.
	 * Without this, the VPU would eventually run out of free framebuffers
	 * to decode into.
	 * Need to lock the mutex here, since the buffer might be getting released
	 * while the decoder is decoding, which would lead to race conditions. */

	GST_IMX_VPU_DECODER_CONTEXT_LOCK(vpu_pool->decoder_context);

	if ((vpu_meta = GST_IMX_VPU_FRAMEBUFFER_META_GET(buffer)) != NULL)
		gst_imx_vpu_decoder_context_mark_as_displayed(vpu_pool->decoder_context, vpu_meta->framebuffer);
	else
		GST_DEBUG_OBJECT(pool, "nothing to mark - there is no VPU metadata for buffer %p", (gpointer)buffer);

	/* Signal the condition variable, unblocking the wait function inside the
	 * decoder's handle_frame() function. In other words, this signals to the
	 * decoder that a framebuffer is free and available again, and decoding
	 * can proceed. */
	g_cond_signal(&(vpu_pool->decoder_context->cond));

	GST_IMX_VPU_DECODER_CONTEXT_UNLOCK(vpu_pool->decoder_context);

	GST_BUFFER_POOL_CLASS(gst_imx_vpu_decoder_framebuffer_pool_parent_class)->release_buffer(pool, buffer);
}
