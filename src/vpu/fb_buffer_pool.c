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


#include <vpu_wrapper.h>
#include <string.h>
#include "../common/phys_mem_meta.h"
#include "fb_buffer_pool.h"
#include "utils.h"
#include "vpu_buffer_meta.h"


GST_DEBUG_CATEGORY_STATIC(imx_vpu_fb_buffer_pool_debug);
#define GST_CAT_DEFAULT imx_vpu_fb_buffer_pool_debug


static void gst_imx_vpu_fb_buffer_pool_finalize(GObject *object);
static const gchar ** gst_imx_vpu_fb_buffer_pool_get_options(GstBufferPool *pool);
static gboolean gst_imx_vpu_fb_buffer_pool_set_config(GstBufferPool *pool, GstStructure *config);
static GstFlowReturn gst_imx_vpu_fb_buffer_pool_alloc_buffer(GstBufferPool *pool, GstBuffer **buffer, GstBufferPoolAcquireParams *params);
static void gst_imx_vpu_fb_buffer_pool_release_buffer(GstBufferPool *pool, GstBuffer *buffer);


G_DEFINE_TYPE(GstImxVpuFbBufferPool, gst_imx_vpu_fb_buffer_pool, GST_TYPE_BUFFER_POOL)




static void gst_imx_vpu_fb_buffer_pool_finalize(GObject *object)
{
	GstImxVpuFbBufferPool *vpu_pool = GST_IMX_VPU_FB_BUFFER_POOL(object);

	if (vpu_pool->framebuffers != NULL)
		gst_object_unref(vpu_pool->framebuffers);

	GST_TRACE_OBJECT(vpu_pool, "shutting down buffer pool");

	G_OBJECT_CLASS(gst_imx_vpu_fb_buffer_pool_parent_class)->finalize(object);
}


static const gchar ** gst_imx_vpu_fb_buffer_pool_get_options(G_GNUC_UNUSED GstBufferPool *pool)
{
	static const gchar *options[] =
	{
		GST_BUFFER_POOL_OPTION_VIDEO_META,
		GST_BUFFER_POOL_OPTION_IMX_VPU_FRAMEBUFFER,
		NULL
	};

	return options;
}


static gboolean gst_imx_vpu_fb_buffer_pool_set_config(GstBufferPool *pool, GstStructure *config)
{
	GstImxVpuFbBufferPool *vpu_pool;
	GstVideoInfo info;
	GstCaps *caps;
	gsize size;
	guint min, max;

	vpu_pool = GST_IMX_VPU_FB_BUFFER_POOL(pool);

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

	vpu_pool->video_info.stride[0] = vpu_pool->framebuffers->y_stride;
	vpu_pool->video_info.stride[1] = vpu_pool->framebuffers->uv_stride;
	vpu_pool->video_info.stride[2] = vpu_pool->framebuffers->uv_stride;
	vpu_pool->video_info.offset[0] = 0;
	vpu_pool->video_info.offset[1] = vpu_pool->framebuffers->y_size;
	vpu_pool->video_info.offset[2] = vpu_pool->framebuffers->y_size + vpu_pool->framebuffers->u_size;
	vpu_pool->video_info.size = vpu_pool->framebuffers->total_size;

	vpu_pool->add_videometa = gst_buffer_pool_config_has_option(config, GST_BUFFER_POOL_OPTION_VIDEO_META);

	return GST_BUFFER_POOL_CLASS(gst_imx_vpu_fb_buffer_pool_parent_class)->set_config(pool, config);
}


static GstFlowReturn gst_imx_vpu_fb_buffer_pool_alloc_buffer(GstBufferPool *pool, GstBuffer **buffer, G_GNUC_UNUSED GstBufferPoolAcquireParams *params)
{
	GstBuffer *buf;
	GstImxVpuFbBufferPool *vpu_pool;
	GstVideoInfo *info;

	vpu_pool = GST_IMX_VPU_FB_BUFFER_POOL(pool);

	info = &(vpu_pool->video_info);

	buf = gst_buffer_new();
	if (buf == NULL)
	{
		GST_ERROR_OBJECT(pool, "could not create new buffer");
		return GST_FLOW_ERROR;
	}

	GST_IMX_VPU_BUFFER_META_ADD(buf);
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


static void gst_imx_vpu_fb_buffer_pool_release_buffer(GstBufferPool *pool, GstBuffer *buffer)
{
	GstImxVpuFbBufferPool *vpu_pool;

	vpu_pool = GST_IMX_VPU_FB_BUFFER_POOL(pool);
	g_assert(vpu_pool->framebuffers != NULL);

	if (vpu_pool->framebuffers->registration_state == GST_IMX_VPU_FRAMEBUFFERS_DECODER_REGISTERED)
	{
		VpuDecRetCode dec_ret;
		GstImxVpuBufferMeta *vpu_meta;

		vpu_meta = GST_IMX_VPU_BUFFER_META_GET(buffer);

		if (vpu_meta->framebuffer == NULL)
		{
			GST_DEBUG_OBJECT(pool, "buffer %p does not have VPU metadata - nothing to clear", (gpointer)buffer);
			return;
		}

		g_mutex_lock(&(vpu_pool->framebuffers->available_fb_mutex));

		if (vpu_meta->not_displayed_yet && vpu_pool->framebuffers->decenc_states.dec.decoder_open)
		{	
			dec_ret = VPU_DecOutFrameDisplayed(vpu_pool->framebuffers->decenc_states.dec.handle, vpu_meta->framebuffer);
			if (dec_ret != VPU_DEC_RET_SUCCESS)
				GST_ERROR_OBJECT(pool, "clearing display framebuffer failed: %s", gst_imx_vpu_strerror(dec_ret));
			else
			{
				vpu_meta->not_displayed_yet = FALSE;
				vpu_pool->framebuffers->num_available_framebuffers++;
				GST_DEBUG_OBJECT(pool, "cleared buffer %p", (gpointer)buffer);
			}
		}
		else if (!vpu_pool->framebuffers->decenc_states.dec.decoder_open)
			GST_DEBUG_OBJECT(pool, "not clearing buffer %p, since VPU decoder is closed", (gpointer)buffer);
		else
			GST_DEBUG_OBJECT(pool, "buffer %p already cleared", (gpointer)buffer);

		g_mutex_unlock(&(vpu_pool->framebuffers->available_fb_mutex));
	}

	GST_BUFFER_POOL_CLASS(gst_imx_vpu_fb_buffer_pool_parent_class)->release_buffer(pool, buffer);
}


static void gst_imx_vpu_fb_buffer_pool_class_init(GstImxVpuFbBufferPoolClass *klass)
{
	GObjectClass *object_class;
	GstBufferPoolClass *parent_class;
	
	object_class = G_OBJECT_CLASS(klass);
	parent_class = GST_BUFFER_POOL_CLASS(klass);

	GST_DEBUG_CATEGORY_INIT(imx_vpu_fb_buffer_pool_debug, "imxvpufbbufferpool", 0, "Freescale i.MX VPU framebuffers buffer pool");

	object_class->finalize       = GST_DEBUG_FUNCPTR(gst_imx_vpu_fb_buffer_pool_finalize);
	parent_class->get_options    = GST_DEBUG_FUNCPTR(gst_imx_vpu_fb_buffer_pool_get_options);
	parent_class->set_config     = GST_DEBUG_FUNCPTR(gst_imx_vpu_fb_buffer_pool_set_config);
	parent_class->alloc_buffer   = GST_DEBUG_FUNCPTR(gst_imx_vpu_fb_buffer_pool_alloc_buffer);
	parent_class->release_buffer = GST_DEBUG_FUNCPTR(gst_imx_vpu_fb_buffer_pool_release_buffer);
}


static void gst_imx_vpu_fb_buffer_pool_init(GstImxVpuFbBufferPool *pool)
{
	pool->framebuffers = NULL;
	pool->add_videometa = FALSE;

	GST_DEBUG_OBJECT(pool, "initializing VPU buffer pool");
}


GstBufferPool *gst_imx_vpu_fb_buffer_pool_new(GstImxVpuFramebuffers *framebuffers)
{
	GstImxVpuFbBufferPool *vpu_pool;

	g_assert(framebuffers != NULL);

	vpu_pool = g_object_new(gst_imx_vpu_fb_buffer_pool_get_type(), NULL);
	vpu_pool->framebuffers = gst_object_ref(framebuffers);

	return GST_BUFFER_POOL_CAST(vpu_pool);
}


void gst_imx_vpu_fb_buffer_pool_set_framebuffers(GstBufferPool *pool, GstImxVpuFramebuffers *framebuffers)
{
	GstImxVpuFbBufferPool *vpu_pool = GST_IMX_VPU_FB_BUFFER_POOL(pool);

	g_assert(framebuffers != NULL);

	if (framebuffers == vpu_pool->framebuffers)
		return;

	/* it is good practice to first ref the new, then unref the old object
	 * even though the case of identical pointers is caught above */
	gst_object_ref(framebuffers);

	if (vpu_pool->framebuffers != NULL)
		gst_object_unref(vpu_pool->framebuffers);

	vpu_pool->framebuffers = framebuffers;
}


gboolean gst_imx_vpu_set_buffer_contents(GstBuffer *buffer, GstImxVpuFramebuffers *framebuffers, VpuFrameBuffer *framebuffer, gboolean heap_mode)
{
	VpuDecRetCode dec_ret;
	GstVideoMeta *video_meta;
	GstImxVpuBufferMeta *vpu_meta;
	GstImxPhysMemMeta *phys_mem_meta;
	GstMemory *memory;

	video_meta = gst_buffer_get_video_meta(buffer);
	if (video_meta == NULL)
	{
		GST_ERROR("buffer with pointer %p has no video metadata", (gpointer)buffer);
		return FALSE;
	}

	vpu_meta = GST_IMX_VPU_BUFFER_META_GET(buffer);
	if (vpu_meta == NULL)
	{
		GST_ERROR("buffer with pointer %p has no VPU metadata", (gpointer)buffer);
		return FALSE;
	}

	phys_mem_meta = GST_IMX_PHYS_MEM_META_GET(buffer);
	if (phys_mem_meta == NULL)
	{
		GST_ERROR("buffer with pointer %p has no phys mem metadata", (gpointer)buffer);
		return FALSE;
	}

	if (heap_mode)
	{
		GstMapInfo map_info;

		memory = gst_allocator_alloc(NULL, framebuffers->total_size, NULL);
		gst_memory_map(memory, &map_info, GST_MAP_WRITE);
		memcpy(map_info.data, framebuffer->pbufVirtY, framebuffers->y_size);
		memcpy(map_info.data + framebuffers->y_size, framebuffer->pbufVirtCb, framebuffers->u_size);
		memcpy(map_info.data + framebuffers->y_size + framebuffers->u_size, framebuffer->pbufVirtCr, framebuffers->v_size);
		gst_memory_unmap(memory, &map_info);

		vpu_meta->framebuffer = NULL;

		phys_mem_meta->phys_addr = 0;
		phys_mem_meta->padding = 0;

		if (framebuffers->registration_state == GST_IMX_VPU_FRAMEBUFFERS_DECODER_REGISTERED)
		{
			dec_ret = VPU_DecOutFrameDisplayed(framebuffers->decenc_states.dec.handle, framebuffer);
			if (dec_ret != VPU_DEC_RET_SUCCESS)
				GST_ERROR("clearing display framebuffer failed: %s", gst_imx_vpu_strerror(dec_ret));
		}
	}
	else
	{
		gint y_padding = 0;

		if (framebuffers->pic_height > video_meta->height)
			y_padding = framebuffers->pic_height - video_meta->height;

		vpu_meta->framebuffer = framebuffer;

		phys_mem_meta->phys_addr = (guintptr)(framebuffer->pbufY);
		phys_mem_meta->padding = framebuffers->y_stride * y_padding;

		memory = gst_memory_new_wrapped(
			GST_MEMORY_FLAG_NO_SHARE,
			framebuffer->pbufVirtY,
			framebuffers->total_size,
			0,
			framebuffers->total_size,
			NULL,
			NULL
		);
	}


	gst_buffer_remove_all_memory(buffer);
	gst_buffer_append_memory(buffer, memory);

	return TRUE;
}


void gst_imx_vpu_mark_buf_as_not_displayed(GstBuffer *buffer)
{
	GstImxVpuBufferMeta *vpu_meta = GST_IMX_VPU_BUFFER_META_GET(buffer);
	g_assert(vpu_meta != NULL);
	vpu_meta->not_displayed_yet = TRUE;
}

