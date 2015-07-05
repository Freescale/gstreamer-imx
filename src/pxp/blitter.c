/* PxP-based i.MX blitter class
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
#include <linux/pxp_device.h>
#include <errno.h>
#include <sys/ioctl.h>
#include "blitter.h"
#include "allocator.h"
#include "device.h"
#include "../common/phys_mem_meta.h"



GST_DEBUG_CATEGORY_STATIC(imx_pxp_blitter_debug);
#define GST_CAT_DEFAULT imx_pxp_blitter_debug


G_DEFINE_TYPE(GstImxPxPBlitter, gst_imx_pxp_blitter, GST_TYPE_IMX_BLITTER)


/* Private structure storing PxP specific data */
struct _GstImxPxPBlitterPrivate
{
	struct pxp_config_data pxp_config;
	struct pxp_chan_handle pxp_channel;
	gboolean pxp_channel_requested;
};


typedef struct
{
	unsigned int format;
	guint bits_per_pixel;
}
GstImxPxPFormatDetails;


static guint const fill_frame_width = 8;
static guint const fill_frame_height = 8;
static GstVideoFormat const fill_frame_format = GST_VIDEO_FORMAT_BGRx;


static void gst_imx_pxp_blitter_finalize(GObject *object);

static gboolean gst_imx_pxp_blitter_set_input_video_info(GstImxBlitter *blitter, GstVideoInfo const *input_video_info);
static gboolean gst_imx_pxp_blitter_set_output_video_info(GstImxBlitter *blitter, GstVideoInfo const *output_video_info);

static gboolean gst_imx_pxp_blitter_set_input_region(GstImxBlitter *blitter, GstImxRegion const *input_region);
static gboolean gst_imx_pxp_blitter_set_output_canvas(GstImxBlitter *blitter, GstImxCanvas const *output_canvas);

static gboolean gst_imx_pxp_blitter_set_input_frame(GstImxBlitter *blitter, GstBuffer *input_frame);
static gboolean gst_imx_pxp_blitter_set_output_frame(GstImxBlitter *blitter, GstBuffer *output_frame);

static GstAllocator* gst_imx_pxp_blitter_get_phys_mem_allocator(GstImxBlitter *blitter);

static gboolean gst_imx_pxp_blitter_fill_region(GstImxBlitter *blitter, GstImxRegion const *region, guint32 color);
static gboolean gst_imx_pxp_blitter_blit(GstImxBlitter *blitter, guint8 alpha);

static void gst_imx_pxp_blitter_set_layer_params(G_GNUC_UNUSED GstImxPxPBlitter *pxp_blitter, GstBuffer *video_frame, GstVideoInfo const *info, struct pxp_layer_param *layer_params);
static gboolean gst_imx_pxp_blitter_allocate_internal_fill_frame(GstImxPxPBlitter *pxp_blitter);
static void gst_imx_pxp_blitter_set_output_rotation(GstImxPxPBlitter *pxp_blitter, GstImxCanvasInnerRotation rotation);
static GstImxPxPFormatDetails const * gst_imx_pxp_blitter_get_format_details(GstVideoFormat gst_format);




static void gst_imx_pxp_blitter_class_init(GstImxPxPBlitterClass *klass)
{
	GObjectClass *object_class;
	GstImxBlitterClass *base_class;

	object_class = G_OBJECT_CLASS(klass);
	base_class = GST_IMX_BLITTER_CLASS(klass);

	object_class->finalize             = GST_DEBUG_FUNCPTR(gst_imx_pxp_blitter_finalize);
	base_class->set_input_video_info   = GST_DEBUG_FUNCPTR(gst_imx_pxp_blitter_set_input_video_info);
	base_class->set_output_video_info  = GST_DEBUG_FUNCPTR(gst_imx_pxp_blitter_set_output_video_info);
	base_class->set_input_region       = GST_DEBUG_FUNCPTR(gst_imx_pxp_blitter_set_input_region);
	base_class->set_output_canvas      = GST_DEBUG_FUNCPTR(gst_imx_pxp_blitter_set_output_canvas);
	base_class->set_input_frame        = GST_DEBUG_FUNCPTR(gst_imx_pxp_blitter_set_input_frame);
	base_class->set_output_frame       = GST_DEBUG_FUNCPTR(gst_imx_pxp_blitter_set_output_frame);
	base_class->get_phys_mem_allocator = GST_DEBUG_FUNCPTR(gst_imx_pxp_blitter_get_phys_mem_allocator);
	base_class->fill_region            = GST_DEBUG_FUNCPTR(gst_imx_pxp_blitter_fill_region);
	base_class->blit                   = GST_DEBUG_FUNCPTR(gst_imx_pxp_blitter_blit);

	GST_DEBUG_CATEGORY_INIT(imx_pxp_blitter_debug, "imxpxpblitter", 0, "Freescale i.MX PxP blitter class");
}


static void gst_imx_pxp_blitter_init(GstImxPxPBlitter *pxp_blitter)
{
	int ret;

	if (!gst_imx_pxp_open())
	{
		GST_ELEMENT_ERROR(pxp_blitter, RESOURCE, OPEN_READ_WRITE, ("could not open PxP device"), (NULL));
		return;
	}

	gst_video_info_init(&(pxp_blitter->input_video_info));
	gst_video_info_init(&(pxp_blitter->output_video_info));
	pxp_blitter->allocator = NULL;
	pxp_blitter->input_frame = NULL;
	pxp_blitter->output_frame = NULL;
	pxp_blitter->use_entire_input_frame = TRUE;

	pxp_blitter->priv = g_slice_alloc(sizeof(GstImxPxPBlitterPrivate));
	memset(pxp_blitter->priv, 0, sizeof(GstImxPxPBlitterPrivate));

	if ((ret = ioctl(gst_imx_pxp_get_fd(), PXP_IOC_GET_CHAN, &(pxp_blitter->priv->pxp_channel.handle))) != 0)
	{
		GST_ELEMENT_ERROR(pxp_blitter, RESOURCE, OPEN_READ_WRITE, ("could not request PxP channel"), ("%s", strerror(errno)));
		gst_imx_pxp_close();
		return;
	}
	else
		pxp_blitter->priv->pxp_channel_requested = TRUE;

	pxp_blitter->visibility_mask = 0;
	pxp_blitter->fill_color = 0xFF000000;
	pxp_blitter->num_empty_regions = 0;
}


GstImxPxPBlitter* gst_imx_pxp_blitter_new(void)
{
	GstAllocator *allocator;
	GstImxPxPBlitter *pxp_blitter;

	allocator = gst_imx_pxp_allocator_new();
	if (allocator == NULL)
		return NULL;

	pxp_blitter = (GstImxPxPBlitter *)g_object_new(gst_imx_pxp_blitter_get_type(), NULL);
	pxp_blitter->allocator = gst_object_ref_sink(allocator);

	if (!gst_imx_pxp_blitter_allocate_internal_fill_frame(pxp_blitter))
	{
		gst_object_unref(GST_OBJECT(pxp_blitter));
		return NULL;
	}

	return pxp_blitter;
}


static void gst_imx_pxp_blitter_finalize(GObject *object)
{
	GstImxPxPBlitter *pxp_blitter = GST_IMX_PXP_BLITTER(object);

	if (pxp_blitter->input_frame != NULL)
		gst_buffer_unref(pxp_blitter->input_frame);
	if (pxp_blitter->output_frame != NULL)
		gst_buffer_unref(pxp_blitter->output_frame);
	if (pxp_blitter->fill_frame != NULL)
		gst_buffer_unref(pxp_blitter->fill_frame);

	if (pxp_blitter->allocator != NULL)
		gst_object_unref(pxp_blitter->allocator);

	if (pxp_blitter->priv != NULL)
	{
		if (pxp_blitter->priv->pxp_channel_requested)
		{
			ioctl(gst_imx_pxp_get_fd(), PXP_IOC_PUT_CHAN, &(pxp_blitter->priv->pxp_channel.handle));
			pxp_blitter->priv->pxp_channel_requested = FALSE;
		}

		gst_imx_pxp_close();
		g_slice_free1(sizeof(GstImxPxPBlitterPrivate), pxp_blitter->priv);
	}

	G_OBJECT_CLASS(gst_imx_pxp_blitter_parent_class)->finalize(object);
}


static gboolean gst_imx_pxp_blitter_set_input_video_info(GstImxBlitter *blitter, GstVideoInfo const *input_video_info)
{
	GstImxPxPBlitter *pxp_blitter = GST_IMX_PXP_BLITTER(blitter);
	pxp_blitter->input_video_info = *input_video_info;
	return TRUE;
}


static gboolean gst_imx_pxp_blitter_set_output_video_info(GstImxBlitter *blitter, GstVideoInfo const *output_video_info)
{
	GstImxPxPBlitter *pxp_blitter = GST_IMX_PXP_BLITTER(blitter);
	pxp_blitter->output_video_info = *output_video_info;
	return TRUE;
}


static gboolean gst_imx_pxp_blitter_set_input_region(GstImxBlitter *blitter, GstImxRegion const *input_region)
{
	GstImxPxPBlitter *pxp_blitter = GST_IMX_PXP_BLITTER(blitter);

	if (input_region != NULL)
	{
		pxp_blitter->priv->pxp_config.proc_data.srect.left = input_region->x1;
		pxp_blitter->priv->pxp_config.proc_data.srect.top = input_region->y1;
		pxp_blitter->priv->pxp_config.proc_data.srect.width = input_region->x2 - input_region->x1;
		pxp_blitter->priv->pxp_config.proc_data.srect.height = input_region->y2 - input_region->y1;

		pxp_blitter->use_entire_input_frame = FALSE;
	}
	else
		pxp_blitter->use_entire_input_frame = TRUE;

	return TRUE;
}


static gboolean gst_imx_pxp_blitter_set_output_canvas(GstImxBlitter *blitter, GstImxCanvas const *output_canvas)
{
	GstImxPxPBlitter *pxp_blitter = GST_IMX_PXP_BLITTER(blitter);
	guint i;

	pxp_blitter->priv->pxp_config.proc_data.drect.left = output_canvas->clipped_inner_region.x1;
	pxp_blitter->priv->pxp_config.proc_data.drect.top = output_canvas->clipped_inner_region.y1;
	pxp_blitter->priv->pxp_config.proc_data.drect.width = output_canvas->clipped_inner_region.x2 - output_canvas->clipped_inner_region.x1;
	pxp_blitter->priv->pxp_config.proc_data.drect.height = output_canvas->clipped_inner_region.y2 - output_canvas->clipped_inner_region.y1;

	pxp_blitter->visibility_mask = output_canvas->visibility_mask;
	pxp_blitter->fill_color = output_canvas->fill_color;

	pxp_blitter->num_empty_regions = 0;
	for (i = 0; i < 4; ++i)
	{
		if ((pxp_blitter->visibility_mask & (1 << i)) == 0)
			continue;

		pxp_blitter->empty_regions[pxp_blitter->num_empty_regions] = output_canvas->empty_regions[i];
		pxp_blitter->num_empty_regions++;
	}

	gst_imx_pxp_blitter_set_output_rotation(pxp_blitter, output_canvas->inner_rotation);

	return TRUE;
}


static gboolean gst_imx_pxp_blitter_set_input_frame(GstImxBlitter *blitter, GstBuffer *input_frame)
{
	GstImxPxPBlitter *pxp_blitter = GST_IMX_PXP_BLITTER(blitter);
	gst_buffer_replace(&(pxp_blitter->input_frame), input_frame);

	if (pxp_blitter->input_frame != NULL)
	{
		gst_imx_pxp_blitter_set_layer_params(pxp_blitter, input_frame, &(pxp_blitter->input_video_info), &(pxp_blitter->priv->pxp_config.s0_param));

		if (pxp_blitter->use_entire_input_frame)
		{
			pxp_blitter->priv->pxp_config.proc_data.srect.left = 0;
			pxp_blitter->priv->pxp_config.proc_data.srect.top = 0;
			pxp_blitter->priv->pxp_config.proc_data.srect.width = GST_VIDEO_INFO_WIDTH(&(pxp_blitter->input_video_info));
			pxp_blitter->priv->pxp_config.proc_data.srect.height = GST_VIDEO_INFO_HEIGHT(&(pxp_blitter->input_video_info));
		}
	}

	return TRUE;
}


static gboolean gst_imx_pxp_blitter_set_output_frame(GstImxBlitter *blitter, GstBuffer *output_frame)
{
	GstImxPxPBlitter *pxp_blitter = GST_IMX_PXP_BLITTER(blitter);
	gst_buffer_replace(&(pxp_blitter->output_frame), output_frame);

	if (pxp_blitter->output_frame != NULL)
	{
		gst_imx_pxp_blitter_set_layer_params(pxp_blitter, output_frame, &(pxp_blitter->output_video_info), &(pxp_blitter->priv->pxp_config.out_param));
	}

	return TRUE;
}


static GstAllocator* gst_imx_pxp_blitter_get_phys_mem_allocator(GstImxBlitter *blitter)
{
	return gst_object_ref(GST_IMX_PXP_BLITTER_CAST(blitter)->allocator);
}


static gboolean gst_imx_pxp_blitter_fill_region(GstImxBlitter *blitter, GstImxRegion const *region, guint32 color)
{
	return TRUE;
}


static gboolean gst_imx_pxp_blitter_blit(GstImxBlitter *blitter, guint8 alpha)
{
	gboolean ret = TRUE;
	GstImxPxPBlitter *pxp_blitter = GST_IMX_PXP_BLITTER(blitter);
	
	if (!(pxp_blitter->priv->pxp_channel_requested))
	{
		GST_ERROR_OBJECT(pxp_blitter, "PxP channel handle wasn't requested - cannot blit");
		return FALSE;
	}

	if (ret && (pxp_blitter->visibility_mask & GST_IMX_CANVAS_VISIBILITY_FLAG_REGION_INNER))
	{
		struct pxp_config_data *pconf = &(pxp_blitter->priv->pxp_config);

		pconf->proc_data.scaling =
			(pconf->proc_data.srect.width != pconf->proc_data.drect.width) ||
			(pconf->proc_data.srect.height != pconf->proc_data.drect.height);

		pconf->handle = pxp_blitter->priv->pxp_channel.handle;
		ret = ioctl(gst_imx_pxp_get_fd(), PXP_IOC_CONFIG_CHAN, pconf);
		if (ret != 0)
		{
			GST_ERROR_OBJECT(pxp_blitter, "could not configure PxP channel: %s", strerror(errno));
			return FALSE;
		}

		ret = ioctl(gst_imx_pxp_get_fd(), PXP_IOC_START_CHAN, &(pxp_blitter->priv->pxp_channel.handle));
		if (ret != 0)
		{
			GST_ERROR_OBJECT(pxp_blitter, "could not start PxP channel: %s", strerror(errno));
			return FALSE;
		}

		ret = ioctl(gst_imx_pxp_get_fd(), PXP_IOC_WAIT4CMPLT, &(pxp_blitter->priv->pxp_channel));
		if (ret != 0)
		{
			GST_ERROR_OBJECT(pxp_blitter, "could not wait for PxP channel completion: %s", strerror(errno));
			return FALSE;
		}
	}

	return ret;
}


static void gst_imx_pxp_blitter_set_layer_params(G_GNUC_UNUSED GstImxPxPBlitter *pxp_blitter, GstBuffer *video_frame, GstVideoInfo const *info, struct pxp_layer_param *layer_params)
{
	GstVideoMeta *video_meta;
	GstImxPhysMemMeta *phys_mem_meta;
	GstVideoFormat format;
	GstImxPxPFormatDetails const *format_details;
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

	format_details = gst_imx_pxp_blitter_get_format_details(format);
	g_assert(format_details != NULL);

	/* In theory, the stride value could be set to padded_width and the
	 * width value to the actual width. Unfortunately, there is no
	 * more-or-less equivalent value for the height, and/or no way to
	 * specify plane offsets. Therefore, set the padded width&height here,
	 * and seleect a sub rect later in the proc_data (similar to how it is
	 * done in GstImxIpuBlitter). */
	layer_params->width = padded_width;
	layer_params->height = padded_height;
	layer_params->stride = padded_width;
	layer_params->pixel_fmt = format_details->format;

	layer_params->paddr = (dma_addr_t)(phys_mem_meta->phys_addr);

#if 0
	layer_params->combine_enable = FALSE;

	layer_params->color_key_enable = 0;
	layer_params->color_key = 0x00000000;
	layer_params->global_alpha_enable = FALSE;
	layer_params->global_override = FALSE;
	layer_params->global_alpha = 0;
	layer_params->alpha_invert = FALSE;
	layer_params->local_alpha_enable = FALSE;
#endif
}


static gboolean gst_imx_pxp_blitter_allocate_internal_fill_frame(GstImxPxPBlitter *pxp_blitter)
{
	//GstImxPhysMemory *phys_mem;

	/* Not using the dma bufferpool for this, since that bufferpool will
	 * be configured to input frame sizes. Plus, the pool wouldn't yield any benefits here. */
	pxp_blitter->fill_frame = gst_buffer_new_allocate(
		pxp_blitter->allocator,
		fill_frame_width * fill_frame_height * gst_imx_pxp_blitter_get_format_details(fill_frame_format)->bits_per_pixel / 8,
		NULL
	);

	if (pxp_blitter->fill_frame == NULL)
	{
		GST_ERROR_OBJECT(pxp_blitter, "could not allocate internal fill frame");
		return FALSE;
	}

	//phys_mem = (GstImxPhysMemory *)gst_buffer_peek_memory(pxp_blitter->fill_frame, 0);

	return TRUE;
}


static void gst_imx_pxp_blitter_set_output_rotation(GstImxPxPBlitter *pxp_blitter, GstImxCanvasInnerRotation rotation)
{
	struct pxp_proc_data *proc_data = &(pxp_blitter->priv->pxp_config.proc_data);

	switch (rotation)
	{
		case GST_IMX_CANVAS_INNER_ROTATION_NONE:
			proc_data->rotate = 0;
			proc_data->hflip = 0;
			proc_data->vflip = 0;
			break;

		case GST_IMX_CANVAS_INNER_ROTATION_90_DEGREES:
			proc_data->rotate = 90;
			proc_data->hflip = 0;
			proc_data->vflip = 0;
			break;

		case GST_IMX_CANVAS_INNER_ROTATION_180_DEGREES:
			proc_data->rotate = 180;
			proc_data->hflip = 0;
			proc_data->vflip = 0;
			break;

		case GST_IMX_CANVAS_INNER_ROTATION_270_DEGREES:
			proc_data->rotate = 270;
			proc_data->hflip = 0;
			proc_data->vflip = 0;
			break;

		case GST_IMX_CANVAS_INNER_ROTATION_HFLIP:
			proc_data->rotate = 0;
			proc_data->hflip = 1;
			proc_data->vflip = 0;
			break;

		case GST_IMX_CANVAS_INNER_ROTATION_VFLIP:
			proc_data->rotate = 0;
			proc_data->hflip = 0;
			proc_data->vflip = 1;
			break;
	}
}


static GstImxPxPFormatDetails const * gst_imx_pxp_blitter_get_format_details(GstVideoFormat gstfmt)
{
#define FORMAT_DETAILS(PXPFMT, BPP) \
		do { static GstImxPxPFormatDetails const d = { PXPFMT, BPP }; return &d; } while (0)

	/* NOTE: for the RGBx/BGRx formats, PxP RGB == GStreamer BGR , and vice versa */
	switch (gstfmt)
	{
		/* packed-pixel formats */
		case GST_VIDEO_FORMAT_BGRx: FORMAT_DETAILS(PXP_PIX_FMT_RGB32, 32);
		case GST_VIDEO_FORMAT_BGRA: FORMAT_DETAILS(PXP_PIX_FMT_BGRA32, 32);
		case GST_VIDEO_FORMAT_RGB16: FORMAT_DETAILS(PXP_PIX_FMT_RGB565, 16);
		case GST_VIDEO_FORMAT_GRAY8: FORMAT_DETAILS(PXP_PIX_FMT_GREY, 8);
		case GST_VIDEO_FORMAT_YUY2: FORMAT_DETAILS(PXP_PIX_FMT_YUYV, 16);
		case GST_VIDEO_FORMAT_UYVY: FORMAT_DETAILS(PXP_PIX_FMT_UYVY, 16);
		case GST_VIDEO_FORMAT_YVYU: FORMAT_DETAILS(PXP_PIX_FMT_YVYU, 16);

		/* planar formats; bits per pixel is always 8 for these */
		case GST_VIDEO_FORMAT_I420: FORMAT_DETAILS(PXP_PIX_FMT_YUV420P, 8);
		case GST_VIDEO_FORMAT_YV12: FORMAT_DETAILS(PXP_PIX_FMT_YVU420P, 8);
		case GST_VIDEO_FORMAT_Y42B: FORMAT_DETAILS(PXP_PIX_FMT_YUV422P, 8);
		case GST_VIDEO_FORMAT_NV12: FORMAT_DETAILS(PXP_PIX_FMT_NV12, 8);

		default: return NULL;
	}

#undef FORMAT_DETAILS
}






