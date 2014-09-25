/* Common Freescale IPU blitter code for GStreamer
 * Copyright (C) 2014  Carlos Rafael Giani
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


#include <gst/gst.h>
#include <gst/video/gstvideometa.h>
#include <sys/ioctl.h>
#include <string.h>
#include <errno.h>
#include <linux/ipu.h>
#include "blitter.h"
#include "allocator.h"
#include "device.h"
#include "../common/phys_mem_meta.h"


GST_DEBUG_CATEGORY_STATIC(imx_ipu_blitter_debug);
#define GST_CAT_DEFAULT imx_ipu_blitter_debug


G_DEFINE_TYPE(GstImxIpuBlitter, gst_imx_ipu_blitter, GST_TYPE_IMX_BASE_BLITTER)


/* Private structure storing IPU specific data */
struct _GstImxIpuBlitterPrivate
{
	struct ipu_task task;
};


static void gst_imx_ipu_blitter_finalize(GObject *object);

static gboolean gst_imx_ipu_blitter_set_input_video_info(GstImxBaseBlitter *base_blitter, GstVideoInfo *input_video_info);
static gboolean gst_imx_ipu_blitter_set_input_frame(GstImxBaseBlitter *base_blitter, GstBuffer *input_frame);
static gboolean gst_imx_ipu_blitter_set_output_frame(GstImxBaseBlitter *base_blitter, GstBuffer *output_frame);
static gboolean gst_imx_ipu_blitter_set_regions(GstImxBaseBlitter *base_blitter, GstImxBaseBlitterRegion const *video_region, GstImxBaseBlitterRegion const *output_region);
static GstAllocator* gst_imx_ipu_blitter_get_phys_mem_allocator(GstImxBaseBlitter *base_blitter);
static gboolean gst_imx_ipu_blitter_blit_frame(GstImxBaseBlitter *base_blitter);

static guint32 gst_imx_ipu_blitter_get_v4l_format(GstVideoFormat format);
//static int gst_imx_ipu_video_bpp(GstVideoFormat fmt);




GType gst_imx_ipu_blitter_rotation_mode_get_type(void)
{
	static GType gst_imx_ipu_blitter_rotation_mode_type = 0;

	if (!gst_imx_ipu_blitter_rotation_mode_type)
	{
		static GEnumValue rotation_mode_values[] =
		{
			{ GST_IMX_IPU_BLITTER_ROTATION_NONE, "No rotation", "none" },
			{ GST_IMX_IPU_BLITTER_ROTATION_HFLIP, "Flip horizontally", "horizontal-flip" },
			{ GST_IMX_IPU_BLITTER_ROTATION_VFLIP, "Flip vertically", "vertical-flip" },
			{ GST_IMX_IPU_BLITTER_ROTATION_180, "Rotate 180 degrees", "rotate-180" },
			{ GST_IMX_IPU_BLITTER_ROTATION_90CW, "Rotate clockwise 90 degrees", "rotate-90cw" },
			{ GST_IMX_IPU_BLITTER_ROTATION_90CW_HFLIP, "Rotate clockwise 90 degrees and flip horizontally", "rotate-90cw-hflip" },
			{ GST_IMX_IPU_BLITTER_ROTATION_90CW_VFLIP, "Rotate clockwise 90 degrees and flip vertically", "rotate-90cw-vflip" },
			{ GST_IMX_IPU_BLITTER_ROTATION_90CCW, "Rotate counter-clockwise 90 degrees", "rotate-90ccw" },
			{ 0, NULL, NULL },
		};

		gst_imx_ipu_blitter_rotation_mode_type = g_enum_register_static(
			"ImxIpuBlitterRotationMode",
			rotation_mode_values
		);
	}

	return gst_imx_ipu_blitter_rotation_mode_type;
}


GType gst_imx_ipu_blitter_deinterlace_mode_get_type(void)
{
	static GType gst_imx_ipu_blitter_deinterlace_mode_type = 0;

	if (!gst_imx_ipu_blitter_deinterlace_mode_type)
	{
		static GEnumValue deinterlace_mode_values[] =
		{
			{ GST_IMX_IPU_BLITTER_DEINTERLACE_NONE, "No deinterlacing", "none" },
			{ GST_IMX_IPU_BLITTER_DEINTERLACE_SLOW_MOTION, "Slow-motion deinterlacing (uses two input frames for one output frame)", "slow-motion" },
			{ GST_IMX_IPU_BLITTER_DEINTERLACE_FAST_MOTION, "Fast-motion deinterlacing (uses one input frames for one output frame)", "fast-motion" },
			{ 0, NULL, NULL },
		};

		gst_imx_ipu_blitter_deinterlace_mode_type = g_enum_register_static(
			"ImxIpuBlitterDeinterlaceMode",
			deinterlace_mode_values
		);
	}

	return gst_imx_ipu_blitter_deinterlace_mode_type;
}




void gst_imx_ipu_blitter_class_init(GstImxIpuBlitterClass *klass)
{
	GObjectClass *object_class;
	GstImxBaseBlitterClass *base_class;

	object_class = G_OBJECT_CLASS(klass);
	base_class = GST_IMX_BASE_BLITTER_CLASS(klass);

	object_class->finalize             = GST_DEBUG_FUNCPTR(gst_imx_ipu_blitter_finalize);
	base_class->set_input_video_info   = GST_DEBUG_FUNCPTR(gst_imx_ipu_blitter_set_input_video_info);
	base_class->set_input_frame        = GST_DEBUG_FUNCPTR(gst_imx_ipu_blitter_set_input_frame);
	base_class->set_output_frame       = GST_DEBUG_FUNCPTR(gst_imx_ipu_blitter_set_output_frame);
	base_class->set_regions            = GST_DEBUG_FUNCPTR(gst_imx_ipu_blitter_set_regions);
	base_class->get_phys_mem_allocator = GST_DEBUG_FUNCPTR(gst_imx_ipu_blitter_get_phys_mem_allocator);
	base_class->blit_frame             = GST_DEBUG_FUNCPTR(gst_imx_ipu_blitter_blit_frame);

	GST_DEBUG_CATEGORY_INIT(imx_ipu_blitter_debug, "imxipublitter", 0, "Freescale i.MX IPU blitter class");
}


void gst_imx_ipu_blitter_init(GstImxIpuBlitter *ipu_blitter)
{
	ipu_blitter->priv = g_slice_alloc(sizeof(GstImxIpuBlitterPrivate));
	memset(&(ipu_blitter->priv->task), 0, sizeof(struct ipu_task));

	if (!gst_imx_ipu_open())
	{
		GST_ELEMENT_ERROR(ipu_blitter, RESOURCE, OPEN_READ_WRITE, ("could not open IPU device"), (NULL));
		return;
	}

	GST_INFO_OBJECT(ipu_blitter, "initialized blitter");
}


GstImxIpuBlitter* gst_imx_ipu_blitter_new(void)
{
	GstImxIpuBlitter* ipu_blitter = (GstImxIpuBlitter *)g_object_new(gst_imx_ipu_blitter_get_type(), NULL);

	return ipu_blitter;
}


void gst_imx_ipu_blitter_enable_crop(GstImxIpuBlitter *ipu_blitter, gboolean crop)
{
	GST_TRACE_OBJECT(ipu_blitter, "set crop to %d", crop);
	ipu_blitter->apply_crop_metadata = crop;
}


gboolean gst_imx_ipu_blitter_is_crop_enabled(GstImxIpuBlitter *ipu_blitter)
{
	return ipu_blitter->apply_crop_metadata;
}


void gst_imx_ipu_blitter_set_output_rotation_mode(GstImxIpuBlitter *ipu_blitter, GstImxIpuBlitterRotationMode rotation_mode)
{
	switch (rotation_mode)
	{
		case GST_IMX_IPU_BLITTER_ROTATION_NONE:       ipu_blitter->priv->task.output.rotate = IPU_ROTATE_NONE; break;
		case GST_IMX_IPU_BLITTER_ROTATION_HFLIP:      ipu_blitter->priv->task.output.rotate = IPU_ROTATE_HORIZ_FLIP; break;
		case GST_IMX_IPU_BLITTER_ROTATION_VFLIP:      ipu_blitter->priv->task.output.rotate = IPU_ROTATE_VERT_FLIP; break;
		case GST_IMX_IPU_BLITTER_ROTATION_180:        ipu_blitter->priv->task.output.rotate = IPU_ROTATE_180; break;
		case GST_IMX_IPU_BLITTER_ROTATION_90CW:       ipu_blitter->priv->task.output.rotate = IPU_ROTATE_90_RIGHT; break;
		case GST_IMX_IPU_BLITTER_ROTATION_90CW_HFLIP: ipu_blitter->priv->task.output.rotate = IPU_ROTATE_90_RIGHT_HFLIP; break;
		case GST_IMX_IPU_BLITTER_ROTATION_90CW_VFLIP: ipu_blitter->priv->task.output.rotate = IPU_ROTATE_90_RIGHT_VFLIP; break;
		case GST_IMX_IPU_BLITTER_ROTATION_90CCW:      ipu_blitter->priv->task.output.rotate = IPU_ROTATE_90_LEFT; break;
	}
}


GstImxIpuBlitterRotationMode gst_imx_ipu_blitter_get_output_rotation_mode(GstImxIpuBlitter *ipu_blitter)
{
	switch (ipu_blitter->priv->task.output.rotate)
	{
		case IPU_ROTATE_NONE:           return GST_IMX_IPU_BLITTER_ROTATION_NONE;
		case IPU_ROTATE_HORIZ_FLIP:     return GST_IMX_IPU_BLITTER_ROTATION_HFLIP;
		case IPU_ROTATE_VERT_FLIP:      return GST_IMX_IPU_BLITTER_ROTATION_VFLIP;
		case IPU_ROTATE_180:            return GST_IMX_IPU_BLITTER_ROTATION_180;
		case IPU_ROTATE_90_RIGHT:       return GST_IMX_IPU_BLITTER_ROTATION_90CW;
		case IPU_ROTATE_90_RIGHT_HFLIP: return GST_IMX_IPU_BLITTER_ROTATION_90CW_HFLIP;
		case IPU_ROTATE_90_RIGHT_VFLIP: return GST_IMX_IPU_BLITTER_ROTATION_90CW_VFLIP;
		case IPU_ROTATE_90_LEFT:        return GST_IMX_IPU_BLITTER_ROTATION_90CCW;
		default:                        return GST_IMX_IPU_BLITTER_ROTATION_NONE;
	}
}


void gst_imx_ipu_blitter_set_deinterlace_mode(GstImxIpuBlitter *ipu_blitter, GstImxIpuBlitterDeinterlaceMode deinterlace_mode)
{
	// TODO: save previous buffer for slow deinterlace mode

	switch (deinterlace_mode)
	{
		case GST_IMX_IPU_BLITTER_DEINTERLACE_NONE:
			GST_DEBUG_OBJECT(ipu_blitter, "set deinterlace mode to none");
			ipu_blitter->priv->task.input.deinterlace.motion = MED_MOTION;
			break;

		case GST_IMX_IPU_BLITTER_DEINTERLACE_SLOW_MOTION:
			GST_DEBUG_OBJECT(ipu_blitter, "set deinterlace mode to slow mode");
			ipu_blitter->priv->task.input.deinterlace.motion = LOW_MOTION;
			break;
			
		case GST_IMX_IPU_BLITTER_DEINTERLACE_FAST_MOTION:
			GST_DEBUG_OBJECT(ipu_blitter, "set deinterlace mode to fast mode");
			ipu_blitter->priv->task.input.deinterlace.motion = HIGH_MOTION;
			break;
	}

	ipu_blitter->deinterlace_mode = deinterlace_mode;
}


GstImxIpuBlitterDeinterlaceMode gst_imx_ipu_blitter_get_deinterlace_mode(GstImxIpuBlitter *ipu_blitter)
{
	return ipu_blitter->deinterlace_mode;
}


static void gst_imx_ipu_blitter_finalize(GObject *object)
{
	GstImxIpuBlitter *ipu_blitter = GST_IMX_IPU_BLITTER(object);

	if (ipu_blitter->priv != NULL)
	{
		gst_imx_ipu_close();
		g_slice_free1(sizeof(GstImxIpuBlitterPrivate), ipu_blitter->priv);
	}

	GST_INFO_OBJECT(object, "shut down IPU blitter");

	G_OBJECT_CLASS(gst_imx_ipu_blitter_parent_class)->finalize(object);
}


static gboolean gst_imx_ipu_blitter_set_input_video_info(GstImxBaseBlitter *base_blitter, GstVideoInfo *input_video_info)
{
	GstImxIpuBlitter *ipu_blitter = GST_IMX_IPU_BLITTER(base_blitter);
	ipu_blitter->input_video_info = *input_video_info;
	return TRUE;
}


#define GST_IMX_FILL_IPU_TASK(ipu_blitter, buffer, taskio) \
do { \
 \
	GstVideoMeta *video_meta; \
	GstVideoCropMeta *video_crop_meta; \
	GstImxPhysMemMeta *phys_mem_meta; \
 \
	video_meta = gst_buffer_get_video_meta(buffer); \
	video_crop_meta = gst_buffer_get_video_crop_meta(buffer); \
	phys_mem_meta = GST_IMX_PHYS_MEM_META_GET(buffer); \
 \
	g_assert((video_meta != NULL) && (phys_mem_meta != NULL) && (phys_mem_meta->phys_addr != 0)); \
 \
	(taskio).width = video_meta->width + phys_mem_meta->x_padding; \
	(taskio).height = video_meta->height + phys_mem_meta->y_padding; \
 \
	if (ipu_blitter->apply_crop_metadata && (video_crop_meta != NULL)) \
	{ \
		if ((video_crop_meta->x >= (guint)(video_meta->width)) || (video_crop_meta->y >= (guint)(video_meta->height))) \
			return FALSE; \
 \
		(taskio).crop.pos.x = video_crop_meta->x; \
		(taskio).crop.pos.y = video_crop_meta->y; \
		(taskio).crop.w = MIN(video_crop_meta->width, video_meta->width - video_crop_meta->x); \
		(taskio).crop.h = MIN(video_crop_meta->height, video_meta->height - video_crop_meta->y); \
	} \
	else \
	{ \
		(taskio).crop.pos.x = 0; \
		(taskio).crop.pos.y = 0; \
		(taskio).crop.w = video_meta->width; \
		(taskio).crop.h = video_meta->height; \
	} \
 \
	(taskio).paddr = (dma_addr_t)(phys_mem_meta->phys_addr); \
	(taskio).format = gst_imx_ipu_blitter_get_v4l_format(video_meta->format); \
} while (0)


static gboolean gst_imx_ipu_blitter_set_input_frame(GstImxBaseBlitter *base_blitter, GstBuffer *input_frame)
{
	GstImxIpuBlitter *ipu_blitter = GST_IMX_IPU_BLITTER(base_blitter);

	GST_IMX_FILL_IPU_TASK(ipu_blitter, input_frame, ipu_blitter->priv->task.input);

	ipu_blitter->priv->task.input.deinterlace.enable = 0;

	if (ipu_blitter->deinterlace_mode != GST_IMX_IPU_BLITTER_DEINTERLACE_NONE)
	{
		switch (ipu_blitter->input_video_info.interlace_mode)
		{
			case GST_VIDEO_INTERLACE_MODE_INTERLEAVED:
				GST_LOG_OBJECT(ipu_blitter, "input stream uses interlacing -> deinterlacing enabled");
				ipu_blitter->priv->task.input.deinterlace.enable = 1;
				break;

			case GST_VIDEO_INTERLACE_MODE_MIXED:
			{
				GstVideoMeta *video_meta;

				// TODO: in the base class, copy meta info to temporary input buffer

				GST_LOG_OBJECT(ipu_blitter, "input stream uses mixed interlacing -> need to check video metadata deinterlacing flag");

				video_meta = gst_buffer_get_video_meta(input_frame);
				if (video_meta != NULL)
				{
					if (video_meta->flags & GST_VIDEO_FRAME_FLAG_INTERLACED)
					{
						GST_LOG_OBJECT(ipu_blitter, "frame has video metadata and deinterlacing flag");
						ipu_blitter->priv->task.input.deinterlace.enable = 1;
					}
					else
						GST_LOG_OBJECT(ipu_blitter, "frame has video metadata but no deinterlacing flag");
				}
				else
					GST_TRACE_OBJECT(ipu_blitter, "frame has no video metadata -> no deinterlacing done");

				break;
			}

			case GST_VIDEO_INTERLACE_MODE_PROGRESSIVE:
				GST_LOG_OBJECT(ipu_blitter, "input stream is progressive -> no deinterlacing necessary");
				break;

			case GST_VIDEO_INTERLACE_MODE_FIELDS:
				GST_FIXME_OBJECT(ipu_blitter, "2-fields deinterlacing not supported (yet)");
				break;

			default:
				break;
		}
	}

	return TRUE;
}


static gboolean gst_imx_ipu_blitter_set_output_frame(GstImxBaseBlitter *base_blitter, GstBuffer *output_frame)
{
	GstImxIpuBlitter *ipu_blitter = GST_IMX_IPU_BLITTER(base_blitter);
	GST_IMX_FILL_IPU_TASK(ipu_blitter, output_frame, ipu_blitter->priv->task.output);
	return TRUE;
}


static gboolean gst_imx_ipu_blitter_set_regions(GstImxBaseBlitter *base_blitter, GstImxBaseBlitterRegion const *video_region, GstImxBaseBlitterRegion const *output_region)
{
	return TRUE;
}


static GstAllocator* gst_imx_ipu_blitter_get_phys_mem_allocator(G_GNUC_UNUSED GstImxBaseBlitter *base_blitter)
{
	return gst_imx_ipu_allocator_new();
}


static gboolean gst_imx_ipu_blitter_blit_frame(GstImxBaseBlitter *base_blitter)
{
	int ret;
	GstImxIpuBlitter *ipu_blitter = GST_IMX_IPU_BLITTER(base_blitter);

	GST_LOG_OBJECT(
		ipu_blitter,
		"task input:  width:  %u  height: %u  format: 0x%x  crop: %u,%u %ux%u  phys addr %" GST_IMX_PHYS_ADDR_FORMAT "  deinterlace enable %u motion 0x%x",
		ipu_blitter->priv->task.input.width, ipu_blitter->priv->task.input.height,
		ipu_blitter->priv->task.input.format,
		ipu_blitter->priv->task.input.crop.pos.x, ipu_blitter->priv->task.input.crop.pos.y, ipu_blitter->priv->task.input.crop.w, ipu_blitter->priv->task.input.crop.h,
		(gst_imx_phys_addr_t)(ipu_blitter->priv->task.input.paddr),
		ipu_blitter->priv->task.input.deinterlace.enable, ipu_blitter->priv->task.input.deinterlace.motion
	);
	GST_LOG_OBJECT(
		ipu_blitter,
		"task output:  width:  %u  height: %u  format: 0x%x  crop: %u,%u %ux%u  paddr %" GST_IMX_PHYS_ADDR_FORMAT "  rotate: %u",
		ipu_blitter->priv->task.output.width, ipu_blitter->priv->task.output.height,
		ipu_blitter->priv->task.output.format,
		ipu_blitter->priv->task.output.crop.pos.x, ipu_blitter->priv->task.output.crop.pos.y, ipu_blitter->priv->task.output.crop.w, ipu_blitter->priv->task.output.crop.h,
		(gst_imx_phys_addr_t)(ipu_blitter->priv->task.output.paddr),
		ipu_blitter->priv->task.output.rotate
	);

	/* The actual blit operation
	 * Input and output frame are assumed to be set up properly at this point
	 */
	ret = ioctl(gst_imx_ipu_get_fd(), IPU_QUEUE_TASK, &(ipu_blitter->priv->task));

	if (ret == -1)
	{
		GST_ERROR_OBJECT(ipu_blitter, "queuing IPU task failed: %s", strerror(errno));
		return FALSE;
	}
	else
		return TRUE;
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


/* Determines the number of bytes per pixel used by the IPU for the given formats;
 * necessary for calculations in the GST_IMX_FILL_IPU_TASK macro */
/*static int gst_imx_ipu_video_bpp(GstVideoFormat fmt)
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
}*/
