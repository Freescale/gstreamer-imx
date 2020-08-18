/* gstreamer-imx: GStreamer plugins for the i.MX SoCs
 * Copyright (C) 2019  Carlos Rafael Giani
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
#include <gst/gst.h>
#include <gst/video/video.h>
#include <gst/video/gstvideometa.h>
#include "gst/imx/gstimxdmabufferallocator.h"
#include "gstimxvpudecbufferpool.h"
#include "gstimxvpudeccontext.h"


GST_DEBUG_CATEGORY_STATIC(imx_vpu_dec_buffer_pool_debug);
#define GST_CAT_DEFAULT imx_vpu_dec_buffer_pool_debug


typedef enum
{
	GST_BUFFER_FLAG_IMX_VPU_RESERVED_FRAMEBUFFER = (GST_VIDEO_BUFFER_FLAG_LAST << 0),
	GST_BUFFER_FLAG_IMX_VPU_FRAMEBUFFER_NEEDS_TO_BE_RETURNED = (GST_VIDEO_BUFFER_FLAG_LAST << 1)
}
GstImxVpuBufferFlags;


struct _GstImxVpuDecBufferPool
{
	GstBufferPool parent;

	/*< private >*/

	GstImxVpuDecContext *decoder_context;
	ImxVpuApiDecStreamInfo stream_info;
	GstBuffer *selected_reserved_buffer;
	GSList *reserved_buffers;
	GMutex selected_buffer_mutex;
	GstVideoInfo video_info;
	gboolean add_videometa;
};


struct _GstImxVpuDecBufferPoolClass
{
	GstBufferPoolClass parent_class;
};


static void gst_imx_vpu_dec_buffer_pool_finalize(GObject *object);
static const gchar ** gst_imx_vpu_dec_buffer_pool_get_options(GstBufferPool *pool);
static gboolean gst_imx_vpu_dec_buffer_pool_set_config(GstBufferPool *pool, GstStructure *config);
static gboolean gst_imx_vpu_dec_buffer_pool_start(GstBufferPool *pool);
static gboolean gst_imx_vpu_dec_buffer_pool_stop(GstBufferPool *pool);
static GstFlowReturn gst_imx_vpu_dec_buffer_pool_acquire_buffer(GstBufferPool *pool, GstBuffer **buffer, GstBufferPoolAcquireParams *params);
static GstFlowReturn gst_imx_vpu_dec_buffer_pool_alloc_buffer(GstBufferPool *pool, GstBuffer **buffer, GstBufferPoolAcquireParams *params);
static void gst_imx_vpu_dec_buffer_pool_release_buffer(GstBufferPool *pool, GstBuffer *buffer);
static void gst_imx_vpu_dec_buffer_pool_reset_buffer(GstBufferPool *pool, GstBuffer *buffer);
static void gst_imx_vpu_dec_buffer_pool_free_buffer(GstBufferPool *pool, GstBuffer *buffer);



G_DEFINE_TYPE(GstImxVpuDecBufferPool, gst_imx_vpu_dec_buffer_pool, GST_TYPE_BUFFER_POOL)




static void gst_imx_vpu_dec_buffer_pool_class_init(GstImxVpuDecBufferPoolClass *klass)
{
	GObjectClass *object_class;
	GstBufferPoolClass *buffer_pool_class;
	
	object_class = G_OBJECT_CLASS(klass);
	buffer_pool_class = GST_BUFFER_POOL_CLASS(klass);

	GST_DEBUG_CATEGORY_INIT(imx_vpu_dec_buffer_pool_debug, "imxvpudecframebufferpool", 0, "NXP i.MX VPU decoder buffer pool");

	object_class->finalize            = GST_DEBUG_FUNCPTR(gst_imx_vpu_dec_buffer_pool_finalize);
	buffer_pool_class->get_options    = GST_DEBUG_FUNCPTR(gst_imx_vpu_dec_buffer_pool_get_options);
	buffer_pool_class->set_config     = GST_DEBUG_FUNCPTR(gst_imx_vpu_dec_buffer_pool_set_config);
	buffer_pool_class->start          = GST_DEBUG_FUNCPTR(gst_imx_vpu_dec_buffer_pool_start);
	buffer_pool_class->stop           = GST_DEBUG_FUNCPTR(gst_imx_vpu_dec_buffer_pool_stop);
	buffer_pool_class->acquire_buffer = GST_DEBUG_FUNCPTR(gst_imx_vpu_dec_buffer_pool_acquire_buffer);
	buffer_pool_class->alloc_buffer   = GST_DEBUG_FUNCPTR(gst_imx_vpu_dec_buffer_pool_alloc_buffer);
	buffer_pool_class->release_buffer = GST_DEBUG_FUNCPTR(gst_imx_vpu_dec_buffer_pool_release_buffer);
	buffer_pool_class->reset_buffer   = GST_DEBUG_FUNCPTR(gst_imx_vpu_dec_buffer_pool_reset_buffer);
	buffer_pool_class->free_buffer    = GST_DEBUG_FUNCPTR(gst_imx_vpu_dec_buffer_pool_free_buffer);
}


static void gst_imx_vpu_dec_buffer_pool_init(GstImxVpuDecBufferPool *imx_vpu_dec_buffer_pool)
{
	GST_DEBUG_OBJECT(imx_vpu_dec_buffer_pool, "initializing buffer pool");

	imx_vpu_dec_buffer_pool->decoder_context = NULL;

	memset(&(imx_vpu_dec_buffer_pool->stream_info), 0, sizeof(imx_vpu_dec_buffer_pool->stream_info));

	imx_vpu_dec_buffer_pool->selected_reserved_buffer = NULL;
	imx_vpu_dec_buffer_pool->reserved_buffers = NULL;
	g_mutex_init(&(imx_vpu_dec_buffer_pool->selected_buffer_mutex));

	gst_video_info_init(&(imx_vpu_dec_buffer_pool->video_info));
	imx_vpu_dec_buffer_pool->add_videometa = FALSE;
}


static void gst_imx_vpu_dec_buffer_pool_finalize(GObject *object)
{
	GstImxVpuDecBufferPool *imx_vpu_dec_buffer_pool = GST_IMX_VPU_DEC_BUFFER_POOL(object);

	GST_DEBUG_OBJECT(imx_vpu_dec_buffer_pool, "shutting down buffer pool");

	if (imx_vpu_dec_buffer_pool->decoder_context != NULL)
		gst_object_unref(GST_OBJECT(imx_vpu_dec_buffer_pool->decoder_context));

	g_mutex_clear(&(imx_vpu_dec_buffer_pool->selected_buffer_mutex));

	G_OBJECT_CLASS(gst_imx_vpu_dec_buffer_pool_parent_class)->finalize(object);
}


static const gchar ** gst_imx_vpu_dec_buffer_pool_get_options(G_GNUC_UNUSED GstBufferPool *pool)
{
	static const gchar *options[] =
	{
		GST_BUFFER_POOL_OPTION_VIDEO_META,
		GST_BUFFER_POOL_OPTION_IMX_VPU_DEC_BUFFER_POOL,
		NULL
	};

	return options;
}


static gboolean gst_imx_vpu_dec_buffer_pool_set_config(GstBufferPool *pool, GstStructure *config)
{
	gboolean ret;
	GstImxVpuDecBufferPool *imx_vpu_dec_buffer_pool;
	ImxVpuApiDecStreamInfo *stream_info;
	ImxVpuApiFramebufferMetrics *fb_metrics;
	GstVideoInfo info;
	GstCaps *caps;
	guint size;

	imx_vpu_dec_buffer_pool = GST_IMX_VPU_DEC_BUFFER_POOL(pool);

	if (!gst_buffer_pool_config_get_params(config, &caps, &size, NULL, NULL))
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
		GST_ERROR_OBJECT(pool, "caps cannot be parsed as video info");
		return FALSE;
	}

	imx_vpu_dec_buffer_pool->video_info = info;

	stream_info = &(imx_vpu_dec_buffer_pool->stream_info);
	fb_metrics = &(stream_info->decoded_frame_framebuffer_metrics);

	/* Set the video info width/height to the actual frame
	 * width/height to exclude padding rows and columns. */
	imx_vpu_dec_buffer_pool->video_info.width = fb_metrics->actual_frame_width;
	imx_vpu_dec_buffer_pool->video_info.height = fb_metrics->actual_frame_height;
	/* Set up the stride sizes according to the framebuffer metrics.
	 * The framebuffer_sizes struct can contain different stride values,
	 * depending on the needs of the VPU. */
	imx_vpu_dec_buffer_pool->video_info.stride[0] = fb_metrics->y_stride;
	imx_vpu_dec_buffer_pool->video_info.stride[1] = fb_metrics->uv_stride;
	imx_vpu_dec_buffer_pool->video_info.stride[2] = fb_metrics->uv_stride;
	/* Set up the stride sizes according to the framebuffer offsets. */
	imx_vpu_dec_buffer_pool->video_info.offset[0] = 0;
	imx_vpu_dec_buffer_pool->video_info.offset[1] = fb_metrics->y_size;
	imx_vpu_dec_buffer_pool->video_info.offset[2] = fb_metrics->y_size + fb_metrics->uv_size;
	imx_vpu_dec_buffer_pool->video_info.size = MAX(stream_info->min_output_framebuffer_size, size);

	imx_vpu_dec_buffer_pool->add_videometa = gst_buffer_pool_config_has_option(config, GST_BUFFER_POOL_OPTION_VIDEO_META);

	GST_DEBUG_OBJECT(
		imx_vpu_dec_buffer_pool,
		"configuring buffer pool with stream info:"
		"  Y/Cb/Cr strides: %d/%d/%d"
		"  Y/Cb/Cr offsets: %" G_GSIZE_FORMAT "/%" G_GSIZE_FORMAT "/%" G_GSIZE_FORMAT
		"  frame size: %" G_GSIZE_FORMAT " bytes"
		"  with videometa: %d",
		imx_vpu_dec_buffer_pool->video_info.stride[0],
		imx_vpu_dec_buffer_pool->video_info.stride[1],
		imx_vpu_dec_buffer_pool->video_info.stride[2],
		imx_vpu_dec_buffer_pool->video_info.offset[0],
		imx_vpu_dec_buffer_pool->video_info.offset[1],
		imx_vpu_dec_buffer_pool->video_info.offset[2],
		imx_vpu_dec_buffer_pool->video_info.size,
		imx_vpu_dec_buffer_pool->add_videometa
	);

	ret = GST_BUFFER_POOL_CLASS(gst_imx_vpu_dec_buffer_pool_parent_class)->set_config(pool, config);

	/* Check that the allocator can allocate DMA buffers.
	 * This is essential for i.MX VPU operation. */
	if (ret)
	{
		GstAllocator *allocator = NULL;
		gboolean is_dma_buffer_allocator = gst_buffer_pool_config_get_allocator(config, &(allocator), NULL) && GST_IS_IMX_DMA_BUFFER_ALLOCATOR(allocator);

		if (G_UNLIKELY(!is_dma_buffer_allocator))
		{
			GST_ERROR_OBJECT(imx_vpu_dec_buffer_pool, "cannot configure the buffer pool because its allocator cannot allocate DMA buffers");
			ret = FALSE;
		}
	}

	return ret;
}


static gboolean gst_imx_vpu_dec_buffer_pool_start(GstBufferPool *pool)
{
	GST_DEBUG_OBJECT(pool, "starting imxvpudec buffer pool");
	return GST_BUFFER_POOL_CLASS(gst_imx_vpu_dec_buffer_pool_parent_class)->start(pool);
}


static gboolean gst_imx_vpu_dec_buffer_pool_stop(GstBufferPool *pool)
{
	GSList *reserved_buffer_list_item;
	GstImxVpuDecBufferPool *imx_vpu_dec_buffer_pool = GST_IMX_VPU_DEC_BUFFER_POOL(pool);

	GST_DEBUG_OBJECT(imx_vpu_dec_buffer_pool, "stopping imxvpudec buffer pool");

	for (reserved_buffer_list_item = imx_vpu_dec_buffer_pool->reserved_buffers; reserved_buffer_list_item != NULL; reserved_buffer_list_item = reserved_buffer_list_item->next)
	{
		GstBuffer *buffer = GST_BUFFER_CAST(reserved_buffer_list_item->data);
		GST_DEBUG_OBJECT(imx_vpu_dec_buffer_pool, "freeing reserved gstbuffer %p", (gpointer)buffer);
		gst_buffer_unref(buffer);
	}

	g_slist_free(imx_vpu_dec_buffer_pool->reserved_buffers);

	return GST_BUFFER_POOL_CLASS(gst_imx_vpu_dec_buffer_pool_parent_class)->stop(pool);
}


static GstFlowReturn gst_imx_vpu_dec_buffer_pool_acquire_buffer(GstBufferPool *pool, GstBuffer **buffer, GstBufferPoolAcquireParams *params)
{
	GstImxVpuDecBufferPool *imx_vpu_dec_buffer_pool = GST_IMX_VPU_DEC_BUFFER_POOL(pool);

	/* NOTE: Using the flag instead of testing for the selected buffer directly,
	 * because this way, a race condition is avoided (thread A selects a reserved
	 * buffer while thread B does a regular acquire call). */
	if ((params != NULL) && (params->flags & GST_IMX_VPU_DEC_BUFFER_POOL_ACQUIRE_FLAG_SELECTED))
	{
		g_mutex_lock(&(imx_vpu_dec_buffer_pool->selected_buffer_mutex));
		*buffer = imx_vpu_dec_buffer_pool->selected_reserved_buffer;
		g_mutex_unlock(&(imx_vpu_dec_buffer_pool->selected_buffer_mutex));

		/* Set this flag to make sure the buffer is returned to the VPU in the
		 * release() function. */
		GST_BUFFER_FLAG_SET(*buffer, GST_BUFFER_FLAG_IMX_VPU_FRAMEBUFFER_NEEDS_TO_BE_RETURNED);

		GST_LOG_OBJECT(imx_vpu_dec_buffer_pool, "acquired reserved gstbuffer %p", (gpointer)(*buffer));

		return GST_FLOW_OK;
	}
	else
	{
		GstFlowReturn flow_ret = GST_BUFFER_POOL_CLASS(gst_imx_vpu_dec_buffer_pool_parent_class)->acquire_buffer(pool, buffer, params);

		if (G_LIKELY(flow_ret == GST_FLOW_OK))
			GST_LOG_OBJECT(imx_vpu_dec_buffer_pool, "acquired regular gstbuffer %p", (gpointer)(*buffer));
		else
			GST_ERROR_OBJECT(imx_vpu_dec_buffer_pool, "could not acquire regular gstbuffer: %s", gst_flow_get_name(flow_ret));

		return flow_ret;
	}
}


static GstFlowReturn gst_imx_vpu_dec_buffer_pool_alloc_buffer(GstBufferPool *pool, GstBuffer **buffer, GstBufferPoolAcquireParams *params)
{
	GstFlowReturn flow_ret;
	GstImxVpuDecBufferPool *imx_vpu_dec_buffer_pool = GST_IMX_VPU_DEC_BUFFER_POOL(pool);
	ImxVpuApiDecStreamInfo *stream_info = &(imx_vpu_dec_buffer_pool->stream_info);

	if (G_UNLIKELY((flow_ret = GST_BUFFER_POOL_CLASS(gst_imx_vpu_dec_buffer_pool_parent_class)->alloc_buffer(pool, buffer, params)) != GST_FLOW_OK))
	{
		GST_ERROR_OBJECT(imx_vpu_dec_buffer_pool, "could not allocate gstbuffer: %s", gst_flow_get_name(flow_ret));
		return flow_ret;
	}
	else
		GST_LOG_OBJECT(imx_vpu_dec_buffer_pool, "allocated gstbuffer %p", (gpointer)(*buffer));

	g_assert(*buffer != NULL);

	if (imx_vpu_dec_buffer_pool->add_videometa)
	{
		GstVideoInfo *video_info = &(imx_vpu_dec_buffer_pool->video_info);

		gst_buffer_add_video_meta_full(
			*buffer,
			GST_VIDEO_FRAME_FLAG_NONE,
			GST_VIDEO_INFO_FORMAT(video_info),
			GST_VIDEO_INFO_WIDTH(video_info), GST_VIDEO_INFO_HEIGHT(video_info),
			GST_VIDEO_INFO_N_PLANES(video_info),
			video_info->offset,
			video_info->stride
		);
	}

	if (stream_info->has_crop_rectangle)
	{
		GstVideoCropMeta *crop_meta = gst_buffer_add_video_crop_meta(*buffer);
		crop_meta->x = stream_info->crop_left;
		crop_meta->y = stream_info->crop_top;
		crop_meta->width = stream_info->crop_width;
		crop_meta->height = stream_info->crop_height;
	}

	return flow_ret;
}


static void gst_imx_vpu_dec_buffer_pool_release_buffer(GstBufferPool *pool, GstBuffer *buffer)
{
	GstImxVpuDecBufferPool *imx_vpu_dec_buffer_pool = GST_IMX_VPU_DEC_BUFFER_POOL(pool);

	if (GST_BUFFER_FLAG_IS_SET(buffer, GST_BUFFER_FLAG_IMX_VPU_RESERVED_FRAMEBUFFER))
	{
		ImxDmaBuffer *framebuffer = gst_imx_get_dma_buffer_from_buffer(buffer);

		g_assert(imx_vpu_dec_buffer_pool->decoder_context != NULL);

		if (G_LIKELY(GST_BUFFER_FLAG_IS_SET(buffer, GST_BUFFER_FLAG_IMX_VPU_FRAMEBUFFER_NEEDS_TO_BE_RETURNED)))
		{
			GST_LOG_OBJECT(imx_vpu_dec_buffer_pool, "returning framebuffer %p to decoder from reserved gstbuffer %p", (gpointer)framebuffer, (gpointer)buffer);
			gst_imx_vpu_dec_context_return_framebuffer_to_decoder(imx_vpu_dec_buffer_pool->decoder_context, framebuffer);
		}

		GST_BUFFER_FLAG_UNSET(buffer, GST_BUFFER_FLAG_IMX_VPU_FRAMEBUFFER_NEEDS_TO_BE_RETURNED);
	}
	else
	{
		GST_LOG_OBJECT(imx_vpu_dec_buffer_pool, "returning regular gstbuffer %p", (gpointer)buffer);
		GST_BUFFER_POOL_CLASS(gst_imx_vpu_dec_buffer_pool_parent_class)->release_buffer(pool, buffer);
	}
}


static void gst_imx_vpu_dec_buffer_pool_reset_buffer(GstBufferPool *pool, GstBuffer *buffer)
{
	/* Default reset_buffer function erases all buffer flag except
	 * for GST_BUFFER_FLAG_TAG_MEMORY. Here, we preserve any extra
	 * VPU related flags we may have added to the buffer. */

	guint vpu_flags = GST_BUFFER_FLAGS (buffer) & (GST_BUFFER_FLAG_IMX_VPU_RESERVED_FRAMEBUFFER | GST_BUFFER_FLAG_IMX_VPU_FRAMEBUFFER_NEEDS_TO_BE_RETURNED);
	GST_BUFFER_POOL_CLASS(gst_imx_vpu_dec_buffer_pool_parent_class)->reset_buffer(pool, buffer);
	GST_BUFFER_FLAGS (buffer) |= vpu_flags;
}


static void gst_imx_vpu_dec_buffer_pool_free_buffer(GstBufferPool *pool, GstBuffer *buffer)
{
	GST_DEBUG_OBJECT(pool, "freeing regular gstbuffer %p", (gpointer)buffer);
	GST_BUFFER_POOL_CLASS(gst_imx_vpu_dec_buffer_pool_parent_class)->free_buffer(pool, buffer);
}


GstImxVpuDecBufferPool* gst_imx_vpu_dec_buffer_pool_new(ImxVpuApiDecStreamInfo *stream_info, GstImxVpuDecContext *decoder_context)
{
	GstImxVpuDecBufferPool *imx_vpu_dec_buffer_pool;

	g_assert(decoder_context != NULL);
	g_assert(stream_info != NULL);

	gst_object_ref(GST_OBJECT(decoder_context));

	imx_vpu_dec_buffer_pool = g_object_new(gst_imx_vpu_dec_buffer_pool_get_type(), NULL);
	imx_vpu_dec_buffer_pool->decoder_context = decoder_context;
	imx_vpu_dec_buffer_pool->stream_info = *stream_info;

	// Clear the floating flag, since it is not useful
	// with buffer pools, and could lead to subtle
	// bugs related to refcounting.
	gst_object_ref_sink(GST_OBJECT(imx_vpu_dec_buffer_pool));

	return imx_vpu_dec_buffer_pool;
}


static gboolean mark_meta_pooled(GstBuffer *buffer, GstMeta **meta, gpointer user_data)
{
	GST_DEBUG_OBJECT(GST_BUFFER_POOL(user_data), "marking meta %p as POOLED in buffer %p", (gpointer)(*meta), (gpointer)buffer);
	GST_META_FLAG_SET(*meta, GST_META_FLAG_POOLED);
	GST_META_FLAG_SET(*meta, GST_META_FLAG_LOCKED);
	return TRUE;
}


GstBuffer* gst_imx_vpu_dec_buffer_pool_reserve_buffer(GstImxVpuDecBufferPool *imx_vpu_dec_buffer_pool)
{
	GstFlowReturn flow_ret;
	GstBuffer *buffer;
	GstBufferPoolClass *bufferpool_class = GST_BUFFER_POOL_GET_CLASS(imx_vpu_dec_buffer_pool);

	if ((flow_ret = bufferpool_class->alloc_buffer(GST_BUFFER_POOL_CAST(imx_vpu_dec_buffer_pool), &buffer, NULL)) != GST_FLOW_OK)
	{
		GST_ERROR_OBJECT(imx_vpu_dec_buffer_pool, "could not allocate reserved buffer: %s", gst_flow_get_name(flow_ret));
		return NULL;
	}

	imx_vpu_dec_buffer_pool->reserved_buffers = g_slist_prepend(imx_vpu_dec_buffer_pool->reserved_buffers, buffer);

	/* Make sure the reserved buffer is marked as pooled and locked, otherwise
	 * it won't be passed to the release() function once its refcount reaches 0. */
	gst_buffer_foreach_meta(buffer, mark_meta_pooled, imx_vpu_dec_buffer_pool);

	/* Clear the TAG_MEMORY flag, since we are now done setting of the memory.
	 * Otherwise, the rest of the dataflow will think that this is an altered
	 * buffer, and may handle it improperly. */
	GST_BUFFER_FLAG_UNSET(buffer, GST_BUFFER_FLAG_TAG_MEMORY);
	/* Mark this as a reserved buffer so that release() does the right thing. */
	GST_BUFFER_FLAG_SET(buffer, GST_BUFFER_FLAG_IMX_VPU_RESERVED_FRAMEBUFFER);

	GST_LOG_OBJECT(imx_vpu_dec_buffer_pool, "allocated gstbuffer %p as a reserved gstbuffer for a framebuffer %p", (gpointer)buffer, (gpointer)gst_imx_get_dma_buffer_from_buffer(buffer));

	return buffer;
}


void gst_imx_vpu_dec_buffer_pool_select_reserved_buffer(GstImxVpuDecBufferPool *imx_vpu_dec_buffer_pool, GstBuffer *buffer)
{
	g_assert((buffer == NULL) || GST_BUFFER_FLAG_IS_SET(buffer, GST_BUFFER_FLAG_IMX_VPU_RESERVED_FRAMEBUFFER));

	g_mutex_lock(&(imx_vpu_dec_buffer_pool->selected_buffer_mutex));
	imx_vpu_dec_buffer_pool->selected_reserved_buffer = buffer;
	g_mutex_unlock(&(imx_vpu_dec_buffer_pool->selected_buffer_mutex));

	GST_LOG_OBJECT(imx_vpu_dec_buffer_pool, "selected reserved gstbuffer %p", (gpointer)buffer);
}
