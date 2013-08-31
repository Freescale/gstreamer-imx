/* GStreamer video sink using the Freescale IPU
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


#include "sink.h"

#include <config.h>

#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <sys/types.h>
#include <linux/mxcfb.h>
#include <linux/ipu.h>

#include <gst/gst.h>
#include <gst/video/video.h>
#include <gst/video/gstvideometa.h>

#include "../../common/phys_mem_meta.h"
#include "../allocator.h"
#include "../buffer_pool.h"




GST_DEBUG_CATEGORY_STATIC(ipusink_debug);
#define GST_CAT_DEFAULT ipusink_debug


/* TODO: Memory-mapped writes into physically contiguous memory blocks are quite slow. This is
 * probably caused by the mapping type: if for example it is not mapped with write combining
 * enabled, random access to the memory will cause lots of wasteful cycles, explaining the
 * slowdown. Until this can be verified, the buffer pool is disabled; upstream does not get a
 * proposal for its allocation, and buffer contents end up copied over to a local physical
 * memory block by using memcpy(). Currently, doing that is ~3 times faster than letting
 * upstream write directly into physical memory blocks allocated by the proposed buffer pool.
 */
#if 0
#define USE_IPU_BUFFER_POOL
#endif




static GstStaticPadTemplate static_sink_template = GST_STATIC_PAD_TEMPLATE(
	"sink",
	GST_PAD_SINK,
	GST_PAD_ALWAYS,
	GST_STATIC_CAPS(
		"video/x-raw,"
		"format = (string) I420, "
		"width = (int) [ 16, 2048 ], "
		"height = (int) [ 16, 2048 ], "
		"framerate = (fraction) [ 0, MAX ]"
	)
);


struct _GstFslIpuSinkPrivate
{
	int ipu_fd, framebuffer_fd;

	struct fb_var_screeninfo fb_var;
	struct fb_fix_screeninfo fb_fix;

	struct ipu_task task;

	/* only used if upstream isn't sending in buffers with physical memory */
	gpointer display_mem_block;
	gsize display_mem_block_size;
};


G_DEFINE_TYPE(GstFslIpuSink, gst_fsl_ipu_sink, GST_TYPE_VIDEO_SINK)


static gboolean gst_fsl_ipu_sink_set_caps(GstBaseSink *sink, GstCaps *caps);
static gboolean gst_fsl_ipu_propose_allocation(GstBaseSink *sink, GstQuery *query);
static GstFlowReturn gst_fsl_ipu_sink_show_frame(GstVideoSink *video_sink, GstBuffer *buf);
static void gst_fsl_ipu_sink_finalize(GObject *object);
static guint32 gst_fsl_ipu_sink_get_v4l_format(GstFslIpuSink *ipu_sink, struct fb_var_screeninfo *fb_var, struct fb_fix_screeninfo *fb_fix);




/* required function declared by G_DEFINE_TYPE */

void gst_fsl_ipu_sink_class_init(GstFslIpuSinkClass *klass)
{
	GObjectClass *object_class;
	GstBaseSinkClass *base_class;
	GstVideoSinkClass *parent_class;
	GstElementClass *element_class;

	GST_DEBUG_CATEGORY_INIT(ipusink_debug, "ipusink", 0, "Freescale IPU video sink");

	object_class = G_OBJECT_CLASS(klass);
	base_class = GST_BASE_SINK_CLASS(klass);
	parent_class = GST_VIDEO_SINK_CLASS(klass);
	element_class = GST_ELEMENT_CLASS(klass);

	gst_element_class_set_static_metadata(
		element_class,
		"Freescale IPU video sink",
		"Sink/Video",
		"Video output using the Freescale IPU",
		"Carlos Rafael Giani <dv@pseudoterminal.org>"
	);

	gst_element_class_add_pad_template(element_class, gst_static_pad_template_get(&static_sink_template));
	base_class->set_caps = GST_DEBUG_FUNCPTR(gst_fsl_ipu_sink_set_caps);
	base_class->propose_allocation = GST_DEBUG_FUNCPTR(gst_fsl_ipu_propose_allocation);
	parent_class->show_frame = GST_DEBUG_FUNCPTR(gst_fsl_ipu_sink_show_frame);
	object_class->finalize = GST_DEBUG_FUNCPTR(gst_fsl_ipu_sink_finalize);
}


void gst_fsl_ipu_sink_init(GstFslIpuSink *ipu_sink)
{
	ipu_sink->priv = g_slice_alloc(sizeof(GstFslIpuSinkPrivate));

	ipu_sink->priv->framebuffer_fd = -1;
	ipu_sink->priv->ipu_fd = -1;

	ipu_sink->priv->display_mem_block = NULL;
	ipu_sink->priv->display_mem_block_size = 0;

	ipu_sink->priv->ipu_fd = open("/dev/mxc_ipu", O_RDWR, 0);
	if (ipu_sink->priv->ipu_fd < 0)
	{
		GST_ELEMENT_ERROR(ipu_sink, RESOURCE, OPEN_READ_WRITE, ("could not open /dev/mxc_ipu: %s", strerror(errno)), (NULL));
		return;
	}

	ipu_sink->priv->framebuffer_fd = open("/dev/fb0", O_RDWR, 0);
	if (ipu_sink->priv->ipu_fd < 0)
	{
		GST_ELEMENT_ERROR(ipu_sink, RESOURCE, OPEN_READ_WRITE, ("could not open /dev/fb0: %s", strerror(errno)), (NULL));
		return;
	}

	if (ioctl(ipu_sink->priv->framebuffer_fd, FBIOBLANK, FB_BLANK_UNBLANK) == -1)
	{
		GST_ELEMENT_ERROR(ipu_sink, RESOURCE, OPEN_READ_WRITE, ("could not open unblank framebuffer: %s", strerror(errno)), (NULL));
		return;
	}

	if (ioctl(ipu_sink->priv->framebuffer_fd, FBIOGET_FSCREENINFO, &(ipu_sink->priv->fb_fix)) == -1)
	{
		GST_ELEMENT_ERROR(ipu_sink, RESOURCE, OPEN_READ_WRITE, ("could not open get fixed screen info: %s", strerror(errno)), (NULL));
		return;
	}

	if (ioctl(ipu_sink->priv->framebuffer_fd, FBIOGET_VSCREENINFO, &(ipu_sink->priv->fb_var)) == -1)
	{
		GST_ELEMENT_ERROR(ipu_sink, RESOURCE, OPEN_READ_WRITE, ("could not open get variable screen info: %s", strerror(errno)), (NULL));
		return;
	}

	memset(&(ipu_sink->priv->task), 0, sizeof(struct ipu_task));

	ipu_sink->priv->task.input.format = IPU_PIX_FMT_YUV420P;

	ipu_sink->priv->task.output.format = gst_fsl_ipu_sink_get_v4l_format(ipu_sink, &(ipu_sink->priv->fb_var), &(ipu_sink->priv->fb_fix));
	ipu_sink->priv->task.output.paddr = (dma_addr_t)(ipu_sink->priv->fb_fix.smem_start);
	ipu_sink->priv->task.output.width = ipu_sink->priv->fb_var.xres;
	ipu_sink->priv->task.output.height = ipu_sink->priv->fb_var.yres;
	ipu_sink->priv->task.output.rotate = 0;
#if 0
	ipu_sink->priv->task.output.crop.pos.x = 10;
	ipu_sink->priv->task.output.crop.pos.y = 10;
	ipu_sink->priv->task.output.crop.w = 400;
	ipu_sink->priv->task.output.crop.h = 300;
#endif

	GST_INFO_OBJECT(ipu_sink, "initialized IPU sink with output screen resolution %d x %d and start phys address %p", ipu_sink->priv->task.output.width, ipu_sink->priv->task.output.height, ipu_sink->priv->task.output.paddr);

	if (ipu_sink->priv->task.output.format == 0)
	{
		GST_ELEMENT_ERROR(ipu_sink, RESOURCE, OPEN_READ_WRITE, ("framebuffer uses unknown or unsupported pixel format"), (NULL));
	}

	ipu_sink->parent.width = ipu_sink->priv->task.output.width;
	ipu_sink->parent.height = ipu_sink->priv->task.output.height;
}


static gboolean gst_fsl_ipu_sink_set_caps(GstBaseSink *sink, GstCaps *caps)
{
	GstFslIpuSink *ipu_sink;
	GstStructure *structure;
	GstVideoInfo video_info;
	GstBufferPool *new_pool, *old_pool;

	ipu_sink = GST_FSL_IPU_SINK(sink);

	gst_video_info_init(&video_info);
	if (!gst_video_info_from_caps(&video_info, caps))
		return FALSE;

	if (GST_VIDEO_INFO_INTERLACE_MODE(&video_info) == GST_VIDEO_INTERLACE_MODE_INTERLEAVED)
	{
		ipu_sink->priv->task.input.deinterlace.enable = 1;
		ipu_sink->priv->task.input.deinterlace.motion = HIGH_MOTION;
	}
	else
	{
		ipu_sink->priv->task.input.deinterlace.enable = 0;
		ipu_sink->priv->task.input.deinterlace.motion = MED_MOTION;
	}

	if (ipu_sink->priv->display_mem_block != NULL)
	{
		gst_fsl_ipu_free_phys_mem(ipu_sink->priv->ipu_fd, ipu_sink->priv->display_mem_block);
		ipu_sink->priv->display_mem_block = 0;
	}

	new_pool = gst_fsl_ipu_buffer_pool_new(ipu_sink->priv->ipu_fd, FALSE);
	structure = gst_buffer_pool_get_config(new_pool);
	gst_buffer_pool_config_set_params(structure, caps, video_info.size, 2, 0);
	if (!gst_buffer_pool_set_config(new_pool, structure))
	{
		GST_ERROR_OBJECT(ipu_sink, "failed to set pool configuration");
		return FALSE;
	}

	old_pool = ipu_sink->pool;
	/* we don't activate the pool; this will be done by downstream after it
	 * has configured the pool. If downstream does not use our pool, it stays unused. */
	ipu_sink->pool = new_pool;

	/* unref the old sink */
	if (old_pool)
	{
		/* we don't deactivate, some elements might still be using it, it will
		 * be deactivated when the last ref is gone */
		gst_object_unref(old_pool);
	}

	return TRUE;
}


#ifdef USE_IPU_BUFFER_POOL

static gboolean gst_fsl_ipu_propose_allocation(GstBaseSink *sink, GstQuery *query)
{
	GstFslIpuSink *ipu_sink;
	GstBufferPool *pool;
	GstStructure *config;
	GstCaps *caps;
	guint size;
	gboolean need_pool;

	ipu_sink = GST_FSL_IPU_SINK(sink);

	gst_query_parse_allocation(query, &caps, &need_pool);

	if (caps == NULL)
	{
		GST_DEBUG_OBJECT(ipu_sink, "no caps specified");
		return FALSE;
	}

	if ((pool = ipu_sink->pool) != NULL)
		gst_object_ref(pool);

	if (pool != NULL)
	{
		GstCaps *pcaps;

		/* we had a pool, check caps */
		GST_DEBUG_OBJECT(ipu_sink, "check existing pool caps");
		config = gst_buffer_pool_get_config(pool);
		gst_buffer_pool_config_get_params(config, &pcaps, &size, NULL, NULL);

		if (!gst_caps_is_equal(caps, pcaps))
		{
			GST_DEBUG_OBJECT(ipu_sink, "pool has different caps");
			/* different caps, we can't use this pool */
			gst_object_unref(pool);
			pool = NULL;
		}
		gst_structure_free(config);
	}

	if ((pool == NULL) && need_pool)
	{
		GstVideoInfo info;

		if (!gst_video_info_from_caps(&info, caps))
		{
			GST_DEBUG_OBJECT(ipu_sink, "invalid caps specified");
			return FALSE;
		}

		pool = gst_fsl_ipu_buffer_pool_new(ipu_sink->priv->ipu_fd, FALSE);

		/* the normal size of a frame */
		size = info.size;

		config = gst_buffer_pool_get_config(pool);
		gst_buffer_pool_config_set_params(config, caps, size, 0, 0);
		if (!gst_buffer_pool_set_config(pool, config))
		{
			GST_DEBUG_OBJECT(ipu_sink, "failed setting config");
			gst_object_unref(pool);
			return FALSE;
		}
	}

	if (pool)
	{
		gst_query_add_allocation_pool(query, pool, size, 0, 0);
		gst_object_unref(pool);
	}

	/* we also support various metadata */
	gst_query_add_allocation_meta(query, GST_VIDEO_META_API_TYPE, NULL);
	gst_query_add_allocation_meta(query, GST_VIDEO_CROP_META_API_TYPE, NULL);
	gst_query_add_allocation_meta(query, gst_fsl_phys_mem_meta_api_get_type(), NULL);

	return TRUE;
}

#else

static gboolean gst_fsl_ipu_propose_allocation(GstBaseSink *sink, GstQuery *query)
{
	GstCaps *caps;
	GstVideoInfo info;
	GstBufferPool *pool;
	guint size;

	gst_query_parse_allocation(query, &caps, NULL);

	if (caps == NULL)
	{
		GST_DEBUG_OBJECT(sink, "no caps specified");
		return FALSE;
	}

	if (!gst_video_info_from_caps(&info, caps))
		return FALSE;

	size = GST_VIDEO_INFO_SIZE(&info);

	if (gst_query_get_n_allocation_pools(query) == 0)
	{
		GstStructure *structure;
		GstAllocationParams params;
		GstAllocator *allocator = NULL;

		memset(&params, 0, sizeof(params));
		params.flags = 0;
		params.align = 15;
		params.prefix = 0;
		params.padding = 0;

		if (gst_query_get_n_allocation_params(query) > 0)
			gst_query_parse_nth_allocation_param(query, 0, &allocator, &params);
		else
			gst_query_add_allocation_param(query, allocator, &params);

		pool = gst_video_buffer_pool_new();

		structure = gst_buffer_pool_get_config(pool);
		gst_buffer_pool_config_set_params(structure, caps, size, 0, 0);
		gst_buffer_pool_config_set_allocator(structure, allocator, &params);

		if (allocator)
			gst_object_unref(allocator);

		if (!gst_buffer_pool_set_config(pool, structure))
		{
			GST_ERROR_OBJECT(sink, "failed to set config");
			gst_object_unref(pool);
			return FALSE;
		}

		gst_query_add_allocation_pool(query, pool, size, 0, 0);
		gst_object_unref(pool);
		gst_query_add_allocation_meta(query, GST_VIDEO_META_API_TYPE, NULL);
	}

	return TRUE;
}

#endif


static GstFlowReturn gst_fsl_ipu_sink_show_frame(GstVideoSink *video_sink, GstBuffer *buf)
{
	GstFslIpuSink *ipu_sink;
	GstVideoMeta *video_meta;
	GstVideoCropMeta *video_crop_meta;
	GstFslPhysMemMeta *phys_mem_meta;
	guint num_extra_rows;
	guint video_width, video_height;

	ipu_sink = GST_FSL_IPU_SINK(video_sink);

	video_meta = gst_buffer_get_video_meta(buf);
	video_crop_meta = gst_buffer_get_video_crop_meta(buf);
	phys_mem_meta = GST_FSL_PHYS_MEM_META_GET(buf);

	video_width = video_meta->width;
	video_height = video_meta->height;

	if (video_crop_meta != NULL)
	{
		if ((video_crop_meta->x >= video_width) || (video_crop_meta->y > video_height))
			return GST_FLOW_OK;

		ipu_sink->priv->task.input.crop.pos.x = video_crop_meta->x;
		ipu_sink->priv->task.input.crop.pos.y = video_crop_meta->y;
		ipu_sink->priv->task.input.crop.w = MIN(video_crop_meta->width, video_width - video_crop_meta->x);
		ipu_sink->priv->task.input.crop.h = MIN(video_crop_meta->height, video_height - video_crop_meta->y);
	}
	else
	{
		ipu_sink->priv->task.input.crop.pos.x = 0;
		ipu_sink->priv->task.input.crop.pos.y = 0;
		ipu_sink->priv->task.input.crop.w = video_width;
		ipu_sink->priv->task.input.crop.h = video_height;
	}

	if (phys_mem_meta != NULL)
	{
		GST_DEBUG_OBJECT(ipu_sink, "using data from the incoming buffer's physical memory address %p to display mem block", phys_mem_meta->phys_addr);

		num_extra_rows = phys_mem_meta->padding / video_meta->stride[0];
		ipu_sink->priv->task.input.paddr = (dma_addr_t)(phys_mem_meta->phys_addr);
	}
	else
	{
		GstMapInfo in_map_info;
		void *dispmem;
		gsize dispmem_size;

		dispmem_size = gst_buffer_get_size(buf);

		if ((ipu_sink->priv->display_mem_block == NULL) || (ipu_sink->priv->display_mem_block_size != dispmem_size))
		{
			ipu_sink->priv->display_mem_block_size = dispmem_size;
			if (ipu_sink->priv->display_mem_block != NULL)
				gst_fsl_ipu_free_phys_mem(ipu_sink->priv->ipu_fd, ipu_sink->priv->display_mem_block);
			ipu_sink->priv->display_mem_block = gst_fsl_ipu_alloc_phys_mem(ipu_sink->priv->ipu_fd, dispmem_size);
			if (ipu_sink->priv->display_mem_block == NULL)
				return GST_FLOW_ERROR;
		}

		gst_buffer_map(buf, &in_map_info, GST_MAP_READ);
		dispmem = mmap(0, dispmem_size, PROT_READ | PROT_WRITE, MAP_SHARED, ipu_sink->priv->ipu_fd, (dma_addr_t)(ipu_sink->priv->display_mem_block));

		GST_DEBUG_OBJECT(ipu_sink, "copying %u bytes from incoming buffer to display mem block", dispmem_size);
		memcpy(dispmem, in_map_info.data, dispmem_size);

		munmap(dispmem, dispmem_size);
		gst_buffer_unmap(buf, &in_map_info);

		num_extra_rows = 0;
		ipu_sink->priv->task.input.paddr = (dma_addr_t)(ipu_sink->priv->display_mem_block);
	}

	ipu_sink->priv->task.input.width = video_meta->stride[0];
	ipu_sink->priv->task.input.height = video_height + num_extra_rows;

	GST_DEBUG_OBJECT(
		ipu_sink,
		"input size: %d x %d  (actually: %d x %d  X padding: %d  Y padding: %d)  phys addr: %p",
		ipu_sink->priv->task.input.crop.w, ipu_sink->priv->task.input.crop.h,
		ipu_sink->priv->task.input.width, ipu_sink->priv->task.input.height,
		ipu_sink->priv->task.input.width - ipu_sink->priv->task.input.crop.w, num_extra_rows,
		ipu_sink->priv->task.input.paddr
	);

	if (ioctl(ipu_sink->priv->ipu_fd, IPU_QUEUE_TASK, &(ipu_sink->priv->task)) == -1)
	{
		GST_ERROR_OBJECT(ipu_sink, "queuing IPU task failed: %s", strerror(errno));
		return GST_FLOW_ERROR;
	}

	return GST_FLOW_OK;
}


static void gst_fsl_ipu_sink_finalize(GObject *object)
{
	GstFslIpuSink *ipu_sink = GST_FSL_IPU_SINK(object);

	if (ipu_sink->priv != NULL)
	{
		if (ipu_sink->priv->display_mem_block != NULL)
			gst_fsl_ipu_free_phys_mem(ipu_sink->priv->ipu_fd, ipu_sink->priv->display_mem_block);
		if (ipu_sink->priv->framebuffer_fd >= 0)
			close(ipu_sink->priv->framebuffer_fd);
		if (ipu_sink->priv->ipu_fd >= 0)
			close(ipu_sink->priv->ipu_fd);
		g_slice_free1(sizeof(GstFslIpuSinkPrivate), ipu_sink->priv);
	}

	G_OBJECT_CLASS(gst_fsl_ipu_sink_parent_class)->finalize(object);
}


static guint32 gst_fsl_ipu_sink_get_v4l_format(GstFslIpuSink *ipu_sink, struct fb_var_screeninfo *fb_var, struct fb_fix_screeninfo *fb_fix)
{
	guint32 fmt = 0;
	guint rlen = fb_var->red.length, glen = fb_var->green.length, blen = fb_var->blue.length, alen = fb_var->transp.length;
	guint rofs = fb_var->red.offset, gofs = fb_var->green.offset, bofs = fb_var->blue.offset, aofs = fb_var->transp.offset;

	if (fb_fix->type != FB_TYPE_PACKED_PIXELS)
	{
		GST_DEBUG_OBJECT(ipu_sink, "unknown framebuffer type %d", fb_fix->type);
		return fmt;
	}

	switch (fb_var->bits_per_pixel)
	{
		case 15:
		{
			if ((rlen == 5) && (glen == 5) && (blen == 5))
				fmt = IPU_PIX_FMT_RGB555;
			break;
		}
		case 16:
		{
			if ((rlen == 5) && (glen == 6) && (blen == 5))
				fmt = IPU_PIX_FMT_RGB565;
			break;
		}
		case 24:
		{
			if ((rlen == 8) && (glen == 8) && (blen == 8))
			{
				if ((rofs == 0) && (gofs == 8) && (bofs == 16))
					fmt = IPU_PIX_FMT_RGB24;
				else if ((rofs == 16) && (gofs == 8) && (bofs == 0))
					fmt = IPU_PIX_FMT_BGR24;
				else if ((rofs == 16) && (gofs == 0) && (bofs == 8))
					fmt = IPU_PIX_FMT_GBR24;
			}
			break;
		}
		case 32:
		{
			if ((rlen == 8) && (glen == 8) && (blen == 8) && (alen == 8))
			{
				if ((rofs == 0) && (gofs == 8) && (bofs == 16) && (aofs == 24))
					fmt = IPU_PIX_FMT_RGBA32;
				else if ((rofs == 16) && (gofs == 8) && (bofs == 0) && (aofs == 24))
					fmt = IPU_PIX_FMT_BGRA32;
				else if ((rofs == 24) && (gofs == 16) && (bofs == 8) && (aofs == 0))
					fmt = IPU_PIX_FMT_ABGR32;
			}
			break;
		}
		default:
			break;
	}

	GST_DEBUG_OBJECT(
		ipu_sink,
		"framebuffer uses %u bpp (sizes: r %u g %u b %u  offsets: r %u g %u b %u) => format 0x%08x",
		fb_var->bits_per_pixel,
		rlen, glen, blen,
		rofs, gofs, bofs,
		fmt
	);

	return fmt;
}

