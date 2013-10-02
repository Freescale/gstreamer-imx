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


#include <string.h>
#include "phys_mem_buffer_pool.h"
#include "phys_mem_meta.h"
#include "phys_mem_allocator.h"


GST_DEBUG_CATEGORY (imx_phys_mem_bufferpool_debug);
#define GST_CAT_DEFAULT imx_phys_mem_bufferpool_debug


static const gchar ** gst_imx_phys_mem_buffer_pool_get_options(GstBufferPool *pool);
static gboolean gst_imx_phys_mem_buffer_pool_set_config(GstBufferPool *pool, GstStructure *config);
static GstFlowReturn gst_imx_phys_mem_buffer_pool_alloc_buffer(GstBufferPool *pool, GstBuffer **buffer, GstBufferPoolAcquireParams *params);
static void gst_imx_phys_mem_buffer_pool_finalize(GObject *object);


G_DEFINE_TYPE(GstImxPhysMemBufferPool, gst_imx_phys_mem_buffer_pool, GST_TYPE_BUFFER_POOL)




static const gchar ** gst_imx_phys_mem_buffer_pool_get_options(G_GNUC_UNUSED GstBufferPool *pool)
{
	static const gchar *options[] =
	{
		GST_BUFFER_POOL_OPTION_VIDEO_META,
		GST_BUFFER_POOL_OPTION_IMX_PHYS_MEM,
		NULL
	};

	return options;
}


static gboolean gst_imx_phys_mem_buffer_pool_set_config(GstBufferPool *pool, GstStructure *config)
{
	GstImxPhysMemBufferPool *imx_phys_mem_pool;
	GstVideoInfo info;
	GstVideoAlignment align;
	GstCaps *caps;
	gsize size;
	GstAllocator *allocator;

	{
		allocator = NULL;

		gst_buffer_pool_config_get_allocator(config, &allocator, NULL);

		if (allocator == NULL)
		{
			GST_ERROR_OBJECT(pool, "pool configuration has NULL allocator set");
			return FALSE;
		}

		if (!GST_IS_IMX_PHYS_MEM_ALLOCATOR(allocator))
		{
			GST_ERROR_OBJECT(pool, "pool configuration does not contain a physical memory allocator");
			return FALSE;
		}
	}

	imx_phys_mem_pool = GST_IMX_PHYS_MEM_BUFFER_POOL(pool);

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
		GST_ERROR_OBJECT(pool, "caps cannot be parsed for video info");
		return FALSE;
	}

	imx_phys_mem_pool->video_info = info;
	GST_VIDEO_INFO_SIZE(&(imx_phys_mem_pool->video_info)) = size;

	gst_video_alignment_reset(&align);
	align.padding_right = (8 - (GST_VIDEO_INFO_WIDTH(&(imx_phys_mem_pool->video_info)) & 7)) & 7;
	align.padding_bottom = (8 - (GST_VIDEO_INFO_HEIGHT(&(imx_phys_mem_pool->video_info)) & 7)) & 7;
	gst_video_info_align(&(imx_phys_mem_pool->video_info), &align);

	imx_phys_mem_pool->add_video_meta = gst_buffer_pool_config_has_option(config, GST_BUFFER_POOL_OPTION_VIDEO_META);

	imx_phys_mem_pool->allocator = allocator;

	return GST_BUFFER_POOL_CLASS(gst_imx_phys_mem_buffer_pool_parent_class)->set_config(pool, config);
}


static GstFlowReturn gst_imx_phys_mem_buffer_pool_alloc_buffer(GstBufferPool *pool, GstBuffer **buffer, G_GNUC_UNUSED GstBufferPoolAcquireParams *params)
{
	GstImxPhysMemBufferPool *imx_phys_mem_pool;
	GstBuffer *buf;
	GstMemory *mem;
	GstVideoInfo *info;
	GstAllocationParams alloc_params;

	imx_phys_mem_pool = GST_IMX_PHYS_MEM_BUFFER_POOL(pool);

	memset(&alloc_params, 0, sizeof(GstAllocationParams));
	alloc_params.flags = imx_phys_mem_pool->read_only ? GST_MEMORY_FLAG_READONLY : 0;
	alloc_params.align = 0;

	info = &imx_phys_mem_pool->video_info;

	buf = gst_buffer_new();
	if (buf == NULL)
	{
		GST_ERROR_OBJECT(pool, "could not create new buffer");
		return GST_FLOW_ERROR;
	}

	mem = gst_allocator_alloc(imx_phys_mem_pool->allocator, info->size, &alloc_params);
	if (mem == NULL)
	{
		gst_buffer_unref(buf);
		GST_ERROR_OBJECT(pool, "could not allocate %u byte for new buffer", info->size);
		return GST_FLOW_ERROR;
	}

	gst_buffer_append_memory(buf, mem);

	if (imx_phys_mem_pool->add_video_meta)
	{
		GstVideoCropMeta *video_crop_meta;

		gst_buffer_add_video_meta_full(
			buf,
			GST_VIDEO_FRAME_FLAG_NONE,
			GST_VIDEO_INFO_FORMAT(info),
			GST_VIDEO_INFO_WIDTH(info), GST_VIDEO_INFO_HEIGHT(info),
			GST_VIDEO_INFO_N_PLANES(info),
			info->offset,
			info->stride
		);
		video_crop_meta = gst_buffer_add_video_crop_meta(buf);
		video_crop_meta->x = 0;
		video_crop_meta->y = 0;
		video_crop_meta->width = GST_VIDEO_INFO_WIDTH(info);
		video_crop_meta->height = GST_VIDEO_INFO_HEIGHT(info);
	}

	{
		GstImxPhysMemory *imx_phys_mem_mem = (GstImxPhysMemory *)mem;
		GstImxPhysMemMeta *phys_mem_meta = (GstImxPhysMemMeta *)GST_IMX_PHYS_MEM_META_ADD(buf);

		phys_mem_meta->phys_addr = imx_phys_mem_mem->phys_addr;

		phys_mem_meta->x_padding = (8 - (GST_VIDEO_INFO_WIDTH(&(imx_phys_mem_pool->video_info)) & 7)) & 7;
		phys_mem_meta->y_padding = (8 - (GST_VIDEO_INFO_HEIGHT(&(imx_phys_mem_pool->video_info)) & 7)) & 7;

		phys_mem_meta->padding = imx_phys_mem_pool->video_info.stride[0] * phys_mem_meta->y_padding;
	}

	*buffer = buf;

	return GST_FLOW_OK;
}


static void gst_imx_phys_mem_buffer_pool_finalize(GObject *object)
{
	GstImxPhysMemBufferPool *imx_phys_mem_pool = GST_IMX_PHYS_MEM_BUFFER_POOL(object);

	GST_DEBUG_OBJECT(object, "shutting down physical memory buffer pool");
	G_OBJECT_CLASS (gst_imx_phys_mem_buffer_pool_parent_class)->finalize(object);

	/* unref'ing AFTER calling the parent class' finalize function, since the parent
	 * class will shut down the allocated memory blocks, for which the allocator must
	 * exist */
	gst_object_unref(imx_phys_mem_pool->allocator);
}


static void gst_imx_phys_mem_buffer_pool_class_init(GstImxPhysMemBufferPoolClass *klass)
{
	GObjectClass *object_class = G_OBJECT_CLASS(klass);
	GstBufferPoolClass *parent_class = GST_BUFFER_POOL_CLASS(klass);

	GST_DEBUG_CATEGORY_INIT(imx_phys_mem_bufferpool_debug, "imxphysmembufferpool", 0, "Physical memory buffer pool");

	object_class->finalize     = GST_DEBUG_FUNCPTR(gst_imx_phys_mem_buffer_pool_finalize);
	parent_class->get_options  = GST_DEBUG_FUNCPTR(gst_imx_phys_mem_buffer_pool_get_options);
	parent_class->set_config   = GST_DEBUG_FUNCPTR(gst_imx_phys_mem_buffer_pool_set_config);
	parent_class->alloc_buffer = GST_DEBUG_FUNCPTR(gst_imx_phys_mem_buffer_pool_alloc_buffer);
}


static void gst_imx_phys_mem_buffer_pool_init(GstImxPhysMemBufferPool *pool)
{
	pool->add_video_meta = FALSE;
	GST_DEBUG_OBJECT(pool, "initializing physical memory buffer pool");
}


GstBufferPool *gst_imx_phys_mem_buffer_pool_new(gboolean read_only)
{
	GstImxPhysMemBufferPool *imx_phys_mem_pool;

	imx_phys_mem_pool = g_object_new(gst_imx_phys_mem_buffer_pool_get_type(), NULL);
	imx_phys_mem_pool->read_only = read_only;

	return GST_BUFFER_POOL_CAST(imx_phys_mem_pool);
}





