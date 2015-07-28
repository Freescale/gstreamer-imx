/* Common Freescale IPU blitter code for GStreamer
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
#include <sys/ioctl.h>
#include <errno.h>
#include <linux/ipu.h>
#include "blitter.h"
#include "allocator.h"
#include "device.h"
#include "../common/phys_mem_meta.h"


GST_DEBUG_CATEGORY_STATIC(imx_ipu_blitter_debug);
#define GST_CAT_DEFAULT imx_ipu_blitter_debug


G_DEFINE_TYPE(GstImxIpuBlitter, gst_imx_ipu_blitter, GST_TYPE_IMX_BLITTER)


/* Private structure storing IPU specific data */
struct _GstImxIpuBlitterPrivate
{
	struct ipu_task main_task, fill_task;
};


static guint const fill_frame_width = 8;
static guint const fill_frame_height = 8;
static GstVideoFormat const format = GST_VIDEO_FORMAT_RGBx;


static void gst_imx_ipu_blitter_finalize(GObject *object);

static gboolean gst_imx_ipu_blitter_set_input_video_info(GstImxBlitter *blitter, GstVideoInfo const *input_video_info);
static gboolean gst_imx_ipu_blitter_set_output_video_info(GstImxBlitter *blitter, GstVideoInfo const *output_video_info);

static gboolean gst_imx_ipu_blitter_set_input_region(GstImxBlitter *blitter, GstImxRegion const *input_region);
static gboolean gst_imx_ipu_blitter_set_output_canvas(GstImxBlitter *blitter, GstImxCanvas const *output_canvas);
static gboolean gst_imx_ipu_blitter_set_num_output_pages(GstImxBlitter *blitter, guint num_output_pages);

static gboolean gst_imx_ipu_blitter_set_input_frame(GstImxBlitter *blitter, GstBuffer *input_frame);
static gboolean gst_imx_ipu_blitter_set_output_frame(GstImxBlitter *blitter, GstBuffer *output_frame);

static GstAllocator* gst_imx_ipu_blitter_get_phys_mem_allocator(GstImxBlitter *blitter);

static gboolean gst_imx_ipu_blitter_fill_region(GstImxBlitter *blitter, GstImxRegion const *region, guint32 color);
static gboolean gst_imx_ipu_blitter_blit(GstImxBlitter *blitter, guint8 alpha);

static void gst_imx_ipu_blitter_set_task_params(GstImxIpuBlitter *ipu_blitter, GstBuffer *video_frame, struct ipu_task *task, GstVideoInfo const *info, gboolean is_input);
static gboolean gst_imx_ipu_blitter_allocate_internal_fill_frame(GstImxIpuBlitter *ipu_blitter);
static void gst_imx_ipu_blitter_print_ipu_fourcc(u32 format, char buf[5]);
static guint32 gst_imx_ipu_blitter_get_v4l_format(GstVideoFormat format);
static int gst_imx_ipu_video_bpp(GstVideoFormat fmt);
static void gst_imx_ipu_blitter_set_output_rotation(GstImxIpuBlitter *ipu_blitter, GstImxCanvasInnerRotation rotation);




static void gst_imx_ipu_blitter_class_init(GstImxIpuBlitterClass *klass)
{
	GObjectClass *object_class;
	GstImxBlitterClass *base_class;

	object_class = G_OBJECT_CLASS(klass);
	base_class = GST_IMX_BLITTER_CLASS(klass);

	object_class->finalize             = GST_DEBUG_FUNCPTR(gst_imx_ipu_blitter_finalize);
	base_class->set_input_video_info   = GST_DEBUG_FUNCPTR(gst_imx_ipu_blitter_set_input_video_info);
	base_class->set_output_video_info  = GST_DEBUG_FUNCPTR(gst_imx_ipu_blitter_set_output_video_info);
	base_class->set_input_region       = GST_DEBUG_FUNCPTR(gst_imx_ipu_blitter_set_input_region);
	base_class->set_output_canvas      = GST_DEBUG_FUNCPTR(gst_imx_ipu_blitter_set_output_canvas);
	base_class->set_input_frame        = GST_DEBUG_FUNCPTR(gst_imx_ipu_blitter_set_input_frame);
	base_class->set_output_frame       = GST_DEBUG_FUNCPTR(gst_imx_ipu_blitter_set_output_frame);
	base_class->set_num_output_pages   = GST_DEBUG_FUNCPTR(gst_imx_ipu_blitter_set_num_output_pages);
	base_class->get_phys_mem_allocator = GST_DEBUG_FUNCPTR(gst_imx_ipu_blitter_get_phys_mem_allocator);
	base_class->fill_region            = GST_DEBUG_FUNCPTR(gst_imx_ipu_blitter_fill_region);
	base_class->blit                   = GST_DEBUG_FUNCPTR(gst_imx_ipu_blitter_blit);

	GST_DEBUG_CATEGORY_INIT(imx_ipu_blitter_debug, "imxipublitter", 0, "Freescale i.MX IPU blitter class");
}


static void gst_imx_ipu_blitter_init(GstImxIpuBlitter *ipu_blitter)
{
	if (!gst_imx_ipu_open())
	{
		GST_ELEMENT_ERROR(ipu_blitter, RESOURCE, OPEN_READ_WRITE, ("could not open IPU device"), (NULL));
		return;
	}

	gst_video_info_init(&(ipu_blitter->input_video_info));
	gst_video_info_init(&(ipu_blitter->output_video_info));
	ipu_blitter->allocator = NULL;
	ipu_blitter->input_frame = NULL;
	ipu_blitter->output_frame = NULL;
	ipu_blitter->use_entire_input_frame = TRUE;

	ipu_blitter->priv = g_slice_alloc(sizeof(GstImxIpuBlitterPrivate));
	memset(&(ipu_blitter->priv->main_task), 0, sizeof(struct ipu_task));

	ipu_blitter->visibility_mask = 0;
	ipu_blitter->fill_color = 0xFF000000;
	ipu_blitter->num_empty_regions = 0;

	ipu_blitter->clipped_outer_region_updated = FALSE;
	ipu_blitter->num_output_pages = 1;
	ipu_blitter->num_cleared_output_pages = 0;

	ipu_blitter->deinterlacing_enabled = GST_IMX_IPU_BLITTER_DEINTERLACE_DEFAULT;
}


GstImxIpuBlitter* gst_imx_ipu_blitter_new(void)
{
	GstAllocator *allocator;
	GstImxIpuBlitter *ipu_blitter;

	allocator = gst_imx_ipu_allocator_new();
	if (allocator == NULL)
		return NULL;

	ipu_blitter = (GstImxIpuBlitter *)g_object_new(gst_imx_ipu_blitter_get_type(), NULL);
	ipu_blitter->allocator = gst_object_ref_sink(allocator);

	if (!gst_imx_ipu_blitter_allocate_internal_fill_frame(ipu_blitter))
	{
		gst_object_unref(GST_OBJECT(ipu_blitter));
		return NULL;
	}

	return ipu_blitter;
}


void gst_imx_ipu_blitter_enable_deinterlacing(GstImxIpuBlitter *ipu_blitter, gboolean enable)
{
	ipu_blitter->deinterlacing_enabled = enable;
}


static void gst_imx_ipu_blitter_finalize(GObject *object)
{
	GstImxIpuBlitter *ipu_blitter = GST_IMX_IPU_BLITTER(object);

	if (ipu_blitter->input_frame != NULL)
		gst_buffer_unref(ipu_blitter->input_frame);
	if (ipu_blitter->output_frame != NULL)
		gst_buffer_unref(ipu_blitter->output_frame);
	if (ipu_blitter->fill_frame != NULL)
		gst_buffer_unref(ipu_blitter->fill_frame);

	if (ipu_blitter->allocator != NULL)
		gst_object_unref(ipu_blitter->allocator);

	if (ipu_blitter->priv != NULL)
	{
		gst_imx_ipu_close();
		g_slice_free1(sizeof(GstImxIpuBlitterPrivate), ipu_blitter->priv);
	}

	G_OBJECT_CLASS(gst_imx_ipu_blitter_parent_class)->finalize(object);
}


static gboolean gst_imx_ipu_blitter_set_input_video_info(GstImxBlitter *blitter, GstVideoInfo const *input_video_info)
{
	GstImxIpuBlitter *ipu_blitter = GST_IMX_IPU_BLITTER(blitter);
	ipu_blitter->input_video_info = *input_video_info;
	return TRUE;
}


static gboolean gst_imx_ipu_blitter_set_output_video_info(GstImxBlitter *blitter, GstVideoInfo const *output_video_info)
{
	GstImxIpuBlitter *ipu_blitter = GST_IMX_IPU_BLITTER(blitter);
	ipu_blitter->output_video_info = *output_video_info;
	return TRUE;
}


static gboolean gst_imx_ipu_blitter_set_input_region(GstImxBlitter *blitter, GstImxRegion const *input_region)
{
	GstImxIpuBlitter *ipu_blitter = GST_IMX_IPU_BLITTER(blitter);

	if (input_region != NULL)
	{
		ipu_blitter->priv->main_task.input.crop.pos.x = input_region->x1;
		ipu_blitter->priv->main_task.input.crop.pos.y = input_region->y1;
		ipu_blitter->priv->main_task.input.crop.w     = input_region->x2 - input_region->x1;
		ipu_blitter->priv->main_task.input.crop.h     = input_region->y2 - input_region->y1;

		ipu_blitter->use_entire_input_frame = FALSE;
	}
	else
		ipu_blitter->use_entire_input_frame = TRUE;

	return TRUE;
}


static gboolean gst_imx_ipu_blitter_set_output_canvas(GstImxBlitter *blitter, GstImxCanvas const *output_canvas)
{
	GstImxIpuBlitter *ipu_blitter = GST_IMX_IPU_BLITTER(blitter);
	guint i;

	ipu_blitter->priv->main_task.output.crop.pos.x = output_canvas->clipped_inner_region.x1;
	ipu_blitter->priv->main_task.output.crop.pos.y = output_canvas->clipped_inner_region.y1;
	ipu_blitter->priv->main_task.output.crop.w     = output_canvas->clipped_inner_region.x2 - output_canvas->clipped_inner_region.x1;
	ipu_blitter->priv->main_task.output.crop.h     = output_canvas->clipped_inner_region.y2 - output_canvas->clipped_inner_region.y1;

	ipu_blitter->clipped_outer_region = output_canvas->clipped_outer_region;
	ipu_blitter->clipped_outer_region_updated = TRUE;
	ipu_blitter->num_cleared_output_pages = 0;

	ipu_blitter->visibility_mask = output_canvas->visibility_mask;
	ipu_blitter->fill_color = output_canvas->fill_color;

	ipu_blitter->num_empty_regions = 0;
	for (i = 0; i < 4; ++i)
	{
		if ((ipu_blitter->visibility_mask & (1 << i)) == 0)
			continue;

		ipu_blitter->empty_regions[ipu_blitter->num_empty_regions] = output_canvas->empty_regions[i];
		ipu_blitter->num_empty_regions++;
	}

	gst_imx_ipu_blitter_set_output_rotation(ipu_blitter, output_canvas->inner_rotation);

	return TRUE;
}


static gboolean gst_imx_ipu_blitter_set_num_output_pages(GstImxBlitter *blitter, guint num_output_pages)
{
	GstImxIpuBlitter *ipu_blitter = GST_IMX_IPU_BLITTER(blitter);

	ipu_blitter->clipped_outer_region_updated = TRUE;
	ipu_blitter->num_cleared_output_pages = 0;
	ipu_blitter->num_output_pages = num_output_pages;

	return TRUE;
}


static gboolean gst_imx_ipu_blitter_set_input_frame(GstImxBlitter *blitter, GstBuffer *input_frame)
{
	GstImxIpuBlitter *ipu_blitter = GST_IMX_IPU_BLITTER(blitter);
	gst_buffer_replace(&(ipu_blitter->input_frame), input_frame);

	if (ipu_blitter->input_frame != NULL)
	{
		ipu_blitter->priv->main_task.input.deinterlace.enable = 0;

		if (ipu_blitter->deinterlacing_enabled)
		{
			switch (GST_VIDEO_INFO_INTERLACE_MODE(&(ipu_blitter->input_video_info)))
			{
				case GST_VIDEO_INTERLACE_MODE_INTERLEAVED:
					GST_LOG_OBJECT(ipu_blitter, "input stream uses interlacing -> deinterlacing enabled");
					ipu_blitter->priv->main_task.input.deinterlace.enable = 1;
					break;

				case GST_VIDEO_INTERLACE_MODE_MIXED:
				{
					if (GST_BUFFER_FLAG_IS_SET(input_frame, GST_VIDEO_BUFFER_FLAG_INTERLACED))
					{
						GST_LOG_OBJECT(ipu_blitter, "frame has deinterlacing flag");
						ipu_blitter->priv->main_task.input.deinterlace.enable = 1;
					}
					else
						GST_LOG_OBJECT(ipu_blitter, "frame has no deinterlacing flag");

					break;
				}

				case GST_VIDEO_INTERLACE_MODE_PROGRESSIVE:
					GST_LOG_OBJECT(ipu_blitter, "input stream is progressive -> no deinterlacing necessary");
					break;

				case GST_VIDEO_INTERLACE_MODE_FIELDS:
					GST_FIXME_OBJECT(ipu_blitter, "2-fields deinterlacing not supported (yet)");
					break;

				default:
					GST_LOG_OBJECT(ipu_blitter, "input stream uses unknown interlacing mode -> no deinterlacing performed");
			}
		}

		if (ipu_blitter->priv->main_task.input.deinterlace.enable)
		{
			if (GST_BUFFER_FLAG_IS_SET(input_frame, GST_VIDEO_BUFFER_FLAG_TFF))
			{
				GST_LOG_OBJECT(ipu_blitter, "interlaced with top field first");
				ipu_blitter->priv->main_task.input.deinterlace.field_fmt = IPU_DEINTERLACE_FIELD_TOP;
			}
			else
			{
				GST_LOG_OBJECT(ipu_blitter, "interlaced with bottom field first");
				ipu_blitter->priv->main_task.input.deinterlace.field_fmt = IPU_DEINTERLACE_FIELD_BOTTOM;
			}

			ipu_blitter->priv->main_task.input.deinterlace.motion = HIGH_MOTION;
		}
		else
			ipu_blitter->priv->main_task.input.deinterlace.motion = MED_MOTION;

		gst_imx_ipu_blitter_set_task_params(ipu_blitter, input_frame, &(ipu_blitter->priv->main_task), &(ipu_blitter->input_video_info), TRUE);

		if (ipu_blitter->use_entire_input_frame)
		{
			ipu_blitter->priv->main_task.input.crop.pos.x = 0;
			ipu_blitter->priv->main_task.input.crop.pos.y = 0;
			ipu_blitter->priv->main_task.input.crop.w     = GST_VIDEO_INFO_WIDTH(&(ipu_blitter->input_video_info));
			ipu_blitter->priv->main_task.input.crop.h     = GST_VIDEO_INFO_HEIGHT(&(ipu_blitter->input_video_info));
		}
	}

	return TRUE;
}


static gboolean gst_imx_ipu_blitter_set_output_frame(GstImxBlitter *blitter, GstBuffer *output_frame)
{
	GstImxIpuBlitter *ipu_blitter = GST_IMX_IPU_BLITTER(blitter);
	gst_buffer_replace(&(ipu_blitter->output_frame), output_frame);

	if (ipu_blitter->output_frame != NULL)
	{
		gst_imx_ipu_blitter_set_task_params(ipu_blitter, output_frame, &(ipu_blitter->priv->main_task), &(ipu_blitter->output_video_info), FALSE);

		ipu_blitter->priv->fill_task.output = ipu_blitter->priv->main_task.output;
	}

	return TRUE;
}


static GstAllocator* gst_imx_ipu_blitter_get_phys_mem_allocator(GstImxBlitter *blitter)
{
	return gst_object_ref(GST_IMX_IPU_BLITTER_CAST(blitter)->allocator);
}


static gboolean gst_imx_ipu_blitter_fill_region(GstImxBlitter *blitter, GstImxRegion const *region, guint32 color)
{
	GstImxIpuBlitter *ipu_blitter = GST_IMX_IPU_BLITTER(blitter);
	struct ipu_task *task = &(ipu_blitter->priv->fill_task);
	int ioctl_ret;

	task->output.rotate = IPU_ROTATE_NONE;
	task->output.crop.pos.x = region->x1;
	task->output.crop.pos.y = region->y1;
	task->output.crop.w = region->x2 - region->x1;
	task->output.crop.h = region->y2 - region->y1;

	GST_LOG_OBJECT(
		ipu_blitter,
		"fill op task input:  width:  %u  height: %u  format: 0x%x  crop: %u,%u %ux%u  phys addr %" GST_IMX_PHYS_ADDR_FORMAT "  deinterlace enable %u motion 0x%x",
		task->input.width, task->input.height,
		task->input.format,
		task->input.crop.pos.x, task->input.crop.pos.y, task->input.crop.w, task->input.crop.h,
		(gst_imx_phys_addr_t)(task->input.paddr),
		task->input.deinterlace.enable, task->input.deinterlace.motion
	);
	GST_LOG_OBJECT(
		ipu_blitter,
		"fill op task output:  width:  %u  height: %u  format: 0x%x  crop: %u,%u %ux%u  paddr %" GST_IMX_PHYS_ADDR_FORMAT "  rotate: %u",
		task->output.width, task->output.height,
		task->output.format,
		task->output.crop.pos.x, task->output.crop.pos.y, task->output.crop.w, task->output.crop.h,
		(gst_imx_phys_addr_t)(task->output.paddr),
		task->output.rotate
	);

	{
		GstMapInfo map_info;
		guint32 *pixels, *pixels_end;
		guint32 opaque_color = color | 0xFF000000;

		gst_buffer_map(ipu_blitter->fill_frame, &map_info, GST_MAP_WRITE);
		pixels = (guint32 *)(map_info.data);
		pixels_end = pixels + fill_frame_width * fill_frame_height;
		for (; pixels < pixels_end; ++pixels)
			*pixels = opaque_color;
		gst_buffer_unmap(ipu_blitter->fill_frame, &map_info);
	}

	if ((ioctl_ret = ioctl(gst_imx_ipu_get_fd(), IPU_QUEUE_TASK, task)) == -1)
	{
		GST_ERROR_OBJECT(ipu_blitter, "queuing IPU task failed: %s", strerror(errno));
		return FALSE;
	}

	return TRUE;
}


static gboolean gst_imx_ipu_blitter_blit(GstImxBlitter *blitter, guint8 alpha)
{
	gboolean ret = TRUE;
	GstImxIpuBlitter *ipu_blitter = GST_IMX_IPU_BLITTER(blitter);

	if (ipu_blitter->clipped_outer_region_updated)
	{
		GST_DEBUG_OBJECT(ipu_blitter, "cleared page %u of %u", ipu_blitter->num_cleared_output_pages, ipu_blitter->num_output_pages);

		if (!gst_imx_ipu_blitter_fill_region(blitter, &(ipu_blitter->clipped_outer_region), ipu_blitter->fill_color | 0xFF000000))
			return FALSE;

		ipu_blitter->num_cleared_output_pages++;
		if (ipu_blitter->num_cleared_output_pages >= ipu_blitter->num_output_pages)
			ipu_blitter->clipped_outer_region_updated = FALSE;
	}

	if (ret && (ipu_blitter->visibility_mask & GST_IMX_CANVAS_VISIBILITY_FLAG_REGION_INNER))
	{
		char fourcc[5];
		int ioctl_ret;

		gst_imx_ipu_blitter_print_ipu_fourcc(ipu_blitter->priv->main_task.input.format, fourcc);
		GST_LOG_OBJECT(
			ipu_blitter,
			"main_task input:  width:  %u  height: %u  format: 0x%x (%s)  crop: %u,%u %ux%u  phys addr %" GST_IMX_PHYS_ADDR_FORMAT "  deinterlace enable %u motion 0x%x",
			ipu_blitter->priv->main_task.input.width, ipu_blitter->priv->main_task.input.height,
			ipu_blitter->priv->main_task.input.format, fourcc,
			ipu_blitter->priv->main_task.input.crop.pos.x, ipu_blitter->priv->main_task.input.crop.pos.y, ipu_blitter->priv->main_task.input.crop.w, ipu_blitter->priv->main_task.input.crop.h,
			(gst_imx_phys_addr_t)(ipu_blitter->priv->main_task.input.paddr),
			ipu_blitter->priv->main_task.input.deinterlace.enable, ipu_blitter->priv->main_task.input.deinterlace.motion
		);
		gst_imx_ipu_blitter_print_ipu_fourcc(ipu_blitter->priv->main_task.output.format, fourcc);
		GST_LOG_OBJECT(
			ipu_blitter,
			"main_task output:  width:  %u  height: %u  format: 0x%x (%s)  crop: %u,%u %ux%u  paddr %" GST_IMX_PHYS_ADDR_FORMAT "  rotate: %u",
			ipu_blitter->priv->main_task.output.width, ipu_blitter->priv->main_task.output.height,
			ipu_blitter->priv->main_task.output.format, fourcc,
			ipu_blitter->priv->main_task.output.crop.pos.x, ipu_blitter->priv->main_task.output.crop.pos.y, ipu_blitter->priv->main_task.output.crop.w, ipu_blitter->priv->main_task.output.crop.h,
			(gst_imx_phys_addr_t)(ipu_blitter->priv->main_task.output.paddr),
			ipu_blitter->priv->main_task.output.rotate
		);

		if ((ioctl_ret = ioctl(gst_imx_ipu_get_fd(), IPU_QUEUE_TASK, &(ipu_blitter->priv->main_task))) == -1)
		{
			GST_ERROR_OBJECT(ipu_blitter, "queuing IPU task failed: %s", strerror(errno));
			ret = FALSE;
		}
	}

	return ret;
}


static void gst_imx_ipu_blitter_set_task_params(GstImxIpuBlitter *ipu_blitter, GstBuffer *video_frame, struct ipu_task *task, GstVideoInfo const *info, gboolean is_input)
{
	GstVideoMeta *video_meta;
	GstImxPhysMemMeta *phys_mem_meta;
	GstVideoFormat format;
	guint width, height, padded_width, padded_height;

	g_assert(video_frame != NULL);

	video_meta = gst_buffer_get_video_meta(video_frame);
	phys_mem_meta = GST_IMX_PHYS_MEM_META_GET(video_frame);

	g_assert((phys_mem_meta != NULL) && (phys_mem_meta->phys_addr != 0));

	format = (video_meta != NULL) ? video_meta->format : GST_VIDEO_INFO_FORMAT(info);
	width = (video_meta != NULL) ? video_meta->width : (guint)(GST_VIDEO_INFO_WIDTH(info));
	height = (video_meta != NULL) ? video_meta->height : (guint)(GST_VIDEO_INFO_HEIGHT(info));

	padded_width = width + phys_mem_meta->x_padding;
	padded_height = height + phys_mem_meta->y_padding;

	if (is_input)
	{
		task->input.width = padded_width;
		task->input.height = padded_height;
		task->input.format = gst_imx_ipu_blitter_get_v4l_format(format);
		task->input.paddr = (dma_addr_t)(phys_mem_meta->phys_addr);
	}
	else
	{
		task->output.width = padded_width;
		task->output.height = padded_height;
		task->output.format = gst_imx_ipu_blitter_get_v4l_format(format);
		task->output.paddr = (dma_addr_t)(phys_mem_meta->phys_addr);
	}
}


static gboolean gst_imx_ipu_blitter_allocate_internal_fill_frame(GstImxIpuBlitter *ipu_blitter)
{
	GstImxPhysMemory *phys_mem;

	/* Not using the dma bufferpool for this, since that bufferpool will
	 * be configured to input frame sizes. Plus, the pool wouldn't yield any benefits here. */
	ipu_blitter->fill_frame = gst_buffer_new_allocate(
		ipu_blitter->allocator,
		fill_frame_width * fill_frame_height * gst_imx_ipu_video_bpp(format),
		NULL
	);

	if (ipu_blitter->fill_frame == NULL)
	{
		GST_ERROR_OBJECT(ipu_blitter, "could not allocate internal fill frame");
		return FALSE;
	}

	phys_mem = (GstImxPhysMemory *)gst_buffer_peek_memory(ipu_blitter->fill_frame, 0);

	memset(&(ipu_blitter->priv->fill_task), 0, sizeof(struct ipu_task));
	ipu_blitter->priv->fill_task.input.crop.pos.x = 0;
	ipu_blitter->priv->fill_task.input.crop.pos.y = 0;
	ipu_blitter->priv->fill_task.input.crop.w = fill_frame_width;
	ipu_blitter->priv->fill_task.input.crop.h = fill_frame_height;
	ipu_blitter->priv->fill_task.input.width = fill_frame_width;
	ipu_blitter->priv->fill_task.input.height = fill_frame_height;
	ipu_blitter->priv->fill_task.input.paddr = (dma_addr_t)(phys_mem->phys_addr);
	ipu_blitter->priv->fill_task.input.format = gst_imx_ipu_blitter_get_v4l_format(format);

	return TRUE;
}


static void gst_imx_ipu_blitter_print_ipu_fourcc(u32 format, char buf[5])
{
	int i;
	for (i = 0; i < 4; ++i)
	{
		u8 b = format >> (i * 8) & 0xff;
		buf[i] = (b < 32) ? '.' : ((char)b);
	}
	buf[4] = 0;
}


static guint32 gst_imx_ipu_blitter_get_v4l_format(GstVideoFormat format)
{
	switch (format)
	{
		/* These formats are defined in ipu.h , but the IPU reports them as
		 * being unsupported.
		 * TODO: It is currently not known how to find out which formats are supported,
		 * or if different i.MX versions support different formats.
		 */
#if 0
		case GST_VIDEO_FORMAT_RGB15: return IPU_PIX_FMT_RGB555;
		case GST_VIDEO_FORMAT_GBR: return IPU_PIX_FMT_GBR24;
		case GST_VIDEO_FORMAT_YVYU: return IPU_PIX_FMT_YVYU;
		case GST_VIDEO_FORMAT_IYU1: return IPU_PIX_FMT_Y41P;
		case GST_VIDEO_FORMAT_GRAY8: return IPU_PIX_FMT_GREY;
		case GST_VIDEO_FORMAT_YVU9: return IPU_PIX_FMT_YVU410P;
		case GST_VIDEO_FORMAT_YUV9: return IPU_PIX_FMT_YUV410P;
#endif
		case GST_VIDEO_FORMAT_RGB16: return IPU_PIX_FMT_RGB565;
		case GST_VIDEO_FORMAT_BGR: return IPU_PIX_FMT_BGR24;
		case GST_VIDEO_FORMAT_RGB: return IPU_PIX_FMT_RGB24;
		case GST_VIDEO_FORMAT_BGRx: return IPU_PIX_FMT_BGR32;
		case GST_VIDEO_FORMAT_BGRA: return IPU_PIX_FMT_BGRA32;
		case GST_VIDEO_FORMAT_RGBx: return IPU_PIX_FMT_RGB32;
		case GST_VIDEO_FORMAT_RGBA: return IPU_PIX_FMT_RGBA32;
		case GST_VIDEO_FORMAT_ABGR: return IPU_PIX_FMT_ABGR32;
		case GST_VIDEO_FORMAT_UYVY: return IPU_PIX_FMT_UYVY;
		case GST_VIDEO_FORMAT_v308: return IPU_PIX_FMT_YUV444;
		case GST_VIDEO_FORMAT_NV12: return IPU_PIX_FMT_NV12;
		case GST_VIDEO_FORMAT_YV12: return IPU_PIX_FMT_YVU420P;
		case GST_VIDEO_FORMAT_I420: return IPU_PIX_FMT_YUV420P;
		case GST_VIDEO_FORMAT_Y42B: return IPU_PIX_FMT_YUV422P;
		case GST_VIDEO_FORMAT_Y444: return IPU_PIX_FMT_YUV444P;
		default:
			GST_WARNING("Unknown format %d (%s)", (gint)format, gst_video_format_to_string(format));
			return 0;
	}
}


static int gst_imx_ipu_video_bpp(GstVideoFormat fmt)
{
	switch (fmt)
	{
		case GST_VIDEO_FORMAT_RGBx: return 4;
		case GST_VIDEO_FORMAT_BGRx: return 4;
		case GST_VIDEO_FORMAT_xRGB: return 4;
		case GST_VIDEO_FORMAT_xBGR: return 4;
		case GST_VIDEO_FORMAT_RGBA: return 4;
		case GST_VIDEO_FORMAT_BGRA: return 4;
		case GST_VIDEO_FORMAT_ARGB: return 4;
		case GST_VIDEO_FORMAT_ABGR: return 4;
		case GST_VIDEO_FORMAT_RGB: return 3;
		case GST_VIDEO_FORMAT_BGR: return 3;
		case GST_VIDEO_FORMAT_RGB16: return 2;
		case GST_VIDEO_FORMAT_BGR16: return 2;
		case GST_VIDEO_FORMAT_RGB15: return 2;
		case GST_VIDEO_FORMAT_BGR15: return 2;
		case GST_VIDEO_FORMAT_ARGB64: return 8;
		case GST_VIDEO_FORMAT_UYVY: return 2;
		default: return 1;
	}
}


static void gst_imx_ipu_blitter_set_output_rotation(GstImxIpuBlitter *ipu_blitter, GstImxCanvasInnerRotation rotation)
{
	switch (rotation)
	{
		case GST_IMX_CANVAS_INNER_ROTATION_NONE:
			ipu_blitter->priv->main_task.output.rotate = IPU_ROTATE_NONE;
			break;

		case GST_IMX_CANVAS_INNER_ROTATION_90_DEGREES:
			ipu_blitter->priv->main_task.output.rotate = IPU_ROTATE_90_RIGHT;
			break;

		case GST_IMX_CANVAS_INNER_ROTATION_180_DEGREES:
			ipu_blitter->priv->main_task.output.rotate = IPU_ROTATE_180;
			break;

		case GST_IMX_CANVAS_INNER_ROTATION_270_DEGREES:
			ipu_blitter->priv->main_task.output.rotate = IPU_ROTATE_90_LEFT;
			break;

		case GST_IMX_CANVAS_INNER_ROTATION_HFLIP:
			ipu_blitter->priv->main_task.output.rotate = IPU_ROTATE_HORIZ_FLIP;
			break;

		case GST_IMX_CANVAS_INNER_ROTATION_VFLIP:
			ipu_blitter->priv->main_task.output.rotate = IPU_ROTATE_VERT_FLIP;
			break;
	}
}
