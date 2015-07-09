/*
 * Copyright (c) 2013-2014, Black Moth Technologies, Philip Craig <phil@blackmoth.com.au>
 * Copyright (c) 2014, Carlos Rafael Giani
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
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <linux/videodev2.h>
#include "../common/phys_mem_meta.h"
#include "v4l2_buffer_pool.h"

GST_DEBUG_CATEGORY_STATIC(imx_v4l2_buffer_pool_debug);
#define GST_CAT_DEFAULT imx_v4l2_buffer_pool_debug

GType gst_imx_v4l2_meta_api_get_type(void)
{
	static volatile GType type;
	static const gchar *tags[] = {
		GST_META_TAG_VIDEO_STR, GST_META_TAG_MEMORY_STR, NULL
	};

	if (g_once_init_enter(&type))
	{
		GType _type = gst_meta_api_type_register("GstImxV4l2MetaAPI", tags);
		g_once_init_leave(&type, _type);
	}
	return type;
}

const GstMetaInfo *gst_imx_v4l2_meta_get_info(void)
{
	static const GstMetaInfo *meta_info = NULL;

	if (g_once_init_enter(&meta_info))
	{
		const GstMetaInfo *meta = gst_meta_register(
				gst_imx_v4l2_meta_api_get_type(),
				"GstImxV4l2Meta",
				sizeof(GstImxV4l2Meta),
				(GstMetaInitFunction)NULL,
				(GstMetaFreeFunction)NULL,
				(GstMetaTransformFunction)NULL);
		g_once_init_leave(&meta_info, meta);
	}
	return meta_info;
}

G_DEFINE_TYPE(GstImxV4l2BufferPool, gst_imx_v4l2_buffer_pool, GST_TYPE_BUFFER_POOL)

static const gchar ** gst_imx_v4l2_buffer_pool_get_options(G_GNUC_UNUSED GstBufferPool *pool)
{
	static const gchar *options[] =
	{
		GST_BUFFER_POOL_OPTION_VIDEO_META,
		NULL
	};

	return options;
}

static gboolean gst_imx_v4l2_buffer_pool_set_config(GstBufferPool *bpool, GstStructure *config)
{
	GstImxV4l2BufferPool *pool = GST_IMX_V4L2_BUFFER_POOL(bpool);
	GstVideoInfo info;
	GstCaps *caps;
	gsize size;
	guint min, max;
	struct v4l2_requestbuffers req;

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


	GST_DEBUG_OBJECT(pool, "set_config: size %d, min %d, max %d",
			size, min, max);

	memset(&req, 0, sizeof(req));
	req.count = min;
	req.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
	req.memory = V4L2_MEMORY_MMAP;
	if (ioctl(GST_IMX_FD_OBJECT_GET_FD(pool->fd_obj_v4l), VIDIOC_REQBUFS, &req) < 0)
	{
		GST_ERROR_OBJECT(pool, "VIDIOC_REQBUFS failed: %s",
				g_strerror(errno));
		return FALSE;
	}

	if (req.count != min)
	{
		min = req.count;
		GST_WARNING_OBJECT(pool, "using %u buffers", min);
	}

	pool->num_buffers = min;
	pool->video_info = info;
	pool->add_videometa = gst_buffer_pool_config_has_option(config, GST_BUFFER_POOL_OPTION_VIDEO_META);

	gst_buffer_pool_config_set_params(config, caps, size, min, max);

	return GST_BUFFER_POOL_CLASS(gst_imx_v4l2_buffer_pool_parent_class)->set_config(bpool, config);
}

static GstFlowReturn gst_imx_v4l2_buffer_pool_alloc_buffer(GstBufferPool *bpool, GstBuffer **buffer, G_GNUC_UNUSED GstBufferPoolAcquireParams *params)
{
	GstImxV4l2BufferPool *pool = GST_IMX_V4L2_BUFFER_POOL(bpool);
	GstBuffer *buf;
	GstImxV4l2Meta *meta;
	GstImxPhysMemMeta *phys_mem_meta;
	GstVideoInfo *info;

	buf = gst_buffer_new();
	if (buf == NULL)
	{
		GST_ERROR_OBJECT(pool, "could not create new buffer");
		return GST_FLOW_ERROR;
	}

	GST_DEBUG_OBJECT(pool, "alloc %u %p", pool->num_allocated, (gpointer)buf);

	meta = GST_IMX_V4L2_META_ADD(buf);
	meta->vbuffer.index = pool->num_allocated;
	meta->vbuffer.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
	meta->vbuffer.memory = V4L2_MEMORY_MMAP;

	if (ioctl(GST_IMX_FD_OBJECT_GET_FD(pool->fd_obj_v4l), VIDIOC_QUERYBUF, &meta->vbuffer) < 0)
	{
		GST_ERROR_OBJECT(pool, "VIDIOC_QUERYBUF error: %s",
				g_strerror(errno));
		gst_buffer_unref(buf);
		return GST_FLOW_ERROR;
	}

	meta->mem = mmap(NULL, meta->vbuffer.length,
			PROT_READ | PROT_WRITE, MAP_SHARED, GST_IMX_FD_OBJECT_GET_FD(pool->fd_obj_v4l),
			meta->vbuffer.m.offset);
	g_assert(meta->mem);

	/* Need to query twice to get the physical address */
	if (ioctl(GST_IMX_FD_OBJECT_GET_FD(pool->fd_obj_v4l), VIDIOC_QUERYBUF, &meta->vbuffer) < 0)
	{
		GST_ERROR_OBJECT(pool, "VIDIOC_QUERYBUF for physical address error: %s",
				g_strerror(errno));
		gst_buffer_unref(buf);
		return GST_FLOW_ERROR;
	}

	phys_mem_meta = GST_IMX_PHYS_MEM_META_ADD(buf);
	phys_mem_meta->phys_addr = meta->vbuffer.m.offset;

	/* Safeguard to catch data loss if in any future i.MX version the types do not match */
	g_assert(meta->vbuffer.m.offset == (__u32)(phys_mem_meta->phys_addr));

	if (pool->add_videometa)
	{
		info = &pool->video_info;

		gst_buffer_add_video_meta_full(
				buf,
				GST_VIDEO_FRAME_FLAG_NONE,
				GST_VIDEO_INFO_FORMAT(info),
				GST_VIDEO_INFO_WIDTH(info),
				GST_VIDEO_INFO_HEIGHT(info),
				GST_VIDEO_INFO_N_PLANES(info),
				info->offset,
				info->stride
				);
	}

	pool->num_allocated++;

	*buffer = buf;

	return GST_FLOW_OK;
}

static void gst_imx_v4l2_buffer_pool_free_buffer(GstBufferPool *bpool, GstBuffer *buf)
{
	GstImxV4l2BufferPool *pool = GST_IMX_V4L2_BUFFER_POOL(bpool);
	GstImxV4l2Meta *meta;

	meta = GST_IMX_V4L2_META_GET(buf);
	g_assert(meta);

	GST_DEBUG_OBJECT(pool, "free %u %p", meta->vbuffer.index, (gpointer)buf);

	munmap(meta->mem, meta->vbuffer.length);
	pool->buffers[meta->vbuffer.index] = NULL;

	gst_buffer_unref(buf);
}

static GstFlowReturn gst_imx_v4l2_buffer_pool_acquire_buffer(GstBufferPool *bpool, GstBuffer **buffer, G_GNUC_UNUSED GstBufferPoolAcquireParams *params)
{
	GstImxV4l2BufferPool *pool = GST_IMX_V4L2_BUFFER_POOL(bpool);
	struct v4l2_buffer vbuffer;
	GstBuffer *buf;
	GstImxV4l2Meta *meta;

	if (GST_BUFFER_POOL_IS_FLUSHING(bpool))
		return GST_FLOW_FLUSHING;

	memset(&vbuffer, 0, sizeof(vbuffer));
	vbuffer.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
	vbuffer.memory = V4L2_MEMORY_MMAP;

	if (ioctl(GST_IMX_FD_OBJECT_GET_FD(pool->fd_obj_v4l), VIDIOC_DQBUF, &vbuffer) < 0)
	{
		GST_ERROR_OBJECT(pool, "VIDIOC_DQBUF failed: %s", g_strerror(errno));
		return GST_FLOW_ERROR;
	}
	buf = pool->buffers[vbuffer.index];
	GST_DEBUG_OBJECT(pool, "dqbuf %u %p", vbuffer.index, (gpointer)buf);
	pool->buffers[vbuffer.index] = NULL;

	g_assert(buf);

	meta = GST_IMX_V4L2_META_GET(buf);
	g_assert(meta);

	gst_buffer_remove_all_memory(buf);
	gst_buffer_append_memory(buf,
			gst_memory_new_wrapped(0,
				meta->mem, meta->vbuffer.length, 0,
				vbuffer.bytesused, NULL, NULL));

	GST_BUFFER_TIMESTAMP(buf) = GST_TIMEVAL_TO_TIME(vbuffer.timestamp);

	*buffer = buf;

	return GST_FLOW_OK;
}

static void gst_imx_v4l2_buffer_pool_release_buffer(GstBufferPool *bpool, GstBuffer *buf)
{
	GstImxV4l2BufferPool *pool = GST_IMX_V4L2_BUFFER_POOL(bpool);
	GstImxV4l2Meta *meta;

	meta = GST_IMX_V4L2_META_GET(buf);
	if (!meta)
	{
		GST_DEBUG_OBJECT(pool, "unref copied buffer %p", (gpointer)buf);
		gst_buffer_unref(buf);
		return;
	}

	GST_DEBUG_OBJECT(pool, "qbuf %u %p", meta->vbuffer.index, (gpointer)buf);

	if (ioctl(GST_IMX_FD_OBJECT_GET_FD(pool->fd_obj_v4l), VIDIOC_QBUF, &meta->vbuffer) < 0)
	{
		GST_ERROR("VIDIOC_QBUF error: %s",
				g_strerror(errno));
		return;
	}
	pool->buffers[meta->vbuffer.index] = buf;
}

static gboolean gst_imx_v4l2_buffer_pool_start(GstBufferPool *bpool)
{
	GstImxV4l2BufferPool *pool = GST_IMX_V4L2_BUFFER_POOL(bpool);
	enum v4l2_buf_type type;

	GST_DEBUG_OBJECT(pool, "start");

	pool->buffers = g_new0(GstBuffer *, pool->num_buffers);
	pool->num_allocated = 0;

	if (!GST_BUFFER_POOL_CLASS(gst_imx_v4l2_buffer_pool_parent_class)->start(bpool))
	{
		GST_ERROR_OBJECT(pool, "failed to allocate start buffers");
		return FALSE;
	}

	type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
	if (ioctl(GST_IMX_FD_OBJECT_GET_FD(pool->fd_obj_v4l), VIDIOC_STREAMON, &type) < 0)
	{
		GST_ERROR_OBJECT(pool, "VIDIOC_STREAMON error: %s",
				g_strerror(errno));
		return FALSE;
	}

	return TRUE;
}

static gboolean gst_imx_v4l2_buffer_pool_stop(GstBufferPool *bpool)
{
	GstImxV4l2BufferPool *pool = GST_IMX_V4L2_BUFFER_POOL(bpool);
	enum v4l2_buf_type type;
	guint i;
	gboolean ret = TRUE;

	GST_DEBUG_OBJECT(pool, "stop");

	type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
	if (ioctl(GST_IMX_FD_OBJECT_GET_FD(pool->fd_obj_v4l), VIDIOC_STREAMOFF, &type) < 0)
	{
		GST_ERROR_OBJECT(pool, "VIDIOC_STREAMOFF error: %s", g_strerror(errno));
		return FALSE;
	}

	for (i = 0; i < pool->num_buffers; i++)
	{
		if (pool->buffers[i])
		{
			GST_BUFFER_POOL_CLASS(gst_imx_v4l2_buffer_pool_parent_class)->release_buffer(bpool, pool->buffers[i]);
			pool->buffers[i] = NULL;
		}
	}
	g_free(pool->buffers);
	pool->buffers = NULL;

	ret = GST_BUFFER_POOL_CLASS(gst_imx_v4l2_buffer_pool_parent_class)->stop(bpool);

	return ret;
}

static void gst_imx_v4l2_buffer_pool_init(GstImxV4l2BufferPool *pool)
{
	GST_DEBUG_OBJECT(pool, "initializing V4L2 buffer pool");
	pool->fd_obj_v4l = NULL;
}

static void gst_imx_v4l2_buffer_pool_finalize(GObject *object)
{
	GstImxV4l2BufferPool *pool = GST_IMX_V4L2_BUFFER_POOL(object);

	GST_TRACE_OBJECT(pool, "shutting down buffer pool");

	g_free(pool->buffers);
	gst_imx_fd_object_unref(pool->fd_obj_v4l);

	G_OBJECT_CLASS(gst_imx_v4l2_buffer_pool_parent_class)->finalize(object);
}

static void gst_imx_v4l2_buffer_pool_class_init(GstImxV4l2BufferPoolClass *klass)
{
	GObjectClass *object_class;
	GstBufferPoolClass *parent_class;

	object_class = G_OBJECT_CLASS(klass);
	parent_class = GST_BUFFER_POOL_CLASS(klass);

	GST_DEBUG_CATEGORY_INIT(imx_v4l2_buffer_pool_debug, "imxv4l2bufferpool", 0, "Freescale i.MX V4L2 buffer pool");

	object_class->finalize = GST_DEBUG_FUNCPTR(gst_imx_v4l2_buffer_pool_finalize);
	parent_class->get_options = GST_DEBUG_FUNCPTR(gst_imx_v4l2_buffer_pool_get_options);
	parent_class->set_config = GST_DEBUG_FUNCPTR(gst_imx_v4l2_buffer_pool_set_config);
	parent_class->start = GST_DEBUG_FUNCPTR(gst_imx_v4l2_buffer_pool_start);
	parent_class->stop = GST_DEBUG_FUNCPTR(gst_imx_v4l2_buffer_pool_stop);
	parent_class->alloc_buffer = GST_DEBUG_FUNCPTR(gst_imx_v4l2_buffer_pool_alloc_buffer);
	parent_class->free_buffer = GST_DEBUG_FUNCPTR(gst_imx_v4l2_buffer_pool_free_buffer);
	parent_class->acquire_buffer = GST_DEBUG_FUNCPTR(gst_imx_v4l2_buffer_pool_acquire_buffer);
	parent_class->release_buffer = GST_DEBUG_FUNCPTR(gst_imx_v4l2_buffer_pool_release_buffer);
}

GstBufferPool *gst_imx_v4l2_buffer_pool_new(GstImxFDObject *fd_obj_v4l)
{
	GstImxV4l2BufferPool *pool;

	pool = g_object_new(gst_imx_v4l2_buffer_pool_get_type(), NULL);
	pool->fd_obj_v4l = gst_imx_fd_object_ref(fd_obj_v4l);

	return GST_BUFFER_POOL_CAST(pool);
}
